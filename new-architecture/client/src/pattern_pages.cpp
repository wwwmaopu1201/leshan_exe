#include "pattern_pages.h"
#include "direct_pattern_dialog.h"
#include "api_client.h"
#include "client_window.h"
#include "client_file_dialog.h"
#include "form_dialog.h"
#include "list_page.h"
#include "page_widgets.h"
#include "ui_controls.h"
#include "ui_dialogs.h"
#include "ui_i18n.h"

#include <QDialog>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPointer>
#include <QProgressDialog>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QTableWidget>
#include <QTimer>
#include <QTime>
#include <QTreeWidget>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>
#include <memory>
#include <functional>

namespace {
struct PatternUploadItem {
    QString path,name,type,order;
    int stitches=0;
    double price=0;
};
class PatternDropArea : public QFrame {
public:
    std::function<void(const QStringList&)> filesDropped;
    PatternDropArea() { setAcceptDrops(true); setObjectName("clientPatternDrop"); }
protected:
    void dragEnterEvent(QDragEnterEvent *event) override {
        if(event->mimeData()->hasUrls()) event->acceptProposedAction();
    }
    void dropEvent(QDropEvent *event) override {
        QStringList paths;
        for(const QUrl &url:event->mimeData()->urls()) if(url.isLocalFile()) paths << url.toLocalFile();
        if(!paths.isEmpty() && filesDropped) filesDropped(paths);
        event->acceptProposedAction();
    }
};
class PatternUploadDialog : public QDialog {
public:
    PatternUploadDialog(QWidget *parent,ApiClient *api,ClientWindow *window,
                        const QJsonArray &types,const QJsonArray &orders)
        :QDialog(parent),api_(api),window_(window) {
        setObjectName("editorDialog"); setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
        UiModalDialog::attach(this,parent);
        resize(850,710); setMinimumSize(760,620);
        auto *outer=new QVBoxLayout(this); outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);
        auto *header=new QFrame; header->setObjectName("dialogHeader");
        auto *heading=new QHBoxLayout(header); heading->setContentsMargins(20,0,16,0);
        heading->addWidget(clientHeading("添加文件")); heading->addStretch();
        auto *close=new QPushButton("×"); close->setObjectName("dialogClose");
        close->setAutoDefault(false);close->setFocusPolicy(Qt::NoFocus);
        heading->addWidget(close); connect(close,&QPushButton::clicked,this,&QDialog::reject);
        outer->addWidget(header);
        auto *body=new QWidget; auto *layout=new QVBoxLayout(body);
        layout->setContentsMargins(22,14,22,14); layout->setSpacing(10);
        mode_=new UiComboBox;mode_->setObjectName("patternUploadMode");
        mode_->addItem("上传到服务器",false);mode_->addItem("直接下发到机器（不留存）",true);
        auto *modeRow=new QHBoxLayout;modeRow->addWidget(new QLabel("上传方式"));modeRow->addWidget(mode_,1);layout->addLayout(modeRow);
        auto *modeHint=clientHint("上传到服务器花型库，保留文件与记录。");layout->addWidget(modeHint);
        auto *fields=new QGridLayout; fields->setHorizontalSpacing(9); fields->setVerticalSpacing(8);
        name_=new QLineEdit; name_->setPlaceholderText("请输入花型名称");
        name_->setMaxLength(255);
        type_=new UiComboBox; type_->setEditable(true); type_->addItem("请选择花型类型",QString());
        for(const QJsonValue &value:types) if(!value.toString().isEmpty()) {
            type_->addItem(value.toString(),value.toString());
            type_->setItemData(type_->count()-1,true,UiI18n::PreserveDataRole);
        }
        order_=new UiComboBox; order_->setEditable(true); order_->addItem("请选择订单编号",QString());
        type_->lineEdit()->setMaxLength(100); order_->lineEdit()->setMaxLength(100);
        order_->lineEdit()->setValidator(new QRegularExpressionValidator(QRegularExpression("^[A-Za-z0-9]{0,100}$"),order_));
        for(const QJsonValue &value:orders) {
            const QString text=value.isObject() ? value.toObject().value("value").toString() : value.toString();
            if(!text.isEmpty()) {
                order_->addItem(text,text);
                order_->setItemData(order_->count()-1,true,UiI18n::PreserveDataRole);
            }
        }
        stitches_=new NumericStepper(0); stitches_->setRange(0,2147483647);
        price_=new NumericStepper(3); price_->setRange(0,999999999.999);
        auto addCatalog=[this](UiComboBox *combo,const QString &endpoint,const QString &title) {
            auto *row=new QWidget; auto *line=new QHBoxLayout(row);
            line->setContentsMargins(0,0,0,0); line->setSpacing(6); line->addWidget(combo,1);
            auto *button=new QPushButton("新增"); line->addWidget(button);
            connect(button,&QPushButton::clicked,this,[this,combo,endpoint,title] {
                const auto value=UiMessageBox::prompt(this,title,"请输入名称",{},100,endpoint.endsWith("order-summary"));
                if(!value || value->trimmed().isEmpty()) return;
                const QPointer<PatternUploadDialog> safe(this);
                api_->post(endpoint,QJsonObject{{"value",value->trimmed()}},
                    [safe,combo,value](const ApiResponse &result) {
                        if(!safe) return;
                        if(!result.ok) { safe->window_->notify(result.error,true); return; }
                        if(combo->findText(value->trimmed())<0) {
                            combo->addItem(value->trimmed(),value->trimmed());
                            combo->setItemData(combo->count()-1,true,UiI18n::PreserveDataRole);
                        }
                        combo->setCurrentText(value->trimmed());
                        safe->window_->notify("新增成功");
                    });
            });
            return row;
        };
        fields->addWidget(new QLabel("花型名称"),0,0); fields->addWidget(name_,0,1);
        fields->addWidget(new QLabel("花型类型"),0,2);
        fields->addWidget(addCatalog(type_,"/pattern/type-summary","新增花型类型"),0,3);
        fields->addWidget(new QLabel("花型针数"),1,0); fields->addWidget(stitches_,1,1);
        fields->addWidget(new QLabel("工价"),1,2); fields->addWidget(price_,1,3);
        fields->addWidget(new QLabel("订单编号"),2,0);
        fields->addWidget(addCatalog(order_,"/pattern/order-summary","新增订单编号"),2,1,1,3);
        fields->setColumnStretch(1,1); fields->setColumnStretch(3,1);
        layout->addLayout(fields);
        nameError_=new QLabel; nameError_->setObjectName("promptError"); nameError_->hide(); layout->addWidget(nameError_);
        connect(name_,&QLineEdit::textChanged,this,[this] {
            const bool invalid=active_>=0 && name_->text().trimmed().isEmpty();
            nameError_->setText(invalid ? "花型名称不能为空" : QString()); nameError_->setVisible(invalid);
        });
        auto *drop=new PatternDropArea; drop->setMinimumHeight(132);
        auto *dropLayout=new QVBoxLayout(drop);
        dropLayout->setContentsMargins(12,10,12,10); dropLayout->setSpacing(7);
        auto *dropLabel=clientHint("将花型文件拖到此处，或点击选择多个文件");
        dropLabel->setAlignment(Qt::AlignCenter); dropLayout->addWidget(dropLabel);
        auto *choose=new QPushButton("选择文件"); choose->setObjectName("primaryButton");
        dropLayout->addWidget(choose,0,Qt::AlignCenter);
        auto *extensions=clientHint("支持 .SLW、.XDG、.NSP、.NTP、.MTP、.SDT、.VDT 格式");
        extensions->setWordWrap(false); extensions->setMinimumHeight(22);
        dropLayout->addWidget(extensions,0,Qt::AlignCenter);
        drop->filesDropped=[this](const QStringList &paths){ addFiles(paths); };
        connect(choose,&QPushButton::clicked,this,[this] {
            addFiles(ClientFileDialog::getOpenFileNames(this,"选择花型文件",{},
                "Pattern Files (*.slw *.SLW *.xdg *.XDG *.nsp *.NSP *.ntp *.NTP *.mtp *.MTP *.sdt *.SDT *.vdt *.VDT)"));
        });
        layout->addWidget(drop);
        layout->addWidget(clientHeading("文件列表（点击文件行可编辑对应花型信息）"));
        table_=new QTableWidget; table_->setObjectName("clientTable");
        table_->setColumnCount(7); table_->setHorizontalHeaderLabels({"文件名","花型名称","花型类型","针数","工价","订单编号","操作"});
        table_->verticalHeader()->hide(); table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        for(int i=2;i<7;++i) table_->setColumnWidth(i,i==5 ? 125 : 85);
        table_->setMinimumHeight(205); layout->addWidget(table_,1);
        connect(table_,&QTableWidget::currentCellChanged,this,[this](int currentRow,int,int,int) {
            saveActive(); active_=currentRow; loadActive();
        });
        auto *bodyScroll=new QScrollArea;bodyScroll->setObjectName("patternUploadBodyScroll");bodyScroll->setFrameShape(QFrame::NoFrame);bodyScroll->setWidgetResizable(true);bodyScroll->setWidget(body);outer->addWidget(bodyScroll,1);
        auto *footer=new QFrame; footer->setObjectName("dialogFooter");
        auto *buttons=new QHBoxLayout(footer); buttons->addStretch();
        auto *cancel=new QPushButton("取消"), *submit=new QPushButton("开始上传");
        submit->setObjectName("primaryButton"); buttons->addWidget(cancel); buttons->addWidget(submit);
        connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
        connect(submit,&QPushButton::clicked,this,[this] {
            saveActive();
            if(items_.isEmpty()) { window_->notify("请先选择花型文件",true); return; }
            for(const PatternUploadItem &item:items_)
                if(item.name.trimmed().isEmpty() || !QFileInfo::exists(item.path)) {
                    if(item.name.trimmed().isEmpty()) { nameError_->setText("花型名称不能为空"); nameError_->show(); name_->setFocus(); return; }
                    window_->notify("请检查文件和花型名称",true); return;
                }
            if(directMode())for(const auto &item:items_) {
                if(item.name.size()>22){window_->notify("直接下发的花型名称不能超过22个字符",true);return;}
                if(QFileInfo(item.path).size()>255*65536){window_->notify("花型文件超过设备协议大小限制",true);return;}
            }
            accept();
        });
        connect(mode_,&QComboBox::currentIndexChanged,this,[this,submit,modeHint] {
            const bool direct=directMode();
            type_->parentWidget()->setEnabled(!direct);order_->parentWidget()->setEnabled(!direct);stitches_->setEnabled(!direct);price_->setEnabled(!direct);
            for(int col=2;col<=5;++col)table_->setColumnHidden(col,direct);
            auto text=[](const QString &source){return UiI18n::instance() ? UiI18n::instance()->text(source) : source;};
            submit->setText(text(direct ? "开始下发" : "开始上传"));
            modeHint->setText(text(direct ? "仅发送到目标机器，不保存服务器花型文件、花型库记录和下发日志。" : "上传到服务器花型库，保留文件与记录。"));
        });
        outer->addWidget(footer);
        submit->setDefault(true);
    }
    bool directMode() const {return mode_->currentData().toBool();}
    QList<PatternUploadItem> items() const { return items_; }
private:
    void addFiles(const QStringList &paths) {
        static const QSet<QString> allowed={"slw","xdg","nsp","ntp","mtp","sdt","vdt"};
        saveActive();
        int added=0;
        for(const QString &path:paths) {
            const QFileInfo file(path);
            if(!file.isFile() || !allowed.contains(file.suffix().toLower())) continue;
            bool existing=false;
            for(const PatternUploadItem &item:items_) if(item.path==file.absoluteFilePath()) { existing=true; break; }
            if(existing) continue;
            PatternUploadItem item; item.path=file.absoluteFilePath(); item.name=file.completeBaseName();
            items_.append(item); ++added;
        }
        if(added==0 && !paths.isEmpty()) window_->notify("没有可添加的花型文件",true);
        renderFiles();
        if(active_<0 && !items_.isEmpty()) table_->setCurrentCell(0,0);
    }
    void renderFiles() {
        QSignalBlocker blocked(table_); table_->setRowCount(items_.size());
        for(int row=0;row<items_.size();++row) {
            const PatternUploadItem &item=items_[row];
            const QStringList values={QFileInfo(item.path).fileName(),item.name,item.type,
                QString::number(item.stitches),QString::number(item.price,'f',3),item.order};
            for(int column=0;column<values.size();++column) {
                auto *cell=new QTableWidgetItem(values[column]); cell->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);
                cell->setData(UiI18n::PreserveDataRole,true);
                table_->setItem(row,column,cell);
            }
            auto *remove=new QPushButton("删除"); remove->setObjectName("clientDangerLink");
            table_->setCellWidget(row,6,remove);
            connect(remove,&QPushButton::clicked,this,[this,row] {
                saveActive(); items_.removeAt(row); active_=-1; renderFiles();
                if(!items_.isEmpty()) table_->setCurrentCell(qMin(row,items_.size()-1),0);
                else { name_->clear(); type_->setCurrentIndex(0); order_->setCurrentIndex(0);
                    stitches_->setValue(0); price_->setValue(0); }
            });
        }
    }
    void saveActive() {
        if(active_<0 || active_>=items_.size()) return;
        PatternUploadItem &item=items_[active_];
        item.name=name_->text().trimmed(); item.type=editableChoiceText(type_);
        item.order=editableChoiceText(order_); item.stitches=int(stitches_->value()); item.price=price_->value();
        renderFiles();
    }
    void loadActive() {
        if(active_<0 || active_>=items_.size()) return;
        const PatternUploadItem &item=items_[active_];
        name_->setText(item.name); type_->setCurrentText(item.type);
        order_->setCurrentText(item.order); stitches_->setValue(item.stitches); price_->setValue(item.price);
    }
    ApiClient *api_; ClientWindow *window_;
    QList<PatternUploadItem> items_;
    int active_=-1;
    QTableWidget *table_;
    QLineEdit *name_;
    QLabel *nameError_;
    UiComboBox *type_,*order_,*mode_;
    NumericStepper *stitches_,*price_;
};
class PatternCheckTree : public QTreeWidget {
protected:
    void mousePressEvent(QMouseEvent *event) override {
        pressed_=itemAt(event->pos()); previous_=pressed_ ? pressed_->checkState(0) : Qt::Unchecked;
        QTreeWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        QTreeWidget::mouseReleaseEvent(event);
        auto *item=itemAt(event->pos());
        if(item && item==pressed_ && item->checkState(0)==previous_)
            item->setCheckState(0,previous_==Qt::Checked ? Qt::Unchecked : Qt::Checked);
        clearSelection(); clearFocus(); pressed_=nullptr;
    }
private:
    QTreeWidgetItem *pressed_=nullptr; Qt::CheckState previous_=Qt::Unchecked;
};
ListSpec queueSpec(bool history) {
    ListSpec spec;
    spec.title=history ? "下发日志" : "下发队列";
    spec.description=history ? "查看花型下发结果与历史记录。" :
        "同一设备按队列顺序下发；缝纫中或关机时任务会等待。";
    spec.path=history ? "/pattern/log" : "/pattern/queue";
    spec.searchHint="花型名称"; spec.searchKey="patternName";
    spec.optionLabel="状态"; spec.optionKey="status";
    spec.options=history ? QList<QPair<QString,QString>>{{"全部状态",""},{"已完成","completed"},
        {"失败","failed"},{"已取消","canceled"}} :
        QList<QPair<QString,QString>>{{"全部状态",""},{"等待中","waiting"},{"下发中","downloading"},
        {"已暂停","paused"},{"已完成","completed"},{"失败","failed"}};
    spec.serverPaged=history; spec.dates=history; spec.refreshSeconds=history ? 0 : 5;
    spec.showHeader=false; spec.showFilters=history;
    if(history) {
        spec.secondaryLabel="订单编号"; spec.secondaryKey="orderNo";
        spec.extraFilters={{"花型类型","patternType",145},{"目标设备","deviceName",150}};
        spec.dateWidth=360;
        spec.columns={{"序号","#",60},{"花型文件","patternName",180},{"花型类型","patternType",120},
            {"针数","stitches",90},{"文件大小","size",100},{"下发设备","deviceName",145},
            {"工价","unitPrice",95},{"订单号","orderNo",120},{"结果","status",105},
            {"备注","message",180},{"下发时间","createdAt",180}};
    } else spec.columns={{"序号","#",60},{"花型文件","patternName",190},{"花型类型","patternType",130},
        {"订单号","orderNo",130},{"目标设备","deviceName",165},{"状态","status",110},
        {"进度","progress",130},{"备注","message",180},{"创建时间","createdAt",180}};
    if(!history) spec.actions={"暂停","继续","取消"};
    return spec;
}

