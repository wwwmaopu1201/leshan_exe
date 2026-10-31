#include "permission_labels.h"
#include "editor_dialog.h"
#include "database.h"
#include "ui_controls.h"
#include "ui_i18n.h"
#include "ui_dialogs.h"

#include <QCheckBox>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <functional>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
class PermissionTreeWidget : public QTreeWidget {
public:
    explicit PermissionTreeWidget(QWidget *parent=nullptr):QTreeWidget(parent) {
        setSelectionMode(QAbstractItemView::NoSelection);
        setFocusPolicy(Qt::NoFocus);
    }
protected:
    void mousePressEvent(QMouseEvent *event) override {
        pressedItem_=event->button()==Qt::LeftButton ? itemAt(event->pos()) : nullptr;
        pressedState_=pressedItem_ ? pressedItem_->checkState(0) : Qt::Unchecked;
        QTreeWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        QTreeWidgetItem *released=event->button()==Qt::LeftButton ? itemAt(event->pos()) : nullptr;
        QTreeWidget::mouseReleaseEvent(event);
        if(released && released==pressedItem_ && released->checkState(0)==pressedState_)
            released->setCheckState(0,pressedState_==Qt::Checked ? Qt::Unchecked : Qt::Checked);
        pressedItem_=nullptr;
    }
private:
    QTreeWidgetItem *pressedItem_=nullptr;
    Qt::CheckState pressedState_=Qt::Unchecked;
};
class EditorModal : public QDialog {
public:
    using QDialog::QDialog;
    std::function<bool()> changed;
    void reject() override {
        if(changed && changed() && UiMessageBox::question(this,"放弃编辑","是否放弃编辑？")!=UiMessageBox::Yes) return;
        QDialog::reject();
    }
};
class EditorUi {
public:
    EditorModal dialog;
    QFormLayout *form;
    std::function<QString()> validate;
    QLabel *validationError;
    explicit EditorUi(const QString &title,int width,QWidget *parent,int height=630,bool scrollable=true):dialog(parent) {
        if(parent) {
            auto *overlay=new QWidget(parent); overlay->setObjectName("dialogOverlay"); overlay->setGeometry(parent->rect());
            overlay->setStyleSheet("background: rgba(0, 0, 0, 112);"); overlay->show(); overlay->raise();
            QObject::connect(&dialog,&QDialog::finished,overlay,&QObject::deleteLater);
        }
        dialog.setObjectName("editorDialog"); dialog.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
        UiModalDialog::attach(&dialog,parent);
        dialog.setWindowTitle(title); dialog.resize(width,height); dialog.setMinimumHeight(qMin(height,440));
        auto *outer=new QVBoxLayout(&dialog); outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);
        auto *header=new QFrame; header->setObjectName("dialogHeader"); header->setFixedHeight(54); auto *headerLayout=new QHBoxLayout(header); headerLayout->setContentsMargins(20,0,16,0);
        auto *heading=new QLabel(title); heading->setObjectName("dialogTitle"); headerLayout->addWidget(heading); headerLayout->addStretch();
        auto *close=new QPushButton("×"); close->setObjectName("dialogClose"); close->setFixedSize(26,26); headerLayout->addWidget(close); outer->addWidget(header);
        close->setAutoDefault(false);close->setFocusPolicy(Qt::NoFocus);
        QObject::connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);
        auto *content=new QWidget; content->setObjectName("dialogContent"); form=new QFormLayout(content); form->setContentsMargins(20,18,20,18); form->setHorizontalSpacing(10); form->setVerticalSpacing(14);
        form->setLabelAlignment(Qt::AlignRight|Qt::AlignVCenter); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        if(scrollable) {
            auto *scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
            scroll->setObjectName("dialogScroll"); scroll->setWidget(content); outer->addWidget(scroll,1);
        } else { outer->addWidget(content,0,Qt::AlignTop); outer->addStretch(1); }
        validationError=new QLabel; validationError->setObjectName("formValidationError"); validationError->setStyleSheet("color:#f56c6c; padding: 4px 20px;"); validationError->setWordWrap(true); validationError->hide(); outer->addWidget(validationError);
        auto *footer=new QFrame; footer->setObjectName("dialogFooter"); auto *footerLayout=new QHBoxLayout(footer); footerLayout->setContentsMargins(18,10,18,10); footerLayout->addStretch();
        auto *cancel=new QPushButton("取消"), *save=new QPushButton("保存"); save->setObjectName("primaryButton"); footerLayout->addWidget(cancel); footerLayout->addWidget(save); outer->addWidget(footer);
        save->setDefault(true);
        QObject::connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
        QObject::connect(save,&QPushButton::clicked,&dialog,[this] {
            const QString error=validate ? validate() : QString();
            validationError->setText(error); validationError->setVisible(!error.isEmpty());
            if(error.isEmpty()) dialog.accept();
        });
    }
    QLineEdit *line(const QString &label,const QString &value={},const QString &hint={},bool readOnly=false) {
        auto *input=new QLineEdit(value); input->setProperty("editorField",label); input->setPlaceholderText(hint); input->setReadOnly(readOnly); form->addRow(label,input); styleField(input);
        QObject::connect(input,&QLineEdit::textChanged,&dialog,[this] { if(validate && validationError->isVisible()) { const auto error=validate(); validationError->setText(error); validationError->setVisible(!error.isEmpty()); } });
        return input;
    }
    QLineEdit *passwordLine(const QString &label,const QString &hint={}) {
        auto *input=line(label,{},hint); input->setEchoMode(QLineEdit::Password);
        auto *action=input->addAction(QIcon(":/eye.svg"),QLineEdit::TrailingPosition);
        QObject::connect(action,&QAction::triggered,input,[input,action] {
            const bool visible=input->echoMode()==QLineEdit::Password;
            input->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
            action->setIcon(QIcon(visible ? ":/eye-off.svg" : ":/eye.svg"));
        });
        return input;
    }
    CounterLine *counterLine(const QString &label,const QString &value,const QString &hint,int maximum) {
        auto *input=new CounterLine(maximum); input->setProperty("editorField",label); input->setText(value); input->setPlaceholderText(hint); form->addRow(label,input); styleField(input); return input;
    }
    QComboBox *combo(const QString &label,const QStringList &names,const QString &selected={},bool editable=false) {
        auto *box=new UiComboBox; box->setEditable(editable); box->addItems(names);
        for(int i=0;i<box->count();++i) box->setItemData(i,true,UiI18n::PreserveDataRole);
        const int at=box->findText(selected); if(at>=0) box->setCurrentIndex(at); else if(editable) box->setEditText(selected); form->addRow(label,box); styleField(box); return box;
    }
    QPlainTextEdit *area(const QString &label,const QString &value={},int height=58) {
        auto *input=new QPlainTextEdit(value); input->setFixedHeight(height); form->addRow(label,input); styleField(input); return input;
    }
    QCheckBox *check(const QString &label,const QString &caption,bool checked) {
        auto *input=new QCheckBox(caption); input->setChecked(checked); form->addRow(label,input); styleField(input); return input;
    }
    UiSwitch *switchControl(const QString &label,const QString &offText,const QString &onText,bool checked) {
        auto *row=new QWidget; row->setFixedHeight(28); auto *layout=new QHBoxLayout(row); layout->setContentsMargins(0,0,0,0); layout->setSpacing(8);
        auto *off=new QLabel(offText), *on=new QLabel(onText);
        auto *control=new UiSwitch;
        control->setChecked(checked); layout->addWidget(off); layout->addWidget(control); layout->addWidget(on); layout->addStretch();
        auto update=[off,on,control] {
            off->setStyleSheet(control->isChecked() ? "color: #303133;" : "color: #409eff; font-weight: 600;");
            on->setStyleSheet(control->isChecked() ? "color: #409eff; font-weight: 600;" : "color: #303133;");
        };
        QObject::connect(control,&UiSwitch::toggled,row,[update](bool){ update(); }); update(); form->addRow(label,row); styleField(row); return control;
    }
    void styleField(QWidget *field) {
        if(auto *label=qobject_cast<QLabel*>(form->labelForField(field))) {
            label->setFixedWidth(UiI18n::instance() && UiI18n::instance()->english() ? 150 : 100);
            label->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
        }
    }
};

