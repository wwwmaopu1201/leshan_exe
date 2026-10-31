#include <QScreen>
#include "management_pages.h"
#include "api_client.h"
#include "client_window.h"
#include "client_file_dialog.h"
#include "form_dialog.h"
#include "list_page.h"
#include "page_widgets.h"
#include "ui_dialogs.h"
#include "ui_controls.h"
#include "ui_i18n.h"
#include "excel_export.h"

#include <QFile>
#include <QComboBox>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QScrollArea>
#include <QTextStream>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>
#include <algorithm>

namespace {
class DeviceDragTree : public QTreeWidget {
public:
    std::function<void(qint64,const QJsonValue&)> moved;
protected:
    void startDrag(Qt::DropActions actions) override {
        dragged_=currentItem(); QTreeWidget::startDrag(actions); dragged_=nullptr;
    }
    void dropEvent(QDropEvent *event) override {
        QTreeWidgetItem *source=dragged_, *target=itemAt(event->position().toPoint());
        if(!source || !target || source==target ||
           source->data(0,Qt::UserRole+1).toString()!="device") { event->ignore(); return; }
        const QString kind=target->data(0,Qt::UserRole+1).toString();
        QJsonValue group(QJsonValue::Undefined);
        if(kind=="ungrouped") group=QJsonValue(QJsonValue::Null);
        else if(kind=="group") group=QJsonValue::fromVariant(target->data(0,Qt::UserRole));
        else if(kind=="device" && target->parent()) {
            QTreeWidgetItem *parent=target->parent();
            if(parent->data(0,Qt::UserRole+1).toString()=="group")
                group=QJsonValue::fromVariant(parent->data(0,Qt::UserRole));
            else if(parent->data(0,Qt::UserRole+1).toString()=="ungrouped")
                group=QJsonValue(QJsonValue::Null);
        }
        if(group.isUndefined()) { event->ignore(); return; }
        if(moved) moved(source->data(0,Qt::UserRole).toLongLong(),group);
        event->acceptProposedAction();
    }
private:
    QTreeWidgetItem *dragged_=nullptr;
};
ListSpec catalogSpec(const QString &path) {
    const bool device=path.startsWith("/device");
    const bool electric=path.endsWith("electric-control-types");
    const bool order=path.endsWith("order-nos");
    ListSpec spec;
    spec.title=order ? "订单编号管理" : electric ? "电控类型" : device ? "设备类型" : "花型类型管理";
    spec.description=order ? "管理花型订单编号及关联记录。" : "管理类型目录及其关联数量。";
    spec.path=order ? "/pattern/order-summary" : electric ? "/device/electric-control-type-summary" :
        device ? "/device/type-summary" : "/pattern/type-summary";
    spec.searchHint=order ? "搜索订单编号" : "搜索类型名称";
    spec.serverPaged=false; spec.selection=true;
    spec.showHeader=false;
    spec.columns={{"序号","#",70},{order ? "订单编号" : spec.title,"value",230},
        {device ? "关联设备数" : "关联花型数",device ? "deviceCount" : "patternCount",140},
        {"更新时间","updateTime",190}};
    spec.actions={"修改名称",order ? "清空编号" : device ? "删除类型" : "清空类型"};
    return spec;
}

class CatalogPage : public ListPage {
public:
    CatalogPage(ApiClient *api,ClientWindow *window,const QString &path)
        :ListPage(api,window,catalogSpec(path)),endpoint_(catalogSpec(path).path) {
        const bool device=path.startsWith("/device");
        relatedPath_=device ? "/device/list" : "/file/pattern";
        relatedKey_=path.endsWith("electric-control-types") ? "electricControlType" :
            device ? "type" : path.endsWith("order-nos") ? "orderNo" : "patternType";
        table_->setProperty("linkCounts",true);
        addToolbarButton(path.endsWith("order-nos") ? "新增编号" : "新增类型",[this,path] {
            const QString title=path.endsWith("order-nos") ? "新增订单编号" :
                path.endsWith("electric-control-types") ? "新增电控类型" :
                path.startsWith("/device") ? "新增设备类型" : "新增花型类型";
            const auto value=UiMessageBox::prompt(this,title,"请输入名称",{},100,path.endsWith("order-nos"));
            if(!value) return;
            api_->post(endpoint_,QJsonObject{{"value",*value}},[this](const ApiResponse &result) {
                if(!result.ok) { window_->notify(result.error,true); return; }
                window_->notify("新增成功"); refresh();
            });
        },true);
        addToolbarButton("批量修改",[this] {
            const auto selected=table_->checkedRecords();
            if(selected.isEmpty()) { window_->notify("请勾选需要修改的条目",true); return; }
            batchRename(selected);
        });
        addToolbarButton(path.endsWith("order-nos") ? "清空编号" : device ? "删除类型" : "清空类型",
            [this] { removeSelected(); });
        addToolbarButton("刷新",[this]{ refresh(); window_->notify("列表已刷新"); });
        setActionHandler([this](ListPage *,const QJsonObject &row,const QString &action) {
            const QString value=row.value("value").toString();
            if(action=="修改名称") rename(value);
            else removeOne(value);
        });
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column) {
            if(column!=table_->columnForKey(relatedPath_.startsWith("/device") ? "deviceCount" : "patternCount")) return;
            const QString value=table_->recordAt(row).value("value").toString();
            if(!value.isEmpty()) window_->navigateFiltered(relatedPath_,relatedKey_,value);
        });
    }