class DownloadQueuePage : public ListPage {
public:
    DownloadQueuePage(ApiClient *api,ClientWindow *window,bool history)
        :ListPage(api,window,queueSpec(history)),history_(history) {
        if(history_) addToolbarButton("导出",[this]{ exportLog(); });
        else {
            createHero();
            setActionHandler([this](ListPage *,const QJsonObject &row,const QString &action) {
                const QString status=row.value("status").toString();
                if(action=="暂停" && status!="waiting" && status!="downloading") return;
                if(action=="继续" && status!="paused" && status!="failed") return;
                if(action=="取消" && QStringList{"completed","canceled"}.contains(status)) return;
                const QString endpoint="/pattern/queue/"+QString::number(row.value("id").toInteger());
                if(action=="取消") {
                    if(UiMessageBox::question(this,"取消任务","确定取消下发任务吗？")!=UiMessageBox::Yes) return;
                    api_->remove(endpoint,{},[this](const ApiResponse &result){ changed(result); });
                } else api_->post(endpoint+(action=="暂停" ? "/pause" : "/resume"),QJsonObject{},
                                  [this](const ApiResponse &result){ changed(result); });
            });
        }
        if(history_) addToolbarButton("刷新",[this]{ refresh(); window_->notify("列表已刷新"); });
    }
private:
    void createHero() {
        auto *hero=clientCard(); hero->setFixedHeight(242);
        auto *layout=new QVBoxLayout(hero); layout->setContentsMargins(12,9,12,10); layout->setSpacing(10);
        auto *actions=new QHBoxLayout; actions->addStretch();
        auto add=[this,actions](const QString &label,std::function<void()> callback) {
            auto *button=new QPushButton(label); actions->addWidget(button);
            connect(button,&QPushButton::clicked,this,std::move(callback)); return button;
        };
        add("刷新",[this]{ refresh(); window_->notify("列表已刷新"); });
        add("全部暂停",[this]{ callAll("/pattern/queue/pause-all","所有等待任务已暂停"); });
        add("全部继续",[this]{ callAll("/pattern/queue/resume-all","所有暂停任务已继续"); });
        add("清除已完成",[this] {
            if(UiMessageBox::question(this,"清除已完成","确定清除已完成任务吗？")!=UiMessageBox::Yes) return;
            api_->remove("/pattern/queue/completed",{},[this](const ApiResponse &result){ changed(result); });
        })->setObjectName("dangerButton");
        layout->addLayout(actions);
        auto *metrics=new QHBoxLayout; metrics->setSpacing(12);
        const QStringList labels={"总任务","等待中","下发中","已暂停","已完成","失败"};
        const QStringList styles={"queueTotal","queueWaiting","queueRunning","queuePaused","queueCompleted","queueFailed"};
        for(int i=0;i<labels.size();++i) {
            auto *card=new QFrame; card->setObjectName(styles[i]);
            auto *stack=new QVBoxLayout(card); stack->setContentsMargins(16,16,16,16);
            stack->addWidget(clientHint(labels[i]));
            auto *number=new QLabel("0"); number->setObjectName("queueValue"); stack->addWidget(number);
            metrics->addWidget(card,1); counters_ << number;
        }
        layout->addLayout(metrics,1);
        auto *note=new QFrame; note->setObjectName("queueNote");
        auto *noteRow=new QHBoxLayout(note); noteRow->setContentsMargins(15,7,15,7);
        noteRow->addWidget(clientHint("同一设备按队列顺序下发；设备缝纫中或关机时会等待，失败任务会在设备在线且空闲后自动重试。"),1);
        lastUpdated_=clientHint("最后刷新 -"); noteRow->addWidget(lastUpdated_);
        layout->addWidget(note);
        auto *vertical=qobject_cast<QVBoxLayout*>(this->layout());
        vertical->insertWidget(0,hero);
        if(toolbar_->itemAt(0) && toolbar_->itemAt(0)->widget()) toolbar_->itemAt(0)->widget()->hide();
        setLoadedHandler([this](const QJsonArray &rows) {
            QList<int> counts={int(rows.size()),0,0,0,0,0};
            for(const QJsonValue &value:rows) {
                const QString status=value.toObject().value("status").toString();
                if(status=="waiting") ++counts[1]; else if(status=="downloading") ++counts[2];
                else if(status=="paused") ++counts[3]; else if(status=="completed") ++counts[4];
                else if(status=="failed") ++counts[5];
            }
            for(int i=0;i<counters_.size();++i) counters_[i]->setText(QString::number(counts[i]));
            lastUpdated_->setText("最后刷新 "+QTime::currentTime().toString("HH:mm:ss"));
        });
    }
    void changed(const ApiResponse &result) {
        if(!result.ok) { window_->notify(result.error,true); return; }
        window_->notify("操作成功"); refresh();
    }
    void callAll(const QString &path,const QString &success) {
        api_->post(path,QJsonObject{},[this,success](const ApiResponse &result) {
            if(!result.ok) { window_->notify(result.error,true); return; }
            window_->notify(success); refresh();
        });
    }
    void exportLog() {
        const QString target=ClientFileDialog::getSaveFileName(this,"导出下发日志","下发日志.csv","CSV (*.csv)");
        if(target.isEmpty()) return;
        window_->notify("正在导出下发日志...");
        const QMap<QString,QString> base=query();
        auto collected=std::make_shared<QJsonArray>();
        auto load=std::make_shared<std::function<void(int)>>();
        *load=[this,target,base,collected,load](int page) {
            QMap<QString,QString> params=base;
            params["page"]=QString::number(page); params["pageSize"]="200";
            api_->get("/pattern/log"+requestQuery(params),[this,target,base,collected,load,page](const ApiResponse &result) {
                if(!result.ok) { window_->notify(result.error,true); *load={}; return; }
                const QJsonArray chunk=apiRows(result.data);
                for(const QJsonValue &row:chunk) collected->append(row);
                const int total=result.data.toObject().value("total").toInt();
                if(!chunk.isEmpty() && collected->size()<total && page<50) { (*load)(page+1); return; }
                QSaveFile file(target);
                if(!file.open(QIODevice::WriteOnly)) { window_->notify("导出失败",true); *load={}; return; }
                file.write("\xEF\xBB\xBF花型文件,花型类型,针数,文件大小,下发设备,工价,订单号,结果,备注,下发时间\n");
                auto quoted=[](QString text){ text.replace('"',"\"\""); return "\""+text+"\""; };
                for(const QJsonValue &value:*collected) {
                    const QJsonObject row=value.toObject(); QStringList cells;
                    for(const QString &key:{"patternName","patternType","stitches","size","deviceName",
                                            "unitPrice","orderNo","status","message","createdAt"})
                        cells << quoted(row.value(key).toVariant().toString());
                    file.write((cells.join(',')+'\n').toUtf8());
                }
                window_->notify(file.commit() ? "导出成功" : "导出失败",!QFileInfo::exists(target));
                *load={};
            });
        };
        (*load)(1);
    }
    bool history_;
    QList<QLabel*> counters_; QLabel *lastUpdated_=nullptr;
};