QStringList catalogValues(Database *db,const QString &catalog,const QString &table,const QString &field) {
    QSet<QString> values;
    for(const auto &item:db->rows(catalog)) values.insert(item.toObject().value("value").toString());
    for(const auto &item:db->rows(table)) values.insert(item.toObject().value(field).toString());
    values.remove(""); QStringList result=values.values(); result.sort(); result.prepend(""); return result;
}
QComboBox *groupBox(EditorUi &ui,Database *db,const QString &label,const QJsonObject &current) {
    auto *box=new UiComboBox; box->addItem("未分组",QVariant());
    for(const auto &value:db->rows("groups",{}, {},"sort_order,id")) {
        const QJsonObject group=value.toObject(); box->addItem(group.value("name").toString(),group.value("id").toInteger());
        if(group.value("id").toInteger()!=1) box->setItemData(box->count()-1,true,UiI18n::PreserveDataRole);
    }
    const int at=box->findData(current.value("groupId").toInteger()); if(at>=0) box->setCurrentIndex(at);
    ui.form->addRow(label,box); ui.styleField(box); return box;
}
QJsonValue groupValue(QComboBox *box) { return box->currentIndex()==0 ? QJsonValue(QJsonValue::Null) : QJsonValue(box->currentData().toInt()); }
bool validPassword(const QString &value) { return value.size()>=6 && value.size()<=32; }
void required(QFormLayout *form,QWidget *field) {
    if(auto *label=qobject_cast<QLabel*>(form->labelForField(field))) {
        label->setTextFormat(Qt::RichText);
        label->setText("<span style='color:#f56c6c'>* </span>"+label->text());
    }
}
std::optional<QString> createCatalog(Database *db,const QString &table,const QString &title,QWidget *parent) {
    EditorUi ui(title,420,parent,250); auto *value=ui.line("类型名称",{},"请输入类型名称");
    while(ui.dialog.exec()==QDialog::Accepted) {
        const QString name=value->text().trimmed();
        if(name.isEmpty()) { UiMessageBox::warning(&ui.dialog,"检查表单","请输入类型名称"); continue; }
        qint64 id=0; QString error;
        if(!db->insert(table,{{"value",name}},&id,&error)) { UiMessageBox::warning(&ui.dialog,"添加失败",error); continue; }
        return name;
    }
    return std::nullopt;
}
}