private:
    void batchRename(const QList<QJsonObject> &selected) {
        ClientFormDialog dialog(this,"批量修改名称",640,540);dialog.setConfirmDiscard(true);
        QSet<QString> originalNames,existingNames;
        for(const auto &value:rows())existingNames.insert(value.toObject().value("value").toString().toCaseFolded());
        for(int i=0;i<selected.size();++i) {
            const QString old=selected[i].value("value").toString();originalNames.insert(old.toCaseFolded());
            FormField field;field.key=QString::number(i);field.label=old;field.preserveLabel=true;field.required=true;field.maximum=100;if(relatedKey_=="orderNo")field.inputPattern="^[A-Za-z0-9]{0,100}$";dialog.addField(field,old);
        }
        dialog.setValidation([=](const QJsonObject &values){QSet<QString> names;for(int i=0;i<selected.size();++i){const QString value=values.value(QString::number(i)).toString().trimmed().toCaseFolded();if(relatedKey_=="orderNo" && !QRegularExpression("^[A-Za-z0-9]+$").match(value).hasMatch())return QString("订单编号只允许字母和数字");if(value.isEmpty())return QString("名称不能为空");if(names.contains(value))return QString("批量修改中存在重复名称");if(existingNames.contains(value) && !originalNames.contains(value))return QString("名称已存在，整批未修改");names.insert(value);}return QString();});
        const auto values=dialog.execute();if(!values)return;QJsonArray renames;
        for(int i=0;i<selected.size();++i)if(selected[i].value("value")!=values->value(QString::number(i)))renames.append(QJsonObject{{"oldValue",selected[i].value("value")},{"newValue",values->value(QString::number(i))}});
        if(renames.isEmpty())return;
        api_->put(endpoint_,QJsonObject{{"renames",renames}},[this](const ApiResponse &result){if(!result.ok){window_->notify(result.error,true);return;}window_->notify("批量修改成功");refresh();});
    }
    void rename(const QString &value) {
        const auto replacement=UiMessageBox::prompt(this,"修改名称","请输入新名称",value,100,relatedKey_=="orderNo");
        if(!replacement || *replacement==value) return;
        api_->put(endpoint_,QJsonObject{{"oldValue",value},{"newValue",*replacement}},
            [this](const ApiResponse &result) {
                if(!result.ok) { window_->notify(result.error,true); return; }
                window_->notify("修改成功"); refresh();
            });
    }
    void removeOne(const QString &value) {
        if(UiMessageBox::question(this,"确认操作","确定清空或删除“"+value+"”吗？")!=UiMessageBox::Yes) return;
        api_->remove(endpoint_,QJsonObject{{"value",value}},[this](const ApiResponse &result) {
            if(!result.ok) { window_->notify(result.error,true); return; }
            window_->notify("操作成功"); refresh();
        });
    }
    void removeSelected() {
        const auto selected=table_->checkedRecords();
        if(selected.isEmpty()) { window_->notify("请先勾选条目",true); return; }
        if(UiMessageBox::question(this,"确认操作",
            QString("确定清理选中的 %1 个条目吗？关联数据会保留。").arg(selected.size()))!=UiMessageBox::Yes) return;
        auto index=std::make_shared<int>(0);
        auto next=std::make_shared<std::function<void()>>();
        *next=[this,selected,index,next] {
            if(*index>=selected.size()) { window_->notify("操作成功"); refresh(); *next={}; return; }
            api_->remove(endpoint_,QJsonObject{{"value",selected[*index].value("value")}},
                [this,index,next](const ApiResponse &result) {
                    if(!result.ok) { window_->notify(result.error,true); *next={}; refresh(); return; }
                    ++*index; (*next)();
                });
        };
        (*next)();
    }
    QString endpoint_;
    QString relatedPath_,relatedKey_;
};

ListSpec deviceSpec(bool groups,bool monitor) {
    ListSpec spec;
    spec.title=groups ? "分组设备列表" : "设备列表";
    spec.description=groups ? "按分组管理设备、调整排序并移动设备。" : "查看设备状态、编辑资料和管理所属分组。";
    spec.path="/device/list?fullModel=1";
    spec.searchHint="设备名/编号";spec.searchHelp="支持设备名、编号、类型、员工和主板号";
    spec.optionLabel="设备状态"; spec.optionKey="status";
    spec.options={{"全部状态",""},{"空闲","idle"},{"缝纫","working"},{"关机","offline"}};
    spec.dates=true; spec.selection=true; spec.serverPaged=false; spec.refreshSeconds=5;
    spec.showHeader=false;
    spec.dateWidth=360;
    spec.columns={{"序号","#",65},{"设备编号","code",125},{"设备名称","name",190},
        {"初始名称","initialName",140},{"设备类型","type",120},{"电控类型","electricControlType",125},
        {"员工工号","employeeCode",120},{"员工姓名","employeeName",120},{"主板编号","mainboardSn",145},
        {"IP地址","ip",125},{"所属分组","groupName",135},{"设备状态","status",110},
        {"备注","remark",160},{"创建时间","createdAt",175}};
    if(groups) spec.columns.append({"排序","sortOrder",80});
    spec.actions=groups ? QStringList{"上移","下移","编辑","移出"} : QStringList{"编辑","移出"};
    if(monitor)spec.actions.insert(spec.actions.size()-1,"监控");
    return spec;
}