ListSpec serverPatternsSpec() {
    ListSpec spec;
    spec.title="服务器花型文件";
    spec.description="管理服务器花型文件，支持上传、编辑、批量下发和本地下载。";
    spec.path="/pattern/list"; spec.searchHint="名称/文件名/类型/订单号";
    spec.optionLabel="花型类型"; spec.optionKey="patternType";
    spec.options={{"全部类型",""}};
    spec.secondaryLabel="订单编号"; spec.secondaryKey="orderNo";
    spec.selection=true; spec.serverPaged=true; spec.dates=true;
    spec.showHeader=false;
    spec.dateWidth=310;
    spec.columns={{"序号","#",60},{"花型名称","name",180},{"花型类型","patternType",130},
        {"文件名","fileName",190},{"文件大小","size",100},{"针数","stitches",100},
        {"工价","unitPrice",100},{"订单编号","orderNo",140},{"上传时间","uploadTime",180}};
    spec.actions={"预览","编辑","下载","下发","删除"};
    return spec;
}

class PatternPage : public ClientPage {
public:
    PatternPage(ApiClient *api,ClientWindow *window):api_(api),window_(window) {
        setObjectName("clientPage");
        auto *layout=new QHBoxLayout(this); layout->setContentsMargins(9,9,9,9); layout->setSpacing(20);
        auto *side=clientCard(); side->setFixedWidth(280);
        auto *sideLayout=new QVBoxLayout(side); sideLayout->setContentsMargins(10,9,10,9); sideLayout->setSpacing(8);
        auto *treeHeader=new QHBoxLayout; treeHeader->addWidget(clientHeading("设备树")); treeHeader->addStretch();
        auto *treeRefresh=new QPushButton("刷新"); treeRefresh->setObjectName("clientLink");
        treeHeader->addWidget(treeRefresh); sideLayout->addLayout(treeHeader);
        auto *legend=new QHBoxLayout;
        for(const auto &state:{QPair<QString,QString>{"空闲","#67c23a"},{"缝纫","#f56c6c"},{"关机","#909399"}}) {
            auto *label=new QLabel(state.first);label->setObjectName("patternStatusLegend"); label->setStyleSheet("color:"+state.second+";"); legend->addWidget(label);
        }
        sideLayout->addLayout(legend);
        treeSearch_=new QLineEdit; treeSearch_->setPlaceholderText("搜索设备或分组");
        sideLayout->addWidget(treeSearch_);
        auto *selection=new QHBoxLayout; selection->addWidget(clientHint("当前选择"));
        selectionName_=clientSelectionLabel();
        selection->addWidget(selectionName_,1);
        auto *clearTree=new QPushButton("清空"); clearTree->setObjectName("clientLink");
        selection->addWidget(clearTree); sideLayout->addLayout(selection);
        tree_=new PatternCheckTree; tree_->setObjectName("groupTree"); tree_->setHeaderHidden(true);
        tree_->setFocusPolicy(Qt::NoFocus); sideLayout->addWidget(tree_,1);
        layout->addWidget(side);
        scroll_=new QScrollArea; scroll_->setFrameShape(QFrame::NoFrame); scroll_->setWidgetResizable(true);
        auto *content=new QWidget; auto *outer=new QVBoxLayout(content);
        outer->setContentsMargins(0,0,0,0); outer->setSpacing(9);
        server_=new ListPage(api,window,serverPatternsSpec());
        server_->setObjectName("clientServerPatterns"); server_->setMinimumHeight(440);
        server_->layout()->setContentsMargins(0,0,0,0);
        server_->addToolbarButton("添加文件",[this]{ upload(); },true);
        server_->addToolbarButton("修改文件",[this]{
            const QList<qint64> ids=server_->checkedIds();
            if(ids.size()!=1) { window_->notify("请选择一个花型",true); return; }
            for(const QJsonValue &value:server_->table()->records())
                if(value.toObject().value("id").toInteger()==ids.first()) { edit(value.toObject()); return; }
        });
        server_->addToolbarButton("批量修改",[this]{ batchEdit(); });
        server_->addToolbarButton("批量下发",[this]{ sendSelected(); })->setObjectName("successButton");
        server_->addToolbarButton("全部下载到本地",[this]{ downloadAll(); });
        server_->addToolbarButton("定位设备文件模块",[this]{ scroll_->ensureWidgetVisible(deviceCard_,10,10); })
            ->setVisible(window_->hasPermission("devicePatternFiles"));
        server_->addToolbarButton("批量删除",[this]{ batchDelete(); })->setObjectName("dangerButton");
        server_->setActionHandler([this](ListPage *,const QJsonObject &row,const QString &action) {
            if(action=="编辑") edit(row);
            else if(action=="预览") preview(row);
            else if(action=="下载") downloadOne(row);
            else if(action=="下发") chooseDevices({row.value("id")});
            else if(action=="删除") removeOne(row);
        });
        outer->addWidget(server_);
        server_->setVisible(window_->hasPermission("patternFiles"));
        deviceCard_=clientCard(); deviceCard_->setObjectName("clientDevicePatterns");
        auto *deviceLayout=new QVBoxLayout(deviceCard_);
        deviceLayout->setContentsMargins(12,11,12,11); deviceLayout->setSpacing(8);
        deviceLayout->addWidget(clientHeading("设备花型文件"));
        deviceLayout->addWidget(clientHint("查询读取服务器缓存；实时读取设备可刷新设备上的花型列表。"));
        auto *filters=new UiFlowLayout(nullptr,8,8);
        deviceSelect_=new UiComboBox; deviceSelect_->setObjectName("patternDeviceSelect"); deviceSelect_->setFixedWidth(220);deviceSelect_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);deviceSelect_->setMinimumContentsLength(10);
        deviceSelect_->addItem("请选择设备",QVariant()); filters->addWidget(deviceSelect_);
        deviceKeyword_=new QLineEdit; deviceKeyword_->setPlaceholderText("文件名/订单号");
        deviceKeyword_->setFixedWidth(175); filters->addWidget(deviceKeyword_);
        deviceType_=new UiComboBox; deviceType_->setFixedWidth(150);
        deviceType_->addItem("花型类型",QString()); filters->addWidget(deviceType_);
        auto *search=new QPushButton("查询"), *reset=new QPushButton("重置");
        search->setObjectName("primaryButton"); filters->addWidget(search); filters->addWidget(reset);
        deviceLayout->addLayout(filters);
        auto *actions=new UiFlowLayout(nullptr,8,8);
        auto *live=new QPushButton("实时读取设备"), *uploadBack=new QPushButton("回传选中文件"),
             *uploadQueue=new QPushButton("上传队列");
        uploadBack->setObjectName("primaryButton");
        auto *deleteSelected=new QPushButton("删除选中文件");deleteSelected->setObjectName("clientDeleteDeviceFiles");
        actions->addWidget(live); actions->addWidget(uploadBack); actions->addWidget(uploadQueue);actions->addWidget(deleteSelected);
        connect(deleteSelected,&QPushButton::clicked,this,[this]{removeDeviceFiles(deviceTable_->checkedRecords());});
        deviceLayout->addLayout(actions);
        deviceTable_=new ClientTable;
        deviceTable_->setProperty("deviceFiles",true);
        deviceTable_->setColumns({{"序号","#",60},{"设备","deviceName",150},
            {"设备文件编号","patternNo",115},
            {"设备文件名","fileName",195},{"花型名称","patternName",160},
            {"花型类型","patternType",130},{"针数","stitches",100},{"文件大小","size",110},
            {"订单编号","orderNo",130},{"更新时间","updateTime",180}},
            true,{"删除"},[this](const QJsonObject &row,const QString&){ removeDeviceFile(row); });
        deviceTable_->setMinimumHeight(180); deviceLayout->addWidget(deviceTable_,1);
        devicePages_=new ClientPagination; deviceLayout->addWidget(devicePages_);
        deviceCard_->setMinimumHeight(370); outer->addWidget(deviceCard_);
        deviceCard_->setVisible(window_->hasPermission("devicePatternFiles"));
        scroll_->setWidget(content); layout->addWidget(scroll_,1);
        auto *states=new QTimer(this);states->setInterval(2000);connect(states,&QTimer::timeout,this,[this]{if(isVisible() && deviceCard_->isVisible() && deviceCard_->isEnabled())pollDeviceStates();});states->start();
        connect(UiI18n::instance(),&UiI18n::languageChanged,this,&PatternPage::updateDeviceStateLabels);
        connect(treeRefresh,&QPushButton::clicked,this,&PatternPage::loadTree);
        connect(clearTree,&QPushButton::clicked,this,[this] {
            {QSignalBlocker blocker(tree_);for(QTreeWidgetItemIterator it(tree_);*it;++it)(*it)->setCheckState(0,Qt::Unchecked);}
            tree_->clearSelection(); setClientSelection(selectionName_); deviceSelect_->setCurrentIndex(0);
        });
        connect(treeSearch_,&QLineEdit::textChanged,this,[this](const QString &keyword) {
            for(QTreeWidgetItemIterator it(tree_);*it;++it) {
                auto *item=*it;
                item->setHidden(!keyword.isEmpty() && item->childCount()==0 &&
                    !item->text(0).contains(keyword,Qt::CaseInsensitive));
            }
        });
        connect(tree_,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *item,int) {
            setClientTreeSelection(selectionName_,item);
            if(item->data(0,Qt::UserRole+1).toString()=="device") {
                const int index=deviceSelect_->findData(item->data(0,Qt::UserRole));
                if(index>=0) deviceSelect_->setCurrentIndex(index);
            }
        });
        connect(tree_,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem *item,int column) {
            if(column!=0) return;
            QSignalBlocker block(tree_);
            if(item->data(0,Qt::UserRole).toString()=="all" && item->checkState(0)!=Qt::PartiallyChecked) {
                const auto state=item->checkState(0);for(QTreeWidgetItemIterator it(tree_);*it;++it)(*it)->setCheckState(0,state);
            }
            if(item->childCount()>0 && item->checkState(0)!=Qt::PartiallyChecked) {
                std::function<void(QTreeWidgetItem*)> cascade=[&](QTreeWidgetItem *parent) {
                    for(int i=0;i<parent->childCount();++i) {
                        auto *child=parent->child(i); child->setCheckState(0,item->checkState(0));
                        cascade(child);
                    }
                };
                cascade(item);
            }
            for(QTreeWidgetItem *parent=item->parent();parent;parent=parent->parent()) {
                bool any=false,all=true;
                for(int i=0;i<parent->childCount();++i) {
                    const Qt::CheckState state=parent->child(i)->checkState(0);
                    any|=state!=Qt::Unchecked; all&=state==Qt::Checked;
                }
                parent->setCheckState(0,all ? Qt::Checked : any ? Qt::PartiallyChecked : Qt::Unchecked);
            }
            int total=0,selected=0;QTreeWidgetItem *allRoot=nullptr;
            for(QTreeWidgetItemIterator it(tree_);*it;++it){if((*it)->data(0,Qt::UserRole).toString()=="all")allRoot=*it;if((*it)->data(0,Qt::UserRole+1)=="device"){++total;selected+=(*it)->checkState(0)==Qt::Checked;}}
            if(allRoot)allRoot->setCheckState(0,selected==0 ? Qt::Unchecked : selected==total ? Qt::Checked : Qt::PartiallyChecked);
            const QJsonArray checked=checkedTreeDevices();
            if(checked.isEmpty()) {
                deviceSelect_->setCurrentIndex(0);
            } else {
                bool selectedStillChecked=false;
                for(const QJsonValue &value:checked)
                    selectedStillChecked|=value.toInteger()==deviceSelect_->currentData().toLongLong();
                if(!selectedStillChecked) {
                    const int index=deviceSelect_->findData(checked.first().toVariant());
                    if(index>=0) deviceSelect_->setCurrentIndex(index);
                }
            }
        });
        connect(search,&QPushButton::clicked,this,[this]{ devicePages_->reset(); refreshDeviceFiles(); });
        connect(deviceKeyword_,&QLineEdit::returnPressed,this,[this]{ devicePages_->reset(); refreshDeviceFiles(); });
        connect(reset,&QPushButton::clicked,this,[this] {
            deviceKeyword_->clear(); deviceType_->setCurrentIndex(0); devicePages_->reset(); refreshDeviceFiles();
        });
        connect(deviceSelect_,&QComboBox::currentIndexChanged,this,[this](int){ devicePages_->reset(); refreshDeviceFiles(); });
        connect(devicePages_,&ClientPagination::changed,this,&PatternPage::refreshDeviceFiles);
        connect(live,&QPushButton::clicked,this,[this] {
            const qint64 id=deviceSelect_->currentData().toLongLong();
            if(id<1) { window_->notify("请先选择设备",true); return; }
            api_->post("/pattern/device-files/refresh",QJsonObject{{"deviceId",id}},[this](const ApiResponse &result) {
                if(!result.ok) { window_->notify(result.error,true); return; }
                window_->notify("设备花型列表已刷新"); refreshDeviceFiles();
            });
        });
        connect(uploadBack,&QPushButton::clicked,this,&PatternPage::uploadSelectedDeviceFiles);
        connect(uploadQueue,&QPushButton::clicked,this,&PatternPage::showUploadQueue);
    }
    void refresh() override;