std::optional<QJsonObject> showEditorDialog(Database *db,const QString &table,const QJsonObject &old,bool creating,QWidget *parent) {
    if(table=="roles") {
        EditorUi ui(creating ? "新增角色" : "编辑角色",720,parent,580,false);
        auto *name=ui.counterLine("角色名称",old.value("name").toString(),"不超过 10 个字",10);
        required(ui.form,name);
        auto *remark=ui.area("备注",old.value("remark").toString());
        auto *link=ui.switchControl("父子联动","关闭","开启",old.value("parentChildLink").toBool(true));
        auto *toolbar=new QWidget; toolbar->setFixedHeight(28); auto *toolbarLayout=new QHBoxLayout(toolbar); toolbarLayout->setContentsMargins(0,0,0,0);
        auto *detail=new QCheckBox("展开细分权限");
        auto *all=new QPushButton("全选"), *none=new QPushButton("清空");
        toolbarLayout->addWidget(detail); toolbarLayout->addWidget(all); toolbarLayout->addWidget(none); toolbarLayout->addStretch(); ui.form->addRow("菜单权限",toolbar); ui.styleField(toolbar);
        required(ui.form,toolbar);
        auto *tip=new QLabel("当前仅展示一级菜单模块；若需要细分权限，可开启展开细分权限。"); tip->setObjectName("mutedText"); tip->setFixedHeight(20); ui.form->addRow("",tip);
        auto *permissions=new PermissionTreeWidget; permissions->setHeaderHidden(true); permissions->setFixedHeight(190);
        permissions->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        const QJsonObject selected=QJsonDocument::fromJson(old.value("permissions").toString().toUtf8()).object();
        const QList<QPair<QString,QString>> modules={{"home","首页"},{"dashboard","数据看板"},{"deviceManagement","设备管理"},{"fileManagement","文件管理"},{"statistics","数据统计"},{"employeeManagement","员工管理"}};
        QHash<QString,QTreeWidgetItem*> items;
        for(const auto &entry:modules) {
            auto *item=new QTreeWidgetItem(permissions,{entry.second}); item->setData(0,Qt::UserRole,entry.first); item->setFlags(item->flags()|Qt::ItemIsUserCheckable);
            item->setCheckState(0,selected.value(entry.first).toBool(creating) ? Qt::Checked : Qt::Unchecked); items[entry.first]=item;
        }
        const QList<std::tuple<QString,QString,QString>> children={{"deviceManagement","deviceInfo","设备信息"},{"deviceManagement","remoteMonitoring","远程监控"},
            {"fileManagement","patternFiles","花型文件"},{"fileManagement","devicePatternFiles","设备花型文件"},{"fileManagement","downloadLog","下发日志"},
            {"statistics","salaryStatistics","工资统计"},{"statistics","statusStatistics","状态统计"}};
        for(const auto &entry:children) {
            auto *item=new QTreeWidgetItem(items[std::get<0>(entry)],{std::get<2>(entry)}); item->setData(0,Qt::UserRole,std::get<1>(entry)); item->setFlags(item->flags()|Qt::ItemIsUserCheckable);
            item->setCheckState(0,selected.value(std::get<1>(entry)).toBool(creating) ? Qt::Checked : Qt::Unchecked);
        }
        permissions->expandAll(); ui.form->addRow("",permissions);
        for(QTreeWidgetItemIterator it(permissions);*it;++it) if((*it)->parent()) (*it)->setHidden(true);
        QObject::connect(detail,&QCheckBox::toggled,&ui.dialog,[permissions,tip](bool checked){
            for(QTreeWidgetItemIterator it(permissions);*it;++it) if((*it)->parent()) (*it)->setHidden(!checked);
            tip->setText(checked ? "可在下方权限框中滚动查看和勾选细分权限。" : "当前仅展示一级菜单模块；若需要细分权限，可开启展开细分权限。");
            permissions->expandAll();
            if(UiI18n::instance()) UiI18n::instance()->refresh();
        });
        QObject::connect(permissions,&QTreeWidget::itemChanged,&ui.dialog,[permissions,link](QTreeWidgetItem *changed,int){
            if(!link->isChecked()) return;
            QSignalBlocker blocker(permissions);
            if(changed->childCount()>0) {
                for(int i=0;i<changed->childCount();++i) changed->child(i)->setCheckState(0,changed->checkState(0));
            } else if(auto *parent=changed->parent()) {
                bool any=false; for(int i=0;i<parent->childCount();++i) any |= parent->child(i)->checkState(0)==Qt::Checked;
                parent->setCheckState(0,any ? Qt::Checked : Qt::Unchecked);
            }
        });
        QObject::connect(all,&QPushButton::clicked,permissions,[permissions]{ for(QTreeWidgetItemIterator it(permissions);*it;++it) (*it)->setCheckState(0,Qt::Checked); });
        QObject::connect(none,&QPushButton::clicked,permissions,[permissions]{ for(QTreeWidgetItemIterator it(permissions);*it;++it) (*it)->setCheckState(0,Qt::Unchecked); });
        auto snapshot=[=] {
            QJsonObject map;
            for(QTreeWidgetItemIterator it(permissions);*it;++it) map[(*it)->data(0,Qt::UserRole).toString()]=(*it)->checkState(0)==Qt::Checked;
            return QJsonObject{{"name",name->text()},{"remark",remark->toPlainText()},{"link",link->isChecked()},{"permissions",map}};
        };
        const auto initial=snapshot(); ui.dialog.changed=[=] { return snapshot()!=initial; };
        while(ui.dialog.exec()==QDialog::Accepted) {
            if(name->text().trimmed().isEmpty()) { UiMessageBox::warning(&ui.dialog,"检查表单","请输入角色名称"); continue; }
            QJsonObject map;
            for(QTreeWidgetItemIterator it(permissions);*it;++it) map[(*it)->data(0,Qt::UserRole).toString()]=(*it)->checkState(0)==Qt::Checked;
            bool any=false; for(auto it=map.begin();it!=map.end();++it) any |= it.value().toBool();
            if(!any) { UiMessageBox::warning(&ui.dialog,"检查表单","请至少选择一项菜单权限"); continue; }
            return QJsonObject{{"name",name->text().trimmed()},{"remark",remark->toPlainText().trimmed()},
                {"parentChildLink",link->isChecked()},{"permissions",QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact))}};
        }
        return std::nullopt;
    }
    if(table=="users") {
        EditorUi ui(creating ? "新建账号" : "编辑账号",660,parent,650);
        QLineEdit *username=nullptr;
        if(creating) {
            auto *row=new QWidget; auto *layout=new QHBoxLayout(row); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
            username=new QLineEdit; username->setPlaceholderText("请输入账号"); layout->addWidget(username,1);
            auto *generate=new QPushButton("随机生成"); generate->setObjectName("inlineAction"); layout->addWidget(generate); ui.form->addRow("账号",row); ui.styleField(row);
            required(ui.form,row);
            QObject::connect(generate,&QPushButton::clicked,&ui.dialog,[username]{ username->setText(QString::number(QRandomGenerator::global()->bounded(100000,999999))); });
        } else username=ui.line("账号",old.value("username").toString(),"",true);
        auto *password=ui.passwordLine(creating ? "密码" : "新密码(可选)",creating ? "请输入密码" : "不填写则不修改密码");
        if(creating) required(ui.form,password);
        auto *confirm=ui.passwordLine("确认密码");
        if(creating) required(ui.form,confirm);
        ui.form->setRowVisible(confirm,creating);
        QObject::connect(password,&QLineEdit::textChanged,&ui.dialog,[form=ui.form,confirm,creating](const QString &value){ form->setRowVisible(confirm,creating || !value.isEmpty()); });
        auto *nickname=ui.counterLine("账号姓名",old.value("nickname").toString(),"请输入账号姓名",50);
        required(ui.form,nickname);
        QStringList roles; for(const auto &item:db->rows("roles")) roles << item.toObject().value("name").toString();
        auto *role=ui.combo("角色",roles,old.value("role").toString().isEmpty() ? "user" : old.value("role").toString());
        required(ui.form,role);
        auto *email=ui.line("邮箱",old.value("email").toString(),"可选"); auto *phone=ui.line("手机号",old.value("phone").toString(),"请输入手机号");required(ui.form,phone);
        username->setProperty("editorField","账号"); username->setMaxLength(11); email->setMaxLength(100); phone->setMaxLength(20);
        ui.validate=[=] {
            if(!QRegularExpression("^[A-Za-z0-9]{1,11}$").match(username->text().trimmed()).hasMatch()) return QString("账号仅支持字母数字，且不超过11位");
            if((creating || !password->text().isEmpty()) && !validPassword(password->text())) return QString("密码需为 6 至 32 位");
            if((creating || !password->text().isEmpty()) && password->text()!=confirm->text()) return QString("两次输入密码不一致");
            if(nickname->text().trimmed().isEmpty()) return QString("账号姓名不能为空");
            const QString address=email->text().trimmed();
            if(!address.isEmpty() && !QRegularExpression("^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$").match(address).hasMatch()) return QString("邮箱格式不正确");
            QString number=phone->text(); number.remove(QRegularExpression("\\s+"));
            if(!QRegularExpression("^1[3-9][0-9]{9}$").match(number).hasMatch()) return QString("手机号不能为空且格式需正确");
            return QString();
        };
        QObject::connect(username,&QLineEdit::textChanged,&ui.dialog,[&ui] { if(ui.validationError->isVisible()) { const auto error=ui.validate(); ui.validationError->setText(error); ui.validationError->setVisible(!error.isEmpty()); } });
        auto *group=groupBox(ui,db,"所属分组",old);
        auto *visibleGroups=new PermissionTreeWidget; visibleGroups->setObjectName("visibleGroups");
        visibleGroups->setHeaderHidden(true); visibleGroups->setFixedHeight(150);
        const QJsonArray previousGroups=QJsonDocument::fromJson(old.value("groupIds").toString().toUtf8()).array();
        const QJsonArray groupRows=db->rows("groups",{}, {},"sort_order,id");
        QHash<qint64,QTreeWidgetItem*> groupNodes;
        for(const auto &value:groupRows) {
            const QJsonObject groupData=value.toObject(); const qint64 id=groupData.value("id").toInteger();
            auto *entry=new QTreeWidgetItem({groupData.value("name").toString()});
            entry->setData(0,Qt::UserRole,id); entry->setFlags(entry->flags()|Qt::ItemIsUserCheckable);
            if(id!=1) entry->setData(0,UiI18n::PreserveDataRole,true);
            entry->setCheckState(0,previousGroups.contains(QJsonValue(id)) ? Qt::Checked : Qt::Unchecked);
            groupNodes[id]=entry;
        }
        for(const auto &value:groupRows) {
            const QJsonObject groupData=value.toObject(); const qint64 id=groupData.value("id").toInteger();
            const qint64 parentId=groupData.value("parentId").toInteger();
            if(groupNodes.contains(parentId)) groupNodes[parentId]->addChild(groupNodes[id]);
            else visibleGroups->addTopLevelItem(groupNodes[id]);
        }
        visibleGroups->expandAll();
        ui.form->addRow("可见分组",visibleGroups); ui.styleField(visibleGroups);
        auto *enabled=ui.switchControl("账号状态","禁用","启用",!old.value("disabled").toBool());
        auto *permissionPreview=new QWidget; auto *tagLayout=new UiFlowLayout(permissionPreview,6,6);
        ui.form->addRow("角色权限",permissionPreview); ui.styleField(permissionPreview);
        auto updateRoleInfo=[db,role,tagLayout,permissionPreview,group,visibleGroups,form=ui.form] {
            const QString roleName=UiI18n::instance() ? UiI18n::instance()->sourceText(role) : role->currentText();
            const bool admin=roleName=="admin";
            form->setRowVisible(group,!admin); form->setRowVisible(visibleGroups,admin);
            const QJsonArray matches=db->rows("roles","name=?",{roleName});
            const QJsonObject map=matches.isEmpty() ? QJsonObject{} : QJsonDocument::fromJson(matches.first().toObject().value("permissions").toString().toUtf8()).object();
            const QStringList enabled=permissionLabels(map);
            while(auto *item=tagLayout->takeAt(0)) { if(item->widget()) item->widget()->deleteLater(); delete item; }
            if(enabled.isEmpty()) { auto *none=new QLabel("当前角色未配置权限"); none->setObjectName("mutedText"); tagLayout->addWidget(none); }
            else for(int index=0;index<enabled.size();++index) {
                const QString label=UiI18n::instance() ? UiI18n::instance()->text(enabled[index]) : enabled[index];
                auto *tag=new QLabel(label); tag->setObjectName("permissionTag"); tag->setAlignment(Qt::AlignCenter);
                tag->setFixedSize(qMax(42,tag->fontMetrics().horizontalAdvance(label)+12),20);
                tagLayout->addWidget(tag);
            }
            tagLayout->invalidate(); permissionPreview->updateGeometry();
            if(UiI18n::instance()) UiI18n::instance()->refresh();
        };
        QObject::connect(role,&QComboBox::currentTextChanged,&ui.dialog,[updateRoleInfo](const QString&){ updateRoleInfo(); }); updateRoleInfo();
        while(ui.dialog.exec()==QDialog::Accepted) {
            if(username->text().trimmed().isEmpty() || nickname->text().trimmed().isEmpty()) { UiMessageBox::warning(&ui.dialog,"检查表单","账号和账号姓名不能为空"); continue; }
            if((creating || !password->text().isEmpty()) && (!validPassword(password->text()) || password->text()!=confirm->text())) { UiMessageBox::warning(&ui.dialog,"检查表单","密码需为 6 至 32 位，且两次输入一致"); continue; }
            const QString roleName=UiI18n::instance() ? UiI18n::instance()->sourceText(role) : role->currentText();
            const bool admin=roleName=="admin";
            QString number=phone->text(); number.remove(QRegularExpression("\\s+"));
            QJsonObject body{{"username",username->text().trimmed()},{"nickname",nickname->text().trimmed()},{"role",roleName},
                {"email",email->text().trimmed()},{"phone",number},{"groupId",admin ? QJsonValue(QJsonValue::Null) : groupValue(group)},{"disabled",!enabled->isChecked()}};
            QJsonArray selectedGroups; if(admin) for(QTreeWidgetItemIterator it(visibleGroups);*it;++it)
                if((*it)->checkState(0)==Qt::Checked) selectedGroups.append((*it)->data(0,Qt::UserRole).toInt());
            body["groupIds"]=QString::fromUtf8(QJsonDocument(selectedGroups).toJson(QJsonDocument::Compact));
            if(creating || !password->text().isEmpty()) body["password"]=password->text();
            const QJsonArray roles=db->rows("roles","name=?",{roleName});
            if(!roles.isEmpty()) body["permissions"]=roles.first().toObject().value("permissions");
            return body;
        }
        return std::nullopt;
    }
    if(table=="devices") {
        EditorUi ui(creating ? "新建设备" : "编辑设备",620,parent,710);
        auto *code=ui.line("设备编号",old.value("code").toString(),"设备编号",!creating);
        auto *name=ui.counterLine("设备名称",old.value("name").toString(),"设备名称",100);
        required(ui.form,name);
        auto *initial=ui.line("初始名称",old.value("initialName").toString(),{},!creating);
        auto catalogRow=[&](const QString &label,const QString &catalog,const QString &field,const QString &current,const QString &dialogTitle) {
            auto *row=new QWidget; auto *layout=new QHBoxLayout(row); layout->setContentsMargins(0,0,0,0); layout->setSpacing(8);
            auto *combo=new UiComboBox; combo->setEditable(true); combo->addItems(catalogValues(db,catalog,"devices",field));
            for(int i=0;i<combo->count();++i) combo->setItemData(i,true,UiI18n::PreserveDataRole);
            combo->setCurrentText(current); combo->setPlaceholderText("请选择"+label); layout->addWidget(combo,1);
            auto *add=new QPushButton("＋ 添加类型"); layout->addWidget(add); ui.form->addRow(label,row); ui.styleField(row);
            QObject::connect(add,&QPushButton::clicked,&ui.dialog,[db,combo,catalog,dialogTitle,&ui]{
                const auto value=createCatalog(db,catalog,dialogTitle,&ui.dialog);
                if(value) { combo->addItem(*value); combo->setItemData(combo->count()-1,true,UiI18n::PreserveDataRole); combo->setCurrentText(*value); }
            });
            return combo;
        };
        auto *type=catalogRow("设备类型","device_type_catalogs","type",old.value("type").toString(),"新增设备类型");
        auto *electric=catalogRow("电控类型","electric_control_type_catalogs","electricControlType",old.value("electricControlType").toString(),"新增电控类型");
        auto *ip=ui.line("IP 地址",old.value("ip").toString(),"192.168.1.xxx",!creating);
        auto *employeeCode=ui.line("员工工号",old.value("employeeCode").toString());
        auto *employeeName=ui.line("员工姓名",old.value("employeeName").toString());
        ui.line("主板编号",old.value("mainboardSn").toString(),{},true);
        auto *remark=ui.area("备注",old.value("remark").toString(),80);
        auto *group=groupBox(ui,db,"设备分组",old);
        ui.validate=[=] {
            if(code->text().trimmed().isEmpty() || name->text().trimmed().isEmpty()) return QString("设备编号和名称不能为空");
            return QString();
        };
        while(ui.dialog.exec()==QDialog::Accepted) {
            const QString typeValue=UiI18n::instance() ? UiI18n::instance()->sourceText(type).trimmed() : type->currentText().trimmed();
            const QString electricValue=UiI18n::instance() ? UiI18n::instance()->sourceText(electric).trimmed() : electric->currentText().trimmed();
            if(code->text().trimmed().isEmpty() || name->text().trimmed().isEmpty()) { UiMessageBox::warning(&ui.dialog,"检查表单","设备编号和名称不能为空"); continue; }
            QJsonObject body{{"name",name->text().trimmed()},{"type",typeValue},
                {"electricControlType",electricValue},{"employeeCode",employeeCode->text().trimmed()},
                {"employeeName",employeeName->text().trimmed()},{"remark",remark->toPlainText().trimmed()},{"groupId",groupValue(group)}};
            if(creating) { body["code"]=code->text().trimmed(); body["initialName"]=initial->text().trimmed(); body["ip"]=ip->text().trimmed(); }
            return body;
        }
        return std::nullopt;
    }
    EditorUi ui(table=="groups" ? (creating ? "新增分组" : "编辑分组") : table=="employees" ? (creating ? "新增员工" : "编辑员工") : (creating ? "新建记录" : "编辑记录"),table=="groups" ? 500 : 560,parent,table=="groups" ? 360 : 560);
    QStringList fields,labels;
    if(table=="operators") { fields={"username","password","nickname","groupId","disabled"}; labels={"账号","密码","姓名","所属分组","限制登录"}; }
    else if(table=="groups") { fields={"name","parentId","sortOrder"}; labels={"分组名称","上级分组","排序"}; }
    else if(table=="employees") { fields={"code","name","department","position","phone","groupId","remark"}; labels={"工号","姓名","部门","岗位","手机号","所属分组","备注"}; }
    else if(table=="patterns") { fields={"name","fileName","patternType","filePath","fileSize","stitches","unitPrice","orderNo"}; labels={"名称","文件名","类型","路径","大小","针数","工价","订单号"}; }
    QHash<QString,QLineEdit*> lines; QComboBox *group=nullptr, *parentGroup=nullptr; QCheckBox *disabled=nullptr;
    for(int i=0;i<fields.size();++i) {
        const QString key=fields[i];
        if(key=="groupId") { group=groupBox(ui,db,labels[i],old); continue; }
        if(key=="parentId") {
            parentGroup=new UiComboBox;
            parentGroup->setProperty("editorField","parentId");
            parentGroup->addItem("顶级分组",QVariant());
            for(const auto &value:db->rows("groups",{}, {},"sort_order,id")) {
                const QJsonObject item=value.toObject();
                if(creating)continue; // Context child creation already fixes its parent; this dialog creates roots.
                if(item.value("id").toInteger()==old.value("id").toInteger() && !creating) continue;
                parentGroup->addItem(item.value("name").toString(),item.value("id").toInteger());
                if(item.value("id").toInteger()!=1) parentGroup->setItemData(parentGroup->count()-1,true,UiI18n::PreserveDataRole);
            }
            const qint64 parentId=old.value("parentId").toInteger();
            if(creating) parentGroup->setCurrentIndex(0);
            else { const int at=parentGroup->findData(parentId); if(at>=0) parentGroup->setCurrentIndex(at); parentGroup->setEnabled(false); }
            if(old.value("id").toInteger()==1 && !creating) parentGroup->setEnabled(false);
            ui.form->addRow(labels[i],parentGroup); ui.styleField(parentGroup); continue;
        }
        if(key=="disabled") { disabled=ui.check(labels[i],"启用限制",old.value(key).toBool()); continue; }
        auto *input=ui.line(labels[i],old.value(key).toVariant().toString());
        if(key=="password") { input->clear(); input->setEchoMode(QLineEdit::Password); input->setPlaceholderText(creating ? "请输入密码" : "留空则不修改"); }
        lines[key]=input;
        if(table=="groups" && key=="name"){input->setMaxLength(50);required(ui.form,input);}
        if(table=="employees") {
            if(key=="code") { input->setReadOnly(!creating); input->setMaxLength(11); input->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{0,11}"),input)); required(ui.form,input); }
            if(key=="name") { input->setMaxLength(8); required(ui.form,input); }
            if(key=="phone") { input->setMaxLength(20); required(ui.form,input); }
        }
    }
    if(table=="employees") ui.validate=[=] {
        if(!QRegularExpression("^[0-9]{1,11}$").match(lines.value("code")->text()).hasMatch()) return QString("工号仅支持1至11位数字");
        if(lines.value("name")->text().trimmed().isEmpty()) return QString("姓名不能为空");
        QString phone=lines.value("phone")->text(); phone.remove(QRegularExpression("\\s+"));
        if(!QRegularExpression("^1[3-9][0-9]{9}$").match(phone).hasMatch()) return QString("手机号不能为空且格式需正确");
        if(!db->employeePhoneAvailable(phone,old.value("id").toInteger()))return QString("手机号已被其他员工使用");
        return QString();
    };
    if(table=="groups")ui.validate=[=]{return lines.value("name")->text().trimmed().isEmpty() ? QString("分组名称不能为空") : QString();};
    while(ui.dialog.exec()==QDialog::Accepted) {
        QJsonObject body;
        for(const QString &key:fields) {
            if(key=="groupId") body[key]=groupValue(group);
            else if(key=="parentId") body[key]=parentGroup->currentIndex()==0 ? QJsonValue(QJsonValue::Null) : QJsonValue(parentGroup->currentData().toInt());
            else if(key=="disabled") body[key]=disabled->isChecked();
            else if(key=="password" && !creating && lines[key]->text().isEmpty()) continue;
            else if(key=="sortOrder" || key=="fileSize" || key=="stitches") body[key]=lines[key]->text().toInt();
            else if(key=="unitPrice") body[key]=lines[key]->text().toDouble();
            else body[key]=lines[key]->text().trimmed();
        }
        if((table=="groups" || table=="employees" || table=="operators") && body.value("name").toString().isEmpty() && body.value("nickname").toString().isEmpty()) { UiMessageBox::warning(&ui.dialog,"检查表单","名称不能为空"); continue; }
        if(table=="employees") { QString phone=body.value("phone").toString(); phone.remove(QRegularExpression("\\s+")); body["phone"]=phone; }
        return body;
    }
    return std::nullopt;
}

std::optional<QJsonValue> showMoveGroupDialog(Database *db,int selectedCount,QWidget *parent) {
    EditorUi ui("批量移动分组",420,parent,250);
    auto *box=groupBox(ui,db,"目标分组",{});
    auto *hint=new QLabel(QString("已选择 %1 条记录").arg(selectedCount)); hint->setObjectName("mutedText"); ui.form->addRow("",hint);
    if(ui.dialog.exec()!=QDialog::Accepted) return std::nullopt;
    return groupValue(box);
}