class DevicePage : public ClientPage {
public:
    DevicePage(ApiClient *api,ClientWindow *window,bool groups):api_(api),window_(window),groups_(groups) {
        setObjectName("clientPage");
        auto *layout=new QHBoxLayout(this); layout->setContentsMargins(9,9,9,9); layout->setSpacing(8);
        auto *panel=clientCard(); panel->setFixedWidth(250);
        auto *side=new QVBoxLayout(panel); side->setContentsMargins(9,10,9,10); side->setSpacing(8);
        auto *header=new QHBoxLayout; header->addWidget(clientHeading("设备树与分组")); header->addStretch();
        auto *reload=new QPushButton("刷新"); reload->setObjectName("clientLink"); header->addWidget(reload);
        side->addLayout(header);
        auto *legend=new QHBoxLayout;
        for(const auto &state:{QPair<QString,QString>{"空闲","#67c23a"},{"缝纫","#f56c6c"},{"关机","#909399"}}) {
            auto *label=new QLabel(state.first);label->setObjectName("deviceStatusLegend"); label->setStyleSheet("color:"+state.second+";"); legend->addWidget(label);
        }
        side->addLayout(legend);
        auto *find=new QLineEdit; find->setPlaceholderText("搜索设备或分组"); side->addWidget(find);
        auto *selection=new QFrame; selection->setObjectName("clientFilter");
        auto *selectionLayout=new QHBoxLayout(selection); selectionLayout->setContentsMargins(6,2,6,2);
        selectionLayout->addWidget(clientHint("当前选择"));
        selectionName_=clientSelectionLabel();
        selectionLayout->addWidget(selectionName_,1);
        auto *clearSelection=new QPushButton("清空"); clearSelection->setObjectName("clientLink");
        clearSelection->setProperty("clearGroupSelection",true);
        selectionLayout->addWidget(clearSelection); side->addWidget(selection);
        auto *dragTree=new DeviceDragTree; tree_=dragTree;
        tree_->setObjectName("groupTree"); tree_->setHeaderHidden(true);
        tree_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);tree_->header()->setStretchLastSection(false);
        tree_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        tree_->setContextMenuPolicy(Qt::CustomContextMenu); tree_->setFocusPolicy(Qt::NoFocus);
        tree_->setDragEnabled(true); tree_->setAcceptDrops(true);
        tree_->setDragDropMode(QAbstractItemView::InternalMove);
        tree_->setDropIndicatorShown(true);
        dragTree->moved=[this](qint64 id,const QJsonValue &group) {
            api_->post("/device/move",QJsonObject{{"deviceIds",QJsonArray{id}},{"groupId",group}},
                [this](const ApiResponse &result) {
                    if(!result.ok) { window_->notify(result.error,true); return; }
                    window_->notify("设备已移动"); refresh();
                });
        };
        side->addWidget(tree_,1); layout->addWidget(panel);
        list_=new ListPage(api,window,deviceSpec(groups,window->hasPermission("remoteMonitoring"))); layout->addWidget(list_,1);
        list_->layout()->setContentsMargins(0,0,0,0);
        if(groups_) {
            list_->table()->setProperty("editableSort",true);
            list_->table()->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::SelectedClicked);
        }
        list_->addToolbarButton("批量移出分组",[this]{ batchRemove(); });
        list_->addToolbarButton("移动到分组",[this]{ moveChecked(); });
        if(groups_) {
            list_->addToolbarButton("保存排序",[this]{ saveOrder(); });
            list_->addToolbarButton("批量删除设备",[this]{ batchDelete(); });
        }
        list_->addToolbarButton("刷新",[this]{ refresh(); window_->notify("列表已刷新"); });
        list_->setActionHandler([this](ListPage *,const QJsonObject &row,const QString &action) {
            if(action=="监控")window_->navigateFiltered("/device/monitor","deviceId",QString::number(row.value("id").toInteger()));
            else if(action=="上移" || action=="下移") moveDeviceOrder(row,action=="上移" ? -1 : 1);
            else if(action=="编辑") editDevice(row);
            else if(action=="移出") removeDevice(row.value("id").toInteger());
        });
        if(groups_) connect(list_->table(),&QTableWidget::cellClicked,this,[this](int row,int column) {
            if(column==0 || column==list_->table()->columnForKey("sortOrder")) return;
            const QJsonObject device=list_->table()->recordAt(row);
            if(!device.isEmpty() && device.value("groupId").toInteger()<1) quickAssign(device);
        });
        connect(reload,&QPushButton::clicked,this,&DevicePage::reloadTree);
        connect(clearSelection,&QPushButton::clicked,this,[this] {
            tree_->clearSelection();tree_->setCurrentItem(nullptr); setClientSelection(selectionName_); list_->setLocalFilter({});
        });
        connect(find,&QLineEdit::textChanged,this,[this](const QString &text) {
            for(QTreeWidgetItemIterator it(tree_);*it;++it) {
                QTreeWidgetItem *item=*it;
                item->setHidden(!text.isEmpty() && !item->text(0).contains(text,Qt::CaseInsensitive) &&
                    item->childCount()==0);
            }
        });
        connect(tree_,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *item,int){applyTreeSelection(item);});
        connect(tree_,&QTreeWidget::customContextMenuRequested,this,&DevicePage::contextMenu);
    }
    void refresh() override {if(api_->token()=="preview-token"){list_->refresh();return;}reloadTree();loadCatalogs();}