private:
    void loadTree();
    void addTreeNode(QTreeWidgetItem *parent,const QJsonObject &node);
    QJsonArray checkedTreeDevices() const;
    void refreshDeviceFiles();
    QList<QPair<QString,QJsonValue>> typeChoices() const;
    QList<QPair<QString,QJsonValue>> orderChoices(const QString &current={}) const;
    std::function<void(QComboBox*)> catalogCreate(const QString &endpoint,const QString &title);
    void upload();
    void directUpload(const QList<PatternUploadItem> &items);
    void edit(const QJsonObject &row);
    void preview(const QJsonObject &row);
    void batchEdit();
    void downloadOne(const QJsonObject &row);
    void downloadAll();
    void chooseDevices(const QJsonArray &patternIds);
    void sendSelected();
    void removeOne(const QJsonObject &row);
    void batchDelete();
    void removeDeviceFile(const QJsonObject &row);
    void removeDeviceFiles(const QList<QJsonObject> &rows);
    void uploadSelectedDeviceFiles();
    void showUploadQueue();
    void pollDeviceStates();
    void updateDeviceStateLabels();
    bool deviceStateRequest_=false;
    quint64 deviceFilesRequest_=0;
    ApiClient *api_; ClientWindow *window_;
    ListPage *server_,*uploadQueuePage_=nullptr;
    ClientTable *deviceTable_;
    ClientPagination *devicePages_;
    UiComboBox *deviceSelect_,*deviceType_;
    QLineEdit *deviceKeyword_;
    QJsonArray types_,orders_,devices_;
    QDialog *uploadQueueDialog_=nullptr;
    QTreeWidget *tree_; QLineEdit *treeSearch_; QLabel *selectionName_;
    QScrollArea *scroll_; QWidget *deviceCard_;
};

void PatternPage::refresh() {
    deviceStateRequest_=false;
    if(window_->hasPermission("patternFiles")) server_->refresh();
    if(api_->token()=="preview-token") return;
    loadTree();
    api_->get("/pattern/types",[this](const ApiResponse &result) {
        if(!result.ok) return;
        types_=result.data.toArray();
        QList<QPair<QString,QVariant>> choices={{"全部类型",QString()}};
        const QString current=deviceType_->currentData().toString();
        deviceType_->clear(); deviceType_->addItem("花型类型",QString());
        for(const QJsonValue &value:types_) {
            choices.append({value.toString(),value.toString()});
            deviceType_->addItem(value.toString(),value.toString());
            deviceType_->setItemData(deviceType_->count()-1,true,UiI18n::PreserveDataRole);
        }
        server_->setOptionChoices(choices);
        const int at=deviceType_->findData(current);
        if(at>=0) deviceType_->setCurrentIndex(at);
    });
    if(window_->hasPermission("patternFiles"))
        api_->get("/pattern/order-summary",[this](const ApiResponse &result) {
            if(result.ok) orders_=apiRows(result.data);
        });
    api_->get("/device/list?fullModel=1",[this](const ApiResponse &result) {
        if(!result.ok) return;
        const QVariant selected=deviceSelect_->currentData();
        QSignalBlocker blocker(deviceSelect_);
        devices_=apiRows(result.data);
        deviceSelect_->clear(); deviceSelect_->addItem("请选择设备",QVariant());
        for(const QJsonValue &value:devices_) {
            const QJsonObject row=value.toObject();
            deviceSelect_->addItem(row.value("displayName").toString(row.value("name").toString()),
                row.value("id").toVariant());
            deviceSelect_->setItemData(deviceSelect_->count()-1,true,UiI18n::PreserveDataRole);
        }
        const int index=deviceSelect_->findData(selected);
        if(index>=0) deviceSelect_->setCurrentIndex(index);
        updateDeviceStateLabels();
        if(window_->hasPermission("devicePatternFiles")) refreshDeviceFiles();
    });
    if(window_->hasPermission("devicePatternFiles")) refreshDeviceFiles();
}

void PatternPage::updateDeviceStateLabels() {
    const auto selected=deviceSelect_->currentData();QSignalBlocker blocker(deviceSelect_);
    QSet<qint64> validIds;for(const auto &value:devices_){const auto row=value.toObject();validIds.insert(row.value("id").toInteger());const auto id=row.value("id").toVariant();int at=deviceSelect_->findData(id);const QString label=row.value("displayName").toString(row.value("name").toString())+" · "+UiI18n::instance()->text(displayStatus(row.value("status").toString()));if(at<0){deviceSelect_->addItem(label,id);at=deviceSelect_->count()-1;}else deviceSelect_->setItemText(at,label);deviceSelect_->setItemData(at,true,UiI18n::PreserveDataRole);}
    for(int index=deviceSelect_->count()-1;index>0;--index)if(!validIds.contains(deviceSelect_->itemData(index).toLongLong()))deviceSelect_->removeItem(index);
    const int at=deviceSelect_->findData(selected);deviceSelect_->setCurrentIndex(at>=0 ? at : 0);deviceSelect_->setToolTip(deviceSelect_->currentText());
}
void PatternPage::pollDeviceStates() {
    if(deviceStateRequest_ || api_->token()=="preview-token" || api_->token().isEmpty())return;deviceStateRequest_=true;
    const QPointer<PatternPage> safe(this);api_->get("/device/list?fullModel=1",[safe](const ApiResponse &result){if(!safe)return;safe->deviceStateRequest_=false;if(!result.ok)return;
        const qint64 selected=safe->deviceSelect_->currentData().toLongLong();QString before;for(const auto &value:safe->devices_)if(value.toObject().value("id").toInteger()==selected)before=value.toObject().value("status").toString();safe->devices_=apiRows(result.data);QHash<qint64,QString> states;QHash<qint64,QJsonObject> deviceRows;for(const auto &value:safe->devices_){const auto row=value.toObject();const auto id=row.value("id").toInteger();states[id]=row.value("status").toString();deviceRows[id]=row;}
        safe->updateDeviceStateLabels();QSignalBlocker treeSignals(safe->tree_);for(QTreeWidgetItemIterator it(safe->tree_);*it;++it)if((*it)->data(0,Qt::UserRole+1)=="device"){const QString state=states.value((*it)->data(0,Qt::UserRole).toLongLong(),"offline");(*it)->setIcon(0,deviceStateIcon(state));(*it)->setData(0,Qt::UserRole+2,state);const auto row=deviceRows.value((*it)->data(0,Qt::UserRole).toLongLong());if(!row.isEmpty()){const QString name=row.value("name").toString();(*it)->setText(0,name);(*it)->setToolTip(0,name);if(safe->tree_->currentItem()==*it)setClientSelection(safe->selectionName_,name,true);}}
        if(selected>0 && (before!=states.value(selected,"offline") || safe->deviceSelect_->currentData().toLongLong()!=selected) && safe->deviceCard_->isEnabled()){safe->refreshDeviceFiles();QTimer::singleShot(1200,safe,[safe,selected]{if(safe && safe->isVisible() && safe->deviceCard_->isEnabled() && safe->deviceSelect_->currentData().toLongLong()==selected)safe->refreshDeviceFiles();});}
    });
}

void PatternPage::loadTree() {
    if(api_->token()=="preview-token") return;
    api_->get("/device/tree",[this](const ApiResponse &result) {
        if(!result.ok) return;
        const auto checked=checkedTreeDevices();QSet<qint64> checkedIds;for(const auto &value:checked)checkedIds.insert(value.toInteger());
        const QVariant current=tree_->currentItem() ? tree_->currentItem()->data(0,Qt::UserRole) : QVariant();
        const QVariant kind=tree_->currentItem() ? tree_->currentItem()->data(0,Qt::UserRole+1) : QVariant();
        QSignalBlocker blocker(tree_);
        tree_->clear();
        for(const QJsonValue &value:result.data.toArray()) addTreeNode(nullptr,value.toObject());
        for(QTreeWidgetItemIterator it(tree_);*it;++it){if((*it)->data(0,Qt::UserRole+1)=="device" && checkedIds.contains((*it)->data(0,Qt::UserRole).toLongLong()))(*it)->setCheckState(0,Qt::Checked);if((*it)->data(0,Qt::UserRole)==current && (*it)->data(0,Qt::UserRole+1)==kind)tree_->setCurrentItem(*it);}
        std::function<void(QTreeWidgetItem*)> aggregate=[&](QTreeWidgetItem *node){if(!node->childCount())return;bool all=true,any=false;for(int i=0;i<node->childCount();++i){auto *child=node->child(i);aggregate(child);all&=child->checkState(0)==Qt::Checked;any|=child->checkState(0)!=Qt::Unchecked;}node->setCheckState(0,all ? Qt::Checked : any ? Qt::PartiallyChecked : Qt::Unchecked);};for(int i=0;i<tree_->topLevelItemCount();++i)aggregate(tree_->topLevelItem(i));
        tree_->expandAll();
    });
}

void PatternPage::addTreeNode(QTreeWidgetItem *parent,const QJsonObject &node) {
    auto *item=parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
    const QString kind=node.value("type").toString();
    const QString label=node.value("id").toString()=="all" ? "全部分组" :
        node.value("label").toString(node.value("name").toString());
    item->setText(0,label); item->setData(0,Qt::UserRole,node.value("id").toVariant());
    item->setData(0,Qt::UserRole+1,kind);item->setData(0,Qt::UserRole+2,node.value("status").toString());
    if(clientTreeBusinessLabel(node))
        item->setData(0,UiI18n::PreserveDataRole,true);
    item->setFlags(item->flags()|Qt::ItemIsUserCheckable); item->setCheckState(0,Qt::Unchecked);
    item->setIcon(0,kind=="device" ? deviceStateIcon(node.value("status").toString()) : QIcon(":/folder.svg"));
    item->setToolTip(0,label);
    if(node.value("id").toString()=="ungrouped") item->setForeground(0,QColor("#f56c6c"));
    for(const QJsonValue &value:node.value("children").toArray()) addTreeNode(item,value.toObject());
}

QJsonArray PatternPage::checkedTreeDevices() const {
    QJsonArray selected;
    for(QTreeWidgetItemIterator it(tree_);*it;++it)
        if((*it)->data(0,Qt::UserRole+1).toString()=="device" && (*it)->checkState(0)==Qt::Checked)
            selected.append(QJsonValue::fromVariant((*it)->data(0,Qt::UserRole)));
    return selected;
}