private:
    void applyTreeSelection(QTreeWidgetItem *item,bool resetPage=true) {
            if(!item) return;
            setClientTreeSelection(selectionName_,item);
            const QString kind=item->data(0,Qt::UserRole+1).toString();
            if(kind=="all") { list_->setLocalFilter({},resetPage); return; }
            QSet<qint64> ids;
            std::function<void(QTreeWidgetItem*)> collect=[&](QTreeWidgetItem *node) {
                if(node->data(0,Qt::UserRole+1).toString()=="device")
                    ids.insert(node->data(0,Qt::UserRole).toLongLong());
                for(int i=0;i<node->childCount();++i) collect(node->child(i));
            };
            collect(item);
            list_->setLocalFilter([ids](const QJsonObject &row) {
                return ids.contains(row.value("id").toInteger());
            },resetPage);
    }
    void editGroupDialog(qint64 id,qint64 currentParent,const QString &currentName,bool creating) {
        ClientFormDialog dialog(this,creating ? "新增分组" : "修改分组",500,340);
        FormField name; name.label="分组名称"; name.key="name"; name.required=true; name.maximum=50;
        dialog.addField(name,currentName);
        FormField parent; parent.label="上级分组"; parent.key="parentId";
        parent.kind=FormField::Choice; parent.required=true;
        parent.readonly=!creating;if(!creating || currentParent==0)parent.choices.append({"顶级分组",0});
        for(QTreeWidgetItemIterator it(tree_);*it;++it) {
            QTreeWidgetItem *node=*it;
            if(node->data(0,Qt::UserRole+1).toString()!="group") continue;
            const qint64 candidate=node->data(0,Qt::UserRole).toLongLong();
            if(candidate<1) continue;
            if(creating) {
                bool within=false;for(QTreeWidgetItem *cursor=node;cursor;cursor=cursor->parent())if(cursor->data(0,Qt::UserRole+1)=="group" && cursor->data(0,Qt::UserRole).toLongLong()==currentParent)within=true;
                if(currentParent==0 || !within)continue;
            }
            bool below=false;
            for(QTreeWidgetItem *cursor=node;cursor;cursor=cursor->parent())
                if(cursor->data(0,Qt::UserRole).toLongLong()==id && id>0) { below=true; break; }
            if(below) continue;
            int depth=0; for(QTreeWidgetItem *cursor=node->parent();cursor;cursor=cursor->parent()) ++depth;
            parent.choices.append({QString(depth*2,' ')+node->text(0),candidate});
        }
        dialog.addField(parent,QJsonValue(currentParent));
        const auto values=dialog.execute(); if(!values) return;
        QJsonObject body={{"name",values->value("name")}};
        if(creating)body["parentId"]=values->value("parentId").toInteger()>0 ? values->value("parentId") : QJsonValue(QJsonValue::Null);
        api_->request(creating ? "POST" : "PUT",
            creating ? "/device/group" : "/device/group/"+QString::number(id),body,
            [this](const ApiResponse &result) {
                if(!result.ok) { window_->notify(result.error,true); return; }
                window_->notify("分组已更新"); refresh();
            });
    }
    std::function<void(QComboBox*)> catalogCreate(const QString &endpoint,bool electric) {
        return [this,endpoint,electric](QComboBox *combo) {
            if(!combo) return;
            const auto value=UiMessageBox::prompt(combo->window(),
                electric ? "新增电控类型" : "新增设备类型","请输入类型名称",{},100);
            if(!value || value->trimmed().isEmpty()) return;
            const QPointer<QComboBox> safe(combo);
            api_->post(endpoint,QJsonObject{{"value",value->trimmed()}},
                [this,safe,value,electric](const ApiResponse &result) {
                    if(!result.ok) { window_->notify(result.error,true); return; }
                    if(electric) controls_.append(value->trimmed()); else types_.append(value->trimmed());
                    if(safe) {
                        if(safe->findText(value->trimmed())<0) {
                            safe->addItem(value->trimmed(),value->trimmed());
                            safe->setItemData(safe->count()-1,true,UiI18n::PreserveDataRole);
                        }
                        safe->setCurrentText(value->trimmed());
                    }
                    window_->notify("新增成功");
                });
        };
    }
    void quickAssign(const QJsonObject &row) {
        ClientFormDialog dialog(this,"快速分组设备",560,510);
        auto field=[&](const QString &label,const QString &key,bool required,bool readonly=false) {
            FormField item; item.label=label; item.key=key; item.required=required; item.readonly=readonly;
            dialog.addField(item,row.value(key));
        };
        field("设备编号","code",false,true); field("初始名称","initialName",false,true);
        field("设备名称","name",true);
        FormField remark; remark.label="备注"; remark.key="remark"; remark.kind=FormField::Multiline;
        dialog.addField(remark,row.value("remark"));
        FormField group; group.label="目标分组"; group.key="groupId";
        group.kind=FormField::Choice; group.required=true; group.choices=groupChoices(false);
        dialog.addField(group);
        const auto values=dialog.execute(); if(!values) return;
        const qint64 groupId=values->value("groupId").toInteger();
        if(groupId<1) { window_->notify("请选择目标分组",true); return; }
        const qint64 id=row.value("id").toInteger();
        api_->put("/device/"+QString::number(id),QJsonObject{{"name",values->value("name")},
            {"remark",values->value("remark")}},[this,id,groupId](const ApiResponse &updated) {
            if(!updated.ok) { window_->notify(updated.error,true); return; }
            api_->post("/device/move",QJsonObject{{"deviceIds",QJsonArray{id}},{"groupId",groupId}},
                [this](const ApiResponse &moved) {
                    if(!moved.ok) { window_->notify(moved.error,true); return; }
                    window_->notify("设备分组成功"); refresh();
                });
        });
    }
    void moveDeviceOrder(const QJsonObject &row,int direction) {
        const qint64 groupId=row.value("groupId").toInteger();
        if(groupId<1) return;
        QVector<QJsonObject> siblings;
        for(const QJsonValue &value:list_->rows()) {
            const QJsonObject item=value.toObject();
            if(item.value("groupId").toInteger()==groupId) siblings.append(item);
        }
        std::sort(siblings.begin(),siblings.end(),[](const QJsonObject &a,const QJsonObject &b) {
            const int orderA=a.value("sortOrder").toInt(),orderB=b.value("sortOrder").toInt();
            return orderA==orderB ? a.value("code").toString()<b.value("code").toString() : orderA<orderB;
        });
        int index=-1;
        for(int i=0;i<siblings.size();++i)
            if(siblings[i].value("id").toInteger()==row.value("id").toInteger()) { index=i; break; }
        const int target=index+direction;
        if(index<0 || target<0 || target>=siblings.size()) {
            window_->notify(direction<0 ? "已经是第一个设备" : "已经是最后一个设备",true); return;
        }
        auto orderFor=[&](int at) {
            const int stored=siblings[at].value("sortOrder").toInt();
            return stored>0 ? stored : (at+1)*10;
        };
        const qint64 first=siblings[index].value("id").toInteger();
        const qint64 second=siblings[target].value("id").toInteger();
        const int firstOrder=orderFor(index), secondOrder=orderFor(target);
        api_->put("/device/"+QString::number(first),QJsonObject{{"sortOrder",secondOrder}},
            [this,second,firstOrder](const ApiResponse &firstResult) {
                if(!firstResult.ok) { window_->notify(firstResult.error,true); return; }
                api_->put("/device/"+QString::number(second),QJsonObject{{"sortOrder",firstOrder}},
                    [this](const ApiResponse &secondResult) {
                        if(!secondResult.ok) { window_->notify(secondResult.error,true); return; }
                        window_->notify("设备顺序已更新"); refresh();
                    });
            });
    }
    void addNode(QTreeWidgetItem *parent,const QJsonObject &object) {
        auto *item=parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
        const QString name=object.value("id").toString()=="all" ? "全部分组" :
            object.value("label").toString(object.value("name").toString());
        item->setText(0,name);
        const QString kind=object.value("type").toString()=="device" ? "device" :
            object.value("isVirtual").toBool() ? object.value("id").toString() : "group";
        item->setData(0,Qt::UserRole,object.value("id").toVariant());
        item->setData(0,Qt::UserRole+1,kind);
        item->setData(0,Qt::UserRole+2,object.value(kind=="device" ? "groupId" : "parentId").toVariant());
        Qt::ItemFlags flags=item->flags();
        if(kind=="device") flags=(flags|Qt::ItemIsDragEnabled)&~Qt::ItemIsDropEnabled;
        else flags=(flags|Qt::ItemIsDropEnabled)&~Qt::ItemIsDragEnabled;
        item->setFlags(flags);
        if(clientTreeBusinessLabel(object)) item->setData(0,UiI18n::PreserveDataRole,true);
        item->setIcon(0,kind=="device" ? deviceStateIcon(object.value("status").toString()) : QIcon(":/folder.svg"));
        item->setToolTip(0,name);
        if(kind=="ungrouped") item->setForeground(0,QColor("#f56c6c"));
        for(const QJsonValue &child:object.value("children").toArray()) addNode(item,child.toObject());
    }
    void reloadTree() {
        if(api_->token()=="preview-token") return;
        api_->get("/device/tree",[this](const ApiResponse &result) {
            if(!result.ok) { window_->notify(result.error,true); return; }
            const QVariant oldId=tree_->currentItem() ? tree_->currentItem()->data(0,Qt::UserRole) : QVariant();
            const QVariant oldKind=tree_->currentItem() ? tree_->currentItem()->data(0,Qt::UserRole+1) : QVariant();
            tree_->clear();
            for(const QJsonValue &value:result.data.toArray()) addNode(nullptr,value.toObject());
            tree_->expandAll();
            for(QTreeWidgetItemIterator it(tree_);*it;++it)
                if((*it)->data(0,Qt::UserRole)==oldId && (*it)->data(0,Qt::UserRole+1)==oldKind && oldId.isValid()) {tree_->setCurrentItem(*it);break;}
            if(tree_->currentItem())applyTreeSelection(tree_->currentItem(),false);else {setClientSelection(selectionName_);list_->setLocalFilter({});}
        });
    }
    void loadCatalogs() {
        if(api_->token()=="preview-token") return;
        api_->get("/device/types",[this](const ApiResponse &result){ if(result.ok) types_=result.data.toArray(); });
        api_->get("/device/electric-control-types",[this](const ApiResponse &result){ if(result.ok) controls_=result.data.toArray(); });
    }
    QList<QPair<QString,QJsonValue>> groupChoices(bool includeEmpty=true) const {
        QList<QPair<QString,QJsonValue>> options;
        if(includeEmpty) options.append({"未分组",QJsonValue(QJsonValue::Null)});
        for(QTreeWidgetItemIterator it(tree_);*it;++it)
            if((*it)->data(0,Qt::UserRole+1).toString()=="group") {
                const qint64 id=(*it)->data(0,Qt::UserRole).toLongLong();
                if(id>0) options.append({QString((*it)->parent() ? "  " : "")+(*it)->text(0),id});
            }
        return options;
    }
    void editDevice(const QJsonObject &row) {
        ClientFormDialog dialog(this,"编辑设备",640,620);
        dialog.setConfirmDiscard(true);
        auto add=[&](const QString &label,const QString &key,bool required=false,bool readonly=false) {
            FormField field; field.label=label; field.key=key; field.required=required; field.readonly=readonly;
            if(key=="name") field.maximum=100;
            dialog.addField(field,row.value(key));
        };
        add("设备编号","code",true,true); add("设备名称","name",true);
        add("初始名称","initialName",false,true);
        auto choices=[](const QJsonArray &items,const QString &current) {
            QList<QPair<QString,QJsonValue>> options={{"请选择",QJsonValue("")}};
            bool found=false;
            for(const QJsonValue &value:items) {
                options.append({value.toString(),value});
                if(value.toString()==current) found=true;
            }
            if(!found && !current.isEmpty()) options.append({current,current});
            return options;
        };
        FormField type; type.label="设备类型"; type.key="type"; type.kind=FormField::Choice;
        type.choices=choices(types_,row.value("type").toString());
        type.quickCreate=catalogCreate("/device/type-summary",false);
        dialog.addField(type,row.value("type"));
        FormField control; control.label="电控类型"; control.key="electricControlType"; control.kind=FormField::Choice;
        control.choices=choices(controls_,row.value("electricControlType").toString());
        control.quickCreate=catalogCreate("/device/electric-control-type-summary",true);
        dialog.addField(control,row.value("electricControlType"));
        add("IP地址","ip",false,true); add("员工工号","employeeCode"); add("员工姓名","employeeName");
        add("主板编号","mainboardSn",false,true);
        FormField remark; remark.label="备注"; remark.key="remark"; remark.kind=FormField::Multiline;
        dialog.addField(remark,row.value("remark"));
        FormField group; group.label="所属分组"; group.key="groupId"; group.kind=FormField::Choice;
        group.choices=groupChoices(); dialog.addField(group,row.value("groupId"));
        const auto result=dialog.execute(); if(!result) return;
        QJsonObject body=*result;
        for(const QString &key:{"code","initialName","ip","mainboardSn"}) body.remove(key);
        api_->put("/device/"+QString::number(row.value("id").toInteger()),body,[this](const ApiResponse &response) {
            if(!response.ok) { window_->notify(response.error,true); return; }
            window_->notify("设备已更新"); refresh();
        });
    }
    void removeDevice(qint64 id) {
        if(UiMessageBox::question(this,"移出分组","确定将设备移出分组吗？")!=UiMessageBox::Yes) return;
        api_->remove("/device/"+QString::number(id),{},[this](const ApiResponse &result) {
            if(!result.ok) { window_->notify(result.error,true); return; }
            window_->notify("设备已移出分组"); refresh();
        });
    }
    void batchRemove() {
        const QList<qint64> ids=list_->checkedIds();
        if(ids.isEmpty()) { window_->notify("请先选择设备",true); return; }
        if(UiMessageBox::question(this,"批量移出","确定将所选设备移出分组吗？")!=UiMessageBox::Yes) return;
        QJsonArray items; for(qint64 id:ids) items.append(id);
        api_->remove("/device/batch",QJsonObject{{"ids",items}},[this](const ApiResponse &result) {
            if(!result.ok) { window_->notify(result.error,true); return; }
            window_->notify("设备已移出分组"); refresh();
        });
    }
    void batchDelete() {
        const QList<qint64> ids=list_->checkedIds();
        if(ids.isEmpty()) { window_->notify("请先选择设备",true); return; }
        if(UiMessageBox::question(this,"批量删除设备","确定永久删除所选设备记录吗？")!=UiMessageBox::Yes) return;
        QJsonArray items; for(qint64 id:ids) items.append(id);
        api_->remove("/device/batch/hard",QJsonObject{{"ids",items}},[this](const ApiResponse &result) {
            if(!result.ok) { window_->notify(result.error,true); return; }
            window_->notify("设备已删除"); refresh();
        });
    }
    void moveChecked() {
        const QList<qint64> ids=list_->checkedIds();
        if(ids.isEmpty()) { window_->notify("请先选择设备",true); return; }
        ClientFormDialog dialog(this,"移动到分组",480,270);
        FormField group; group.label="目标分组"; group.key="groupId"; group.kind=FormField::Choice;
        group.choices=groupChoices(); dialog.addField(group);
        const auto result=dialog.execute(); if(!result) return;
        QJsonArray items; for(qint64 id:ids) items.append(id);
        api_->post("/device/move",QJsonObject{{"deviceIds",items},{"groupId",result->value("groupId")}},
            [this](const ApiResponse &response) {
                if(!response.ok) { window_->notify(response.error,true); return; }
                window_->notify("设备已移动"); refresh();
            });
    }
    void saveOrder() {
        const QJsonArray rows=list_->table()->records();
        auto pending=std::make_shared<int>(rows.size());
        auto failures=std::make_shared<QStringList>();
        if(rows.isEmpty()) { window_->notify("暂无数据",true); return; }
        const int sortColumn=list_->table()->columnForKey("sortOrder");
        if(sortColumn<0) return;
        for(int i=0;i<rows.size();++i) {
            const QJsonObject row=rows[i].toObject();
            bool valid=false;
            const int order=list_->table()->item(i,sortColumn)->text().toInt(&valid);
            if(!valid || order<0) { window_->notify("请输入有效的排序数字",true); return; }
            api_->put("/device/"+QString::number(row.value("id").toInteger()),
                QJsonObject{{"sortOrder",order}},[this,pending,failures](const ApiResponse &result) {
                    if(!result.ok) failures->append(result.error);
                    if(--*pending==0) {
                        window_->notify(failures->isEmpty() ? "排序已保存" : failures->first(),
                            !failures->isEmpty());
                        refresh();
                    }
                });
        }
    }
    void contextMenu(const QPoint &point) {
        QTreeWidgetItem *item=tree_->itemAt(point); if(!item) return;
        const QString kind=item->data(0,Qt::UserRole+1).toString();
        const qint64 id=item->data(0,Qt::UserRole).toLongLong();
        const qint64 originalParent=item->data(0,Qt::UserRole+2).toLongLong();const QString originalName=item->text(0);
        QMenu menu(tree_); menu.setObjectName("groupContextMenu");
        QAction *addChild=nullptr,*addSibling=nullptr,*rename=nullptr,*remove=nullptr,*addRoot=nullptr;
        QAction *up=nullptr,*down=nullptr,*moveHere=nullptr,*deleteDevice=nullptr;
        if(kind=="group" || kind=="all" || kind=="ungrouped") {
            if(kind=="all" || kind=="ungrouped")addRoot=menu.addAction("新增分组");
            if(kind=="group")addChild=menu.addAction("新增子分组");
            if(kind=="group") {
                if(!list_->checkedIds().isEmpty()) moveHere=menu.addAction("移动已选设备到当前组");
                if(id>1) {
                    addSibling=menu.addAction("新增同级分组");
                    up=menu.addAction("上移"); down=menu.addAction("下移");
                    rename=menu.addAction("重命名"); remove=menu.addAction("删除分组");
                }
            }
        } else if(kind=="device") {rename=menu.addAction("重命名");if(originalParent>0)remove=menu.addAction("移出分组");deleteDevice=menu.addAction("删除设备");}
        menu.ensurePolished();QPoint position=tree_->viewport()->mapToGlobal(point);QRect region(window_->mapToGlobal(QPoint()),window_->size());if(tree_->screen())region=region.intersected(tree_->screen()->availableGeometry());region.adjust(6,6,-6,-6);const QSize extent=menu.sizeHint();if(region.isValid()){position.setX(qBound(region.left(),position.x(),qMax(region.left(),region.right()-extent.width()+1)));position.setY(qBound(region.top(),position.y(),qMax(region.top(),region.bottom()-extent.height()+1)));}
        // Popup platforms can adjust the menu geometry after calculating sizeHint.
        // Clamp its actual size after showing as well, including near the bottom edge.
        connect(&menu,&QMenu::aboutToShow,&menu,[&menu,region]{QTimer::singleShot(0,&menu,[&menu,region]{if(!region.isValid())return;QPoint at=menu.pos();at.setX(qBound(region.left(),at.x(),qMax(region.left(),region.right()-menu.width()+1)));at.setY(qBound(region.top(),at.y(),qMax(region.top(),region.bottom()-menu.height()+1)));menu.move(at);});});
        QAction *chosen=menu.exec(position);
        if(!chosen) return;
        if(chosen==deleteDevice) {
            if(UiMessageBox::question(this,"删除设备","确定删除该设备吗？")!=UiMessageBox::Yes)return;
            api_->remove("/device/"+QString::number(id)+"/hard",{},[this](const ApiResponse &result){if(!result.ok){window_->notify(result.error,true);return;}window_->notify("设备已删除");refresh();});
        } else if(chosen==addRoot || chosen==addChild || chosen==addSibling) {
            const qint64 parentId=chosen==addSibling ?
                originalParent :
                chosen==addChild && id>0 ? id : 0;
            editGroupDialog(0,parentId,{},true);
        } else if(chosen==moveHere) {
            QJsonArray devices; for(qint64 deviceId:list_->checkedIds()) devices.append(deviceId);
            api_->post("/device/move",QJsonObject{{"deviceIds",devices},{"groupId",id}},
                [this](const ApiResponse &result) {
                    if(!result.ok) { window_->notify(result.error,true); return; }
                    window_->notify("设备已移动"); refresh();
                });
        } else if(chosen==up || chosen==down) {
            item=nullptr;for(QTreeWidgetItemIterator it(tree_);*it;++it)if((*it)->data(0,Qt::UserRole+1).toString()==kind && (*it)->data(0,Qt::UserRole).toLongLong()==id){item=*it;break;}
            if(!item){window_->notify("分组已更新，请重试",true);return;}
            QTreeWidgetItem *parent=item->parent();
            if(!parent) return;
            QList<QTreeWidgetItem*> siblings;
            for(int i=0;i<parent->childCount();++i)
                if(parent->child(i)->data(0,Qt::UserRole+1).toString()=="group")
                    siblings << parent->child(i);
            const int index=siblings.indexOf(item), target=index+(chosen==up ? -1 : 1);
            if(index<0 || target<0 || target>=siblings.size()) return;
            siblings.swapItemsAt(index,target);
            QJsonArray ordering;
            for(int i=0;i<siblings.size();++i)
                ordering.append(QJsonObject{{"id",siblings[i]->data(0,Qt::UserRole).toLongLong()},
                    {"sortOrder",(i+1)*10}});
            api_->post("/group/sort",ordering,[this](const ApiResponse &result) {
                if(!result.ok) { window_->notify(result.error,true); return; }
                window_->notify("分组排序已保存"); reloadTree();
            });
        } else if(chosen==rename) {
            if(kind=="group")
                editGroupDialog(id,originalParent,originalName,false);
            else {
                const auto name=UiMessageBox::prompt(this,"重命名","请输入新名称",originalName,100);
                if(!name) return;
                api_->put("/device/"+QString::number(id),QJsonObject{{"name",*name}},
                    [this](const ApiResponse &result) {
                        if(!result.ok) { window_->notify(result.error,true); return; }
                        window_->notify("名称已修改"); refresh();
                    });
            }
        } else if(chosen==remove) {
            if(kind=="device") removeDevice(id);
            else if(UiMessageBox::question(this,"删除分组","确定删除该分组吗？组内设备将移至未分组设备。")==UiMessageBox::Yes)
                api_->remove("/device/group/"+QString::number(id),{},[this](const ApiResponse &result) {
                    if(!result.ok) { window_->notify(result.error,true); return; }
                    window_->notify("分组已删除"); refresh();
                });
        }
    }
    ApiClient *api_; ClientWindow *window_; bool groups_;
    ListPage *list_; QTreeWidget *tree_; QLabel *selectionName_;
    QJsonArray types_,controls_;
};