void PatternPage::refreshDeviceFiles() {
    const auto generation=++deviceFilesRequest_;
    const qint64 deviceId=deviceSelect_->currentData().toLongLong();
    if(deviceId<1 || api_->token()=="preview-token") {
        deviceTable_->setRecords({}); devicePages_->setTotal(0); return;
    }
    QMap<QString,QString> params={{"deviceId",QString::number(deviceId)},
        {"page",QString::number(devicePages_->page())},{"pageSize",QString::number(devicePages_->pageSize())}};
    if(!deviceKeyword_->text().trimmed().isEmpty()) params["keyword"]=deviceKeyword_->text().trimmed();
    if(!deviceType_->currentData().toString().isEmpty())
        params["patternType"]=deviceType_->currentData().toString();
    api_->get("/pattern/device-files"+requestQuery(params),[this,generation,deviceId](const ApiResponse &result) {
        if(generation!=deviceFilesRequest_ || deviceSelect_->currentData().toLongLong()!=deviceId)return;
        if(!result.ok) { window_->notify(result.error,true); return; }
        devicePages_->setTotal(result.data.toObject().value("total").toInt());
        QJsonArray rows;
        for(const QJsonValue &value:apiRows(result.data)) {
            QJsonObject row=value.toObject();
            row["deviceName"]=deviceSelect_->currentText();
            row["updateTime"]=row.value("updatedAt");
            rows.append(row);
        }
        deviceTable_->setRecords(rows,(devicePages_->page()-1)*devicePages_->pageSize());
        deviceTable_->setEmptyText(result.data.toObject().value("offline").toBool() ?
            "设备未连接" : "暂无设备花型数据");
    });
}

QList<QPair<QString,QJsonValue>> PatternPage::typeChoices() const {
    QList<QPair<QString,QJsonValue>> choices={{"可输入新类型",QString()}};
    for(const QJsonValue &value:types_) choices.append({value.toString(),value});
    return choices;
}

QList<QPair<QString,QJsonValue>> PatternPage::orderChoices(const QString &current) const {
    QList<QPair<QString,QJsonValue>> choices={{"请选择订单编号",QString()}};
    bool found=false;
    for(const QJsonValue &value:orders_) {
        const QString name=value.isObject() ? value.toObject().value("value").toString() : value.toString();
        if(!name.isEmpty()) { choices.append({name,name}); found|=name==current; }
    }
    if(!found && !current.isEmpty()) choices.append({current,current});
    return choices;
}

std::function<void(QComboBox*)> PatternPage::catalogCreate(const QString &endpoint,const QString &title) {
    return [this,endpoint,title](QComboBox *combo) {
        if(!combo) return;
        const auto value=UiMessageBox::prompt(combo->window(),title,"请输入名称",{},100,endpoint.endsWith("order-summary"));
        if(!value || value->trimmed().isEmpty()) return;
        const QPointer<QComboBox> safeCombo(combo);
        api_->post(endpoint,QJsonObject{{"value",value->trimmed()}},
            [this,safeCombo,value,endpoint](const ApiResponse &result) {
                if(!result.ok) { window_->notify(result.error,true); return; }
                if(endpoint.endsWith("type-summary")) types_.append(value->trimmed());
                else if(endpoint.endsWith("order-summary")) orders_.append(QJsonObject{{"value",value->trimmed()}});
                if(safeCombo) {
                    if(safeCombo->findText(value->trimmed())<0) {
                        safeCombo->addItem(value->trimmed(),value->trimmed());
                        safeCombo->setItemData(safeCombo->count()-1,true,UiI18n::PreserveDataRole);
                    }
                    safeCombo->setCurrentText(value->trimmed());
                }
                window_->notify("新增成功");
            });
    };
}

void PatternPage::upload() {
    QList<PatternUploadItem> selected;bool direct=false;
    {PatternUploadDialog dialog(this,api_,window_,types_,orders_);
    if(dialog.exec()!=QDialog::Accepted)return;selected=dialog.items();direct=dialog.directMode();}
    if(direct){directUpload(selected);return;}
    const auto items=std::make_shared<QList<PatternUploadItem>>(selected);
    const auto index=std::make_shared<int>(0);
    const auto failures=std::make_shared<QStringList>();
    auto next=std::make_shared<std::function<void()>>();
    *next=[this,items,index,failures,next] {
        if(*index>=items->size()) {
            if(failures->isEmpty()) window_->notify(QString("已上传 %1 个花型文件").arg(items->size()));
            else UiMessageBox::output(this,"花型上传结果",
                QString("成功 %1 个，失败 %2 个\n%3")
                    .arg(items->size()-failures->size()).arg(failures->size()).arg(failures->join('\n')));
            refresh(); *next={}; return;
        }
        const PatternUploadItem item=items->at(*index);
        window_->notify(QString("正在上传第 %1/%2 个文件").arg(*index+1).arg(items->size()));
        const QJsonObject fields={{"name",item.name},{"patternType",item.type},
            {"stitches",item.stitches},{"unitPrice",item.price},{"orderNo",item.order}};
        api_->uploadFile("/pattern/upload",item.path,fields,
            [this,items,index,failures,next,item](const ApiResponse &result) {
                if(!result.ok) failures->append(QFileInfo(item.path).fileName()+"："+result.error);
                ++*index; (*next)();
            });
    };
    (*next)();
}

void PatternPage::directUpload(const QList<PatternUploadItem> &items) {
    const QPointer<PatternPage> safe(this);
    api_->get("/device/list?fullModel=1",[safe,items](const ApiResponse &result) {
        if(!safe)return;
        if(!result.ok){safe->window_->notify(result.error,true);return;}
        const auto devices=apiRows(result.data);if(devices.isEmpty()){safe->window_->notify("暂无可选设备",true);return;}
        QJsonArray selected;
        {ClientFormDialog chooser(safe,"选择目标设备",540,540);FormField targets;targets.label="目标设备";targets.key="deviceIds";targets.kind=FormField::MultiChoice;targets.required=true;
        for(const auto &v:devices){const auto device=v.toObject();targets.choices.append({device.value("displayName").toString(device.value("name").toString()),device.value("id")});}
        chooser.addField(targets,safe->checkedTreeDevices());const auto values=chooser.execute();if(!values)return;selected=values->value("deviceIds").toArray();}
        if(items.size()*selected.size()>100){safe->window_->notify("一次直接下发最多100项，请减少文件或设备",true);return;}
        QJsonArray targetRows;for(const auto &v:devices)if(selected.contains(v.toObject().value("id")))targetRows.append(v);
        QList<QPair<QString,QString>> files;for(const auto &item:items)files.append({item.path,item.name});
        DirectPatternDialog progress(safe,safe->api_,files,targetRows);progress.exec();
    });
}

void PatternPage::edit(const QJsonObject &row) {
    ClientFormDialog dialog(this,"修改文件",560,450);
    FormField name; name.label="花型名称"; name.key="name"; name.required=true;
    dialog.addField(name,row.value("name"));
    FormField type; type.label="花型类型"; type.key="patternType";
    type.kind=FormField::Choice; type.editable=true; type.choices=typeChoices();
    type.quickCreate=catalogCreate("/pattern/type-summary","新增花型类型");
    dialog.addField(type,row.value("patternType"));
    FormField stitches; stitches.label="针数"; stitches.key="stitches";
    stitches.kind=FormField::Integer; dialog.addField(stitches,row.value("stitches"));
    FormField price; price.label="工价"; price.key="unitPrice";
    price.kind=FormField::Decimal; dialog.addField(price,row.value("unitPrice"));
    FormField order; order.label="订单编号"; order.key="orderNo";
    order.kind=FormField::Choice; order.choices=orderChoices(row.value("orderNo").toString());
    order.quickCreate=catalogCreate("/pattern/order-summary","新增订单编号");
    dialog.addField(order,row.value("orderNo"));
    const auto data=dialog.execute(); if(!data) return;
    api_->put("/pattern/"+QString::number(row.value("id").toInteger()),*data,[this](const ApiResponse &result) {
        if(!result.ok) { window_->notify(result.error,true); return; }
        window_->notify("花型已更新"); refresh();
    });
}

void PatternPage::preview(const QJsonObject &row) {
    QDialog dialog(this); dialog.setObjectName("editorDialog");
    dialog.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint); dialog.resize(620,510);
    UiModalDialog::attach(&dialog,this);
    auto *outer=new QVBoxLayout(&dialog); outer->setContentsMargins(0,0,0,0);
    auto *header=new QFrame; header->setObjectName("dialogHeader");
    auto *heading=new QHBoxLayout(header); heading->setContentsMargins(20,0,16,0);
    heading->addWidget(clientHeading("花型预览")); heading->addStretch();
    auto *close=new QPushButton("×"); close->setObjectName("dialogClose");
    heading->addWidget(close); connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);
    outer->addWidget(header);
    auto *body=new QWidget; auto *layout=new QVBoxLayout(body);
    layout->setContentsMargins(22,14,22,14); layout->setSpacing(12);
    auto *placeholder=clientCard(); auto *placeholderLayout=new QVBoxLayout(placeholder);
    auto *symbol=new QLabel; symbol->setAlignment(Qt::AlignCenter);
    symbol->setPixmap(QIcon(":/folder.svg").pixmap(46,46)); placeholderLayout->addWidget(symbol);
        auto *message=clientHint("当前格式暂不支持图形预览，请查看花型信息。"); message->setAlignment(Qt::AlignCenter);
        message->setObjectName("patternPreviewUnavailable"); message->setWordWrap(true);
    placeholderLayout->addWidget(message); placeholder->setMinimumHeight(165);
    layout->addWidget(placeholder);
    auto *details=new QGridLayout; details->setSpacing(8);
    const QList<QPair<QString,QString>> fields={{"花型名称",row.value("name").toString()},
        {"文件大小",QString::number(row.value("fileSize").toInteger())+" B"},
        {"花型类型",row.value("patternType").toString()},{"针数",QString::number(row.value("stitches").toInteger())},
        {"工价",QString::number(row.value("unitPrice").toDouble(),'f',3)},
        {"订单编号",row.value("orderNo").toString()}};
    for(int i=0;i<fields.size();++i) {
        auto *caption=clientHint(fields[i].first); caption->setAlignment(Qt::AlignLeft|Qt::AlignTop);
        details->addWidget(caption,i/2*2,i%2*2);
        auto *value=new QLabel(fields[i].second.isEmpty() ? "-" : fields[i].second);
        value->setAlignment(Qt::AlignLeft|Qt::AlignTop);
        value->setProperty("i18nSkip",true); value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setWordWrap(true); value->setToolTip(value->text());
        value->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
        details->addWidget(value,i/2*2,i%2*2+1);
    }
    layout->addLayout(details);
    auto *scroll=new QScrollArea; scroll->setObjectName("patternPreviewScroll");
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setWidget(body);
    outer->addWidget(scroll,1);
    auto *footer=new QFrame; footer->setObjectName("dialogFooter");
    auto *foot=new QHBoxLayout(footer); foot->addStretch();
    auto *done=new QPushButton("关闭"); foot->addWidget(done);
    connect(done,&QPushButton::clicked,&dialog,&QDialog::accept);
    outer->addWidget(footer); dialog.exec();
}

void PatternPage::batchEdit() {
    const QList<qint64> ids=server_->checkedIds();
    if(ids.isEmpty()) { window_->notify("请先选择花型",true); return; }
    ClientFormDialog dialog(this,"批量修改",540,350);
    FormField type; type.label="花型类型"; type.key="patternType";
    type.kind=FormField::Choice; type.editable=true; type.choices=typeChoices();
    type.quickCreate=catalogCreate("/pattern/type-summary","新增花型类型"); dialog.addField(type);
    FormField order; order.label="订单编号"; order.key="orderNo";
    order.kind=FormField::Choice; order.choices=orderChoices();
    order.quickCreate=catalogCreate("/pattern/order-summary","新增订单编号"); dialog.addField(order);
    FormField price; price.label="工价"; price.key="unitPrice";
    price.kind=FormField::Decimal; dialog.addField(price);
    FormField changePrice; changePrice.label="修改工价"; changePrice.key="changeUnitPrice";
    changePrice.kind=FormField::Toggle; dialog.addField(changePrice,false);
    const auto values=dialog.execute(); if(!values) return;
    QJsonArray selected; for(qint64 id:ids) selected.append(id);
    QJsonObject body=*values;
    body.remove("changeUnitPrice");
    if(!values->value("changeUnitPrice").toBool()) body.remove("unitPrice");
    for(const QString &key:{"patternType","orderNo"}) if(body.value(key).toString().trimmed().isEmpty()) body.remove(key);
    body["ids"]=selected;
    api_->post("/pattern/batch-update",body,[this](const ApiResponse &result) {
        if(!result.ok) { window_->notify(result.error,true); return; }
        window_->notify("花型已批量更新"); refresh();
    });
}

void PatternPage::downloadOne(const QJsonObject &row) {
    QString name=row.value("downloadFileName").toString();
    if(name.isEmpty()) name=row.value("name").toString();
    const QString target=ClientFileDialog::getSaveFileName(this,"下载花型文件",name);
    if(target.isEmpty()) return;
    window_->notify("正在下载花型文件...");
    api_->downloadToFile("/pattern/"+QString::number(row.value("id").toInteger())+"/file",target,
        [this](const ApiResponse &result) { window_->notify(result.ok ? "文件已下载" : result.error,!result.ok); });
}

void PatternPage::downloadAll() {
    const QString target=ClientFileDialog::getSaveFileName(this,"下载服务器花型",UiI18n::instance() && UiI18n::instance()->english() ? "ServerPatterns.zip" : "服务器花型文件.zip","ZIP (*.zip)");
    if(target.isEmpty()) return;
    window_->notify("正在下载服务器花型...");
    api_->downloadToFile("/pattern/files/download"+requestQuery(server_->query()),target,
        [this](const ApiResponse &result) { window_->notify(result.ok ? "文件已下载" : result.error,!result.ok); });
}

void PatternPage::chooseDevices(const QJsonArray &patternIds) {
    if(devices_.isEmpty()) { window_->notify("暂无可选设备",true); return; }
    auto submit=[this,patternIds](const QJsonArray &selected) {
        const bool batch=patternIds.size()>1;
        const QJsonObject body=batch ? QJsonObject{{"patternIds",patternIds},{"deviceIds",selected}} :
            QJsonObject{{"patternId",patternIds.first()},{"deviceIds",selected}};
        api_->post(batch ? "/pattern/batch-download" : "/pattern/download",body,[this](const ApiResponse &result) {
            if(!result.ok) { window_->notify(result.error,true); return; }
            window_->notify("已加入下发队列");
        });
    };
    const auto checked=checkedTreeDevices();if(!checked.isEmpty()){submit(checked);return;}
    ClientFormDialog dialog(this,"选择目标设备",520,520);
    FormField targets; targets.label="目标设备"; targets.key="deviceIds";
    targets.kind=FormField::MultiChoice; targets.required=true;
    for(const QJsonValue &value:devices_) {
        const QJsonObject row=value.toObject();
        targets.choices.append({row.value("displayName").toString(row.value("name").toString()),row.value("id")});
    }
    dialog.addField(targets,checked);
    const auto values=dialog.execute(); if(!values) return;
    const QJsonArray selected=values->value("deviceIds").toArray();
    submit(selected);
}

void PatternPage::sendSelected() {
    QJsonArray selected; for(qint64 id:server_->checkedIds()) selected.append(id);
    if(selected.isEmpty()) { window_->notify("请先选择花型",true); return; }
    chooseDevices(selected);
}

void PatternPage::removeOne(const QJsonObject &row) {
    if(UiMessageBox::question(this,"删除花型","确定删除该花型文件吗？")!=UiMessageBox::Yes) return;
    api_->remove("/pattern/"+QString::number(row.value("id").toInteger()),{},[this](const ApiResponse &result) {
        if(!result.ok) { window_->notify(result.error,true); return; }
        window_->notify("花型已删除"); refresh();
    });
}

void PatternPage::batchDelete() {
    const QList<qint64> ids=server_->checkedIds();
    if(ids.isEmpty()) { window_->notify("请先选择花型",true); return; }
    if(UiMessageBox::question(this,"批量删除","确定删除所选花型文件吗？")!=UiMessageBox::Yes) return;
    auto index=std::make_shared<int>(0);
    auto next=std::make_shared<std::function<void()>>();
    *next=[this,ids,index,next] {
        if(*index>=ids.size()) { window_->notify("花型已删除"); refresh(); return; }
        api_->remove("/pattern/"+QString::number(ids[*index]),{},
            [this,index,next](const ApiResponse &result) {
                if(!result.ok) { window_->notify(result.error,true); return; }
                ++*index; (*next)();
            });
    };
    (*next)();
}

void PatternPage::removeDeviceFile(const QJsonObject &row) {removeDeviceFiles({row});}
void PatternPage::removeDeviceFiles(const QList<QJsonObject> &rows) {
    if(rows.isEmpty()){window_->notify("请至少选择一个设备文件",true);return;}
    const bool english=UiI18n::instance() && UiI18n::instance()->english();
    const QString question=english ? QString("Delete the %1 selected device pattern files?").arg(rows.size()) : QString("确定删除选中的 %1 个设备花型文件吗？").arg(rows.size());
    if(UiMessageBox::question(this,"删除设备文件",question)!=UiMessageBox::Yes)return;
    struct Deletion {
        QList<QJsonObject> rows;int completed=0;bool canceled=false,finished=false;QString error,failedFile;
        QPointer<QProgressDialog> progress;QPointer<QWidget> card;std::function<void()> next;
        ~Deletion(){if(card)card->setEnabled(true);if(progress)progress->deleteLater();}
    };
    auto state=std::make_shared<Deletion>();state->rows=rows;state->card=deviceCard_;state->card->setEnabled(false);
    state->progress=new QProgressDialog(QString(),UiI18n::instance()->text("停止后续删除"),0,rows.size(),this);state->progress->setObjectName("deviceFileDeleteProgress");state->progress->setAutoClose(false);state->progress->setAutoReset(false);state->progress->setMinimumDuration(0);state->progress->setWindowTitle(UiI18n::instance()->text("删除设备文件"));UiModalDialog::attach(state->progress,this);
    const QPointer<PatternPage> safe(this);const std::weak_ptr<Deletion> weak=state;
    auto finish=[safe,weak]{auto state=weak.lock();if(!state || state->finished)return;state->finished=true;if(state->progress){state->progress->reset();state->progress->hide();}if(state->card)state->card->setEnabled(true);if(!safe)return;safe->refreshDeviceFiles();const bool en=UiI18n::instance()->english();QString message=(en ? QString("Deleted %1/%2 files") : QString("已删除 %1/%2 个文件")).arg(state->completed).arg(state->rows.size());if(state->canceled)message+=(en ? "; remaining deletions stopped" : "；已停止后续删除");if(!state->error.isEmpty())message+="; "+state->failedFile+": "+UiI18n::instance()->text(state->error);safe->window_->notify(message,!state->error.isEmpty());};
    QObject::connect(state->progress,&QProgressDialog::canceled,this,[weak]{if(auto state=weak.lock())state->canceled=true;});
    state->next=[safe,weak,finish]{auto state=weak.lock();if(!state || !safe)return;if(state->canceled || state->completed>=state->rows.size()){finish();return;}const auto row=state->rows[state->completed];const bool en=UiI18n::instance()->english();state->progress->setLabelText((en ? QString("Deleting device files %1/%2") : QString("正在删除设备文件 %1/%2")).arg(state->completed+1).arg(state->rows.size())+"\n"+row.value("fileName").toString());state->progress->setValue(state->completed);
        safe->api_->request("DELETE","/pattern/device-files/"+QString::number(row.value("id").toInteger()),QJsonObject{{"selectedFile",row}},[state,safe,finish](const ApiResponse &result){if(!safe)return;if(!result.ok){state->error=result.error;state->failedFile=state->rows[state->completed].value("fileName").toString();finish();return;}++state->completed;if(state->progress)state->progress->setValue(state->completed);state->next();},45000);
    };
    state->progress->show();state->next();
}