ListSpec employeeSpec() {
    ListSpec spec;
    spec.title="员工管理"; spec.description="维护员工档案，支持批量导入、导出与删除。";
    spec.path="/employee/list"; spec.searchHint="员工姓名/备注";
    spec.optionLabel="所属分组"; spec.optionKey="groupId"; spec.options={{"全部分组",""}};
    spec.selection=true; spec.serverPaged=true; spec.dates=true;
    spec.showHeader=false;
    spec.dateWidth=360;
    spec.dateBeforeOption=true;
    spec.extraFilters={{"员工工号","code",160},{"联系电话","phone",160}};
    spec.columns={{"序号","#",65},{"员工工号","code",125},{"员工姓名","name",170},
        {"手机号","phone",160},{"所属分组","groupName",160},{"备注","remark",180},
        {"创建时间","createdAt",180}};
    spec.actions={"编辑","删除"};
    return spec;
}

class EmployeePage : public ListPage {
public:
    EmployeePage(ApiClient *api,ClientWindow *window):ListPage(api,window,employeeSpec()) {
        addToolbarButton("新增员工",[this]{ edit({},true); },true);
        addToolbarButton("下载导入模板",[this]{ templateFile(); });
        addToolbarButton("批量导入",[this]{ importFile(); });
        addToolbarButton("批量删除",[this]{ batchDelete(); });
        addToolbarButton("导出",[this]{ exportFile(); });
        addToolbarButton("刷新",[this]{ refresh(); window_->notify("列表已刷新"); });
        setActionHandler([this](ListPage *,const QJsonObject &row,const QString &action) {
            if(action=="编辑") edit(row,false); else removeEmployee(row.value("id").toInteger());
        });
    }
    void refresh() override {
        ListPage::refresh();
        if(api_->token()=="preview-token") return;
        api_->get("/employee/groups",[this](const ApiResponse &result) {
            if(!result.ok || !option_) return;
            const QString current=option_->currentData().toString();
            option_->clear(); option_->addItem("全部分组",QString());
            groups_=result.data.toArray();
            for(const QJsonValue &value:groups_) {
                const QJsonObject group=value.toObject();
                option_->addItem(group.value("name").toString(),group.value("id").toVariant());
                option_->setItemData(option_->count()-1,true,UiI18n::PreserveDataRole);
            }
            const int index=option_->findData(current);
            if(index>=0) option_->setCurrentIndex(index);
        });
    }
private:
    QList<QPair<QString,QJsonValue>> groupChoices() const {
        QList<QPair<QString,QJsonValue>> choices={{"请选择分组",QJsonValue(QJsonValue::Null)}};
        for(const QJsonValue &value:groups_) {
            const QJsonObject group=value.toObject();
            choices.append({group.value("name").toString(),group.value("id")});
        }
        return choices;
    }
    void edit(const QJsonObject &row,bool creating) {
        ClientFormDialog dialog(this,creating ? "新增员工" : "编辑员工",560,510);
        auto add=[&](const QString &label,const QString &key,bool required,bool readonly=false) {
            FormField field; field.label=label; field.key=key; field.required=required; field.readonly=readonly;
            field.maximum=key=="code" ? 11 : key=="name" ? 8 : key=="phone" ? 20 : 255;
            if(key=="code" && creating) field.inputPattern="^[0-9]{0,11}$";
            dialog.addField(field,row.value(key));
        };
        add("员工工号","code",true,!creating);
        add("员工姓名","name",true); add("手机号","phone",true);
        FormField group; group.label="所属分组"; group.key="groupId"; group.kind=FormField::Choice;
        group.required=true; group.choices=groupChoices(); dialog.addField(group,row.value("groupId"));
        FormField remark; remark.label="备注"; remark.key="remark"; remark.kind=FormField::Multiline;
        dialog.addField(remark,row.value("remark"));
        const auto result=dialog.execute(); if(!result) return;
        QJsonObject body=*result;
        if(!creating) body.remove("code");
        const QString endpoint=creating ? "/employee" : "/employee/"+QString::number(row.value("id").toInteger());
        api_->request(creating ? "POST" : "PUT",endpoint,body,[this](const ApiResponse &response) {
            if(!response.ok) { window_->notify(response.error,true); return; }
            window_->notify("员工资料已保存"); refresh();
        });
    }
    void removeEmployee(qint64 id) {
        if(UiMessageBox::question(this,"删除员工","确定删除该员工吗？")!=UiMessageBox::Yes) return;
        api_->remove("/employee/"+QString::number(id),{},[this](const ApiResponse &response) {
            if(!response.ok) { window_->notify(response.error,true); return; }
            window_->notify("员工已删除"); refresh();
        });
    }
    void batchDelete() {
        const QList<qint64> ids=checkedIds();
        if(ids.isEmpty()) { window_->notify("请先选择员工",true); return; }
        if(UiMessageBox::question(this,"批量删除","确定删除所选员工吗？")!=UiMessageBox::Yes) return;
        auto cursor=std::make_shared<int>(0);
        auto next=std::make_shared<std::function<void()>>();
        *next=[this,ids,cursor,next] {
            if(*cursor>=ids.size()) { window_->notify("员工已删除"); refresh(); return; }
            api_->remove("/employee/"+QString::number(ids[*cursor]),{},
                [this,cursor,next](const ApiResponse &response) {
                    if(!response.ok) { window_->notify(response.error,true); return; }
                    ++*cursor; (*next)();
                });
        };
        (*next)();
    }
    void templateFile() {
        const QString target=ClientFileDialog::getSaveFileName(this,"保存员工导入模板","员工导入模板.csv","CSV (*.csv)");
        if(target.isEmpty()) return;
        QSaveFile file(target);
        if(!file.open(QIODevice::WriteOnly) || file.write("\xEF\xBB\xBF员工工号,员工姓名,手机号,备注\n")<0 ||
           !file.commit()) { window_->notify("保存模板失败",true); return; }
        window_->notify("模板已保存");
    }
    static QList<QStringList> parseRows(const QString &text,bool *valid) {
        const QString first=text.section('\n',0,0);
        const QChar delimiter=first.contains('\t') ? '\t' : first.contains(',') ? ',' : ';';
        QList<QStringList> rows; QStringList row; QString field; bool quoted=false;
        for(int i=0;i<text.size();++i) {
            const QChar ch=text[i];
            if(ch=='"') {
                if(quoted && i+1<text.size() && text[i+1]=='"') { field+='"'; ++i; }
                else quoted=!quoted;
            } else if(!quoted && ch==delimiter) { row << field.trimmed(); field.clear(); }
            else if(!quoted && (ch=='\n' || ch=='\r')) {
                if(ch=='\r' && i+1<text.size() && text[i+1]=='\n') ++i;
                row << field.trimmed(); field.clear();
                if(!row.join("").isEmpty()) rows << row;
                row.clear();
            } else field+=ch;
        }
        *valid=!quoted;
        row << field.trimmed(); if(!row.join("").isEmpty()) rows << row;
        return rows;
    }
    void importFile() {
        ClientFormDialog dialog(this,"批量导入员工",640,580);
        FormField group; group.label="导入所属分组"; group.key="groupId"; group.kind=FormField::Choice;
        group.required=true; group.choices=groupChoices(); dialog.addField(group);
        FormField fileField; fileField.label="导入文件"; fileField.key="file";
        fileField.kind=FormField::File; fileField.maximum=0; dialog.addField(fileField);
        FormField paste; paste.label="粘贴表格内容"; paste.key="importText";
        paste.kind=FormField::Multiline; paste.maximum=0; dialog.addField(paste);
        FormField overwrite; overwrite.label="覆盖已有工号"; overwrite.key="overwriteExisting";
        overwrite.kind=FormField::Toggle; dialog.addField(overwrite,false);
        const auto selection=dialog.execute(); if(!selection) return;
        const qint64 groupId=selection->value("groupId").toInteger();
        if(groupId<1) { window_->notify("请选择导入所属分组",true); return; }
        QString content=selection->value("importText").toString();
        const QString filePath=selection->value("file").toString();
        if(!filePath.isEmpty()) {
            QFile file(filePath);
            if(!file.open(QIODevice::ReadOnly)) { window_->notify("无法读取导入文件",true); return; }
            const QByteArray bytes=file.readAll();
            if(bytes.size()>=2 && ((uchar(bytes[0])==0xff && uchar(bytes[1])==0xfe) ||
                                   (uchar(bytes[0])==0xfe && uchar(bytes[1])==0xff))) {
                QVector<char16_t> units;
                const bool bigEndian=uchar(bytes[0])==0xfe;
                for(int i=2;i+1<bytes.size();i+=2)
                    units.append(bigEndian ? char16_t((uchar(bytes[i])<<8)|uchar(bytes[i+1])) :
                        char16_t(uchar(bytes[i])|(uchar(bytes[i+1])<<8)));
                content=QString::fromUtf16(units.constData(),units.size());
            } else {
                content=QString::fromUtf8(bytes);
                if(content.contains(QChar::ReplacementCharacter)) content=QString::fromLocal8Bit(bytes);
            }
        }
        content.remove(QChar(0xfeff));
        if(content.trimmed().isEmpty()) { window_->notify("请输入导入内容",true); return; }
        bool valid=false;
        const QList<QStringList> parsed=parseRows(content,&valid);
        if(!valid) { window_->notify("CSV 引号未闭合，请检查文件格式",true); return; }
        QJsonArray records;
        QStringList errors;
        for(int i=0;i<parsed.size();++i) {
            const QStringList &fields=parsed[i];
            if(i==0 && !fields.isEmpty() &&
               (fields[0].contains("工号") || fields[0].compare("code",Qt::CaseInsensitive)==0)) continue;
            QString phone=fields.value(2); phone.remove(QRegularExpression("\\s+"));
            if(fields.size()<3 || fields[0].isEmpty() || fields[1].isEmpty() || fields[2].isEmpty() ||
               !QRegularExpression("^[0-9]{1,11}$").match(fields[0]).hasMatch() || fields[1].size()>8 ||
               !QRegularExpression("^1[3-9][0-9]{9}$").match(phone).hasMatch()) {
                errors << QString("第%1行格式错误").arg(i+1); continue;
            }
            QJsonObject employee{{"code",fields[0]},{"name",fields[1]},{"phone",phone}};
            if(fields.size()>3) employee["remark"]=fields[3];
            records.append(employee);
        }
        if(!errors.isEmpty()) { UiMessageBox::output(this,"导入内容存在错误",errors.join('\n')); return; }
        if(records.isEmpty()) { window_->notify("导入文件没有有效员工",true); return; }
        window_->notify("正在导入员工...");
        api_->post("/employee/import",QJsonObject{{"employees",records},{"groupId",groupId},{"overwriteExisting",selection->value("overwriteExisting")}},
            [this](const ApiResponse &response) {
                if(!response.ok) { window_->notify(response.error,true); return; }
                const QJsonObject data=response.data.toObject();
                const QString format="导入成功 %1 条，更新 %2 条，恢复 %3 条";
                const QString summary=(UiI18n::instance() ? UiI18n::instance()->text(format) : format)
                    .arg(data.value("successCount").toInt()).arg(data.value("updatedCount").toInt()).arg(data.value("restoredCount").toInt());
                QStringList errors; for(const QJsonValue &value:data.value("errors").toArray()) errors.append(value.toString());
                if(errors.isEmpty()) window_->notify(summary);
                else UiMessageBox::output(this,"员工导入结果",summary+"\n"+errors.join('\n'));
                refresh();
            });
    }
    void exportFile() {
        QString target=ClientFileDialog::getSaveFileName(this,"导出员工","员工列表.xlsx","Excel (*.xlsx);;CSV (*.csv)");
        if(target.isEmpty()) return;
        if(!target.endsWith(".xlsx",Qt::CaseInsensitive) && !target.endsWith(".csv",Qt::CaseInsensitive)) target+=".xlsx";
        window_->notify("正在导出员工...");
        api_->get("/employee/export"+requestQuery(query()),[this,target](const ApiResponse &response) {
            if(!response.ok) { window_->notify(response.error,true); return; }
            QTemporaryDir temporary;
            if(!temporary.isValid()) { window_->notify("无法创建临时文件",true); return; }
            const bool excel=target.endsWith(".xlsx",Qt::CaseInsensitive);
            const QString csv=excel ? temporary.filePath("employees.csv") : target;
            QSaveFile file(csv);
            if(!file.open(QIODevice::WriteOnly)) { window_->notify("导出失败",true); return; }
            file.write("\xEF\xBB\xBF员工工号,员工姓名,手机号,所属分组,备注,创建时间\n");
            auto quote=[](QString value) { value.replace('"',"\"\""); return "\""+value+"\""; };
            for(const QJsonValue &value:apiRows(response.data)) {
                const QJsonObject row=value.toObject();
                QStringList cells;
                for(const QString &key:{"code","name","phone","groupName","remark","createdAt"})
                    cells << quote(row.value(key).toString());
                file.write((cells.join(',')+'\n').toUtf8());
            }
            if(!file.commit()) { window_->notify("导出失败",true); return; }
            QString error;
            if(excel && !csvToXlsx(csv,target,&error)) { window_->notify(error,true); return; }
            window_->notify("导出成功");
        });
    }
    QJsonArray groups_;
};

}

ClientPage *createManagementPage(const QString &path,ApiClient *api,ClientWindow *window) {
    if(path=="/device/list") return new DevicePage(api,window,false);
    if(path=="/device/group") return new DevicePage(api,window,true);
    if(path=="/employee/list") return new EmployeePage(api,window);
    if(path=="/device/types" || path=="/device/electric-control-types" ||
       path=="/file/pattern-types" || path=="/file/order-nos")
        return new CatalogPage(api,window,path);
    return nullptr;
}