void PatternPage::uploadSelectedDeviceFiles() {
    const qint64 deviceId=deviceSelect_->currentData().toLongLong();
    const QList<qint64> ids=deviceTable_->checkedIds();
    if(deviceId<1 || ids.isEmpty()) { window_->notify("请至少选择一个设备文件",true); return; }
    QJsonArray selected; for(qint64 id:ids) selected.append(id);
    QJsonArray snapshots;
    for(const QJsonObject &file:deviceTable_->checkedRecords()) snapshots.append(file);
    QJsonObject body{{"deviceId",deviceId},{"fileIds",selected},{"selectedFiles",snapshots}};
    api_->post("/pattern/device-files/upload",body,[this,body](const ApiResponse &result) mutable {
        if(result.ok) { window_->notify("已加入上传队列，请点击上传队列查看进度"); return; }
        if(result.status!=409) { window_->notify(result.error,true); return; }
        ClientFormDialog dialog(this,"同名花型处理",640,600);
        FormField mode; mode.label="处理方式"; mode.key="conflictMode"; mode.kind=FormField::Choice;
        mode.preserveChoices=false;
        mode.choices={{"覆盖同名花型","overwrite"},{"重命名后新增","rename"}};
        dialog.addField(mode,"rename");
        const QJsonArray duplicates=result.data.toObject().value("duplicates").toArray();
        QSet<QString> suggested;
        for(const QJsonValue &value:duplicates) {
            const QJsonObject entry=value.toObject();
            FormField field;
            field.label=(UiI18n::instance() && UiI18n::instance()->english() ? "Pattern name: " : "花型名称：")+QString("[#%1] ").arg(entry.value("patternNo").toInteger())+entry.value("fileName").toString();
            field.preserveLabel=true; field.required=true;
            field.key=QString::number(entry.value("fileId").toInteger());
            const QString base=entry.value("patternName").toString();int suffix=1;QString candidate=entry.value("suggestedName").toString();
            if(candidate.isEmpty() || suggested.contains(candidate.toCaseFolded()))do{candidate=base+QString("(%1)").arg(suffix++);}while(suggested.contains(candidate.toCaseFolded()));suggested.insert(candidate.toCaseFolded());dialog.addField(field,candidate);
        }
        dialog.setFieldPagination(10,1);
        QComboBox *modeControl=nullptr;
        for(auto *combo:dialog.findChildren<QComboBox*>()) if(combo->property("formFieldKey").toString()=="conflictMode") modeControl=combo;
        if(modeControl) {
            auto updateNames=[&dialog,modeControl] {
                const bool rename=modeControl->currentData().toString()=="rename";
                for(auto *input:dialog.findChildren<QLineEdit*>()) if(!input->property("formFieldKey").toString().isEmpty()) {
                    input->setEnabled(rename); input->setProperty("skipRequired",!rename);
                }
                if(!rename) if(auto *error=dialog.findChild<QLabel*>("promptError")) error->hide();
            };
            connect(modeControl,&QComboBox::currentIndexChanged,&dialog,[updateNames](int){ updateNames(); }); updateNames();
        }
        const auto choices=dialog.execute(); if(!choices) return;
        body["conflictMode"]=choices->value("conflictMode");
        QJsonObject names=*choices; names.remove("conflictMode");
        body["renameNames"]=names;
        api_->post("/pattern/device-files/upload",body,[this](const ApiResponse &retry) {
            window_->notify(retry.ok ? "已加入上传队列，请点击上传队列查看进度" : retry.error,!retry.ok);
        });
    });
}

void PatternPage::showUploadQueue() {
    if(!uploadQueueDialog_) {
        uploadQueueDialog_=new QDialog(this); uploadQueueDialog_->setObjectName("editorDialog");
        uploadQueueDialog_->setWindowTitle("上传队列");
        uploadQueueDialog_->setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
        UiModalDialog::attach(uploadQueueDialog_,this);
        uploadQueueDialog_->resize(1040,650);
        auto *layout=new QVBoxLayout(uploadQueueDialog_); layout->setContentsMargins(0,0,0,0);
        auto *header=new QFrame; header->setObjectName("dialogHeader");
        auto *head=new QHBoxLayout(header); head->setContentsMargins(20,0,16,0);
        head->addWidget(clientHeading("上传队列")); head->addStretch();
        auto *close=new QPushButton("×"); close->setObjectName("dialogClose"); head->addWidget(close);
        connect(close,&QPushButton::clicked,uploadQueueDialog_,&QDialog::reject);
        layout->addWidget(header);
        ListSpec spec; spec.title="上传队列"; spec.description="查看设备文件回传进度。";
        spec.path="/pattern/upload-queue"; spec.serverPaged=true; spec.refreshSeconds=2;
        spec.columns={{"序号","#",60},{"设备","deviceName",170},{"文件","fileName",190},
            {"服务器花型名称","patternName",190},{"状态","status",110},{"进度","progress",100},{"备注","message",200},
            {"创建时间","createdAt",180}};
        spec.actions={"暂停","继续","取消"};
        uploadQueuePage_=new ListPage(api_,window_,spec);
        uploadQueuePage_->table()->setProperty("uploadQueue",true);
        uploadQueuePage_->setActionHandler([this](ListPage *,const QJsonObject &row,const QString &action) {
            const QString state=row.value("status").toString();
            if(action=="暂停" && state!="waiting") return;
            if(action=="继续" && state!="paused") return;
            if(action=="取消" && state!="waiting" && state!="paused") return;
            const QString endpoint="/pattern/upload-queue/"+QString::number(row.value("id").toInteger());
            const QString path=action=="暂停" ? endpoint+"/pause" : action=="继续" ? endpoint+"/resume" : endpoint;
            api_->request(action=="取消" ? "DELETE" : "POST",path,QJsonObject{},
                [this](const ApiResponse &result) {
                    window_->notify(result.ok ? "操作成功" : result.error,!result.ok);
                    uploadQueuePage_->refresh();
                });
        });
        auto *clearCompleted=uploadQueuePage_->addToolbarButton("清理已完成",[this] {
            if(UiMessageBox::question(uploadQueueDialog_,"确认清理","确定清理已完成的上传记录吗？")!=UiMessageBox::Yes) return;
            api_->remove("/pattern/upload-queue/completed",{},[this](const ApiResponse &result) {
                window_->notify(result.ok ? "队列已清理" : result.error,!result.ok);
                uploadQueuePage_->refresh();
            });
        });
        clearCompleted->setEnabled(false);
        uploadQueuePage_->setLoadedHandler([clearCompleted](const QJsonArray &rows) {
            bool available=false;
            for(const QJsonValue &value:rows) if(QStringList{"completed","failed","canceled"}.contains(value.toObject().value("status").toString())) available=true;
            clearCompleted->setEnabled(available);
        });
        auto *summaryRow=new QWidget;auto *summaryLayout=new QHBoxLayout(summaryRow);auto *overall=new QProgressBar;overall->setObjectName("uploadOverallProgress");overall->setRange(0,100);overall->setToolTip("处理进度包含已结束的成功或失败任务，请核对各任务结果。");auto *counts=new QLabel;counts->setObjectName("uploadOverallCounts");counts->setProperty("i18nSkip",true);summaryLayout->addWidget(overall,1);summaryLayout->addWidget(counts);layout->addWidget(summaryRow);
        auto summary=std::make_shared<QJsonObject>();auto render=[summary,overall,counts]{const bool en=UiI18n::instance()->english();overall->setValue(summary->value("overallProgress").toInt());overall->setFormat((en ? QString("Task processing ") : QString("任务处理进度 "))+"%p%");counts->setText((en ? QString("Completed %1 · Failed %2 · Total %3") : QString("完成 %1 · 失败 %2 · 总数 %3")).arg(summary->value("completed").toInt()).arg(summary->value("failed").toInt()).arg(summary->value("total").toInt()));};uploadQueuePage_->setResponseHandler([summary,render](const ApiResponse &result){*summary=result.data.toObject().value("summary").toObject();render();});connect(UiI18n::instance(),&UiI18n::languageChanged,uploadQueueDialog_,render);render();
        layout->addWidget(uploadQueuePage_,1);
        auto *footer=new QFrame; footer->setObjectName("dialogFooter");
        auto *foot=new QHBoxLayout(footer); foot->addStretch();
        auto *done=new QPushButton("关闭"); foot->addWidget(done);
        connect(done,&QPushButton::clicked,uploadQueueDialog_,&QDialog::accept);
        layout->addWidget(footer);
    }
    uploadQueuePage_->refresh(); uploadQueueDialog_->exec();
    server_->refresh(); refreshDeviceFiles();
}
}

ClientPage *createPatternPage(const QString &path,ApiClient *api,ClientWindow *window) {
    if(path=="/file/pattern") return new PatternPage(api,window);
    if(path=="/file/queue") return new DownloadQueuePage(api,window,false);
    if(path=="/file/log") return new DownloadQueuePage(api,window,true);
    return nullptr;
}
