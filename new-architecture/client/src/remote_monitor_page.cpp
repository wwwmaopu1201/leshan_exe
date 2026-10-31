#include "remote_monitor_page.h"
#include "api_client.h"
#include "client_window.h"
#include "page_widgets.h"
#include "ui_controls.h"
#include "ui_i18n.h"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QSet>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
class RemoteMonitorPage : public ClientPage {
public:
    RemoteMonitorPage(ApiClient *api,ClientWindow *window):api_(api),window_(window) {
        setObjectName("clientPage");
        auto *outer=new QHBoxLayout(this); outer->setContentsMargins(9,9,9,9); outer->setSpacing(9);
        auto *side=clientCard(); side->setFixedWidth(255);
        auto *sideLayout=new QVBoxLayout(side); sideLayout->setContentsMargins(12,11,12,11); sideLayout->setSpacing(9);
        sideLayout->addWidget(clientHeading("设备树"));
        treeKeyword_=new QLineEdit; treeKeyword_->setPlaceholderText("搜索树节点");
        sideLayout->addWidget(treeKeyword_);
        deviceKeyword_=new QLineEdit; deviceKeyword_->setPlaceholderText("按设备名称搜索");
        sideLayout->addWidget(deviceKeyword_);
        dates_=new DateRangeWidget; sideLayout->addWidget(dates_);
        auto *filters=new QHBoxLayout;
        auto *search=new QPushButton("筛选设备"), *reset=new QPushButton("重置");
        search->setObjectName("primaryButton"); filters->addWidget(search); filters->addWidget(reset);
        sideLayout->addLayout(filters);
        tree_=new QTreeWidget; tree_->setObjectName("groupTree"); tree_->setHeaderHidden(true);
        tree_->setFocusPolicy(Qt::NoFocus); sideLayout->addWidget(tree_,1);
        outer->addWidget(side);

        auto *scroll=new QScrollArea; scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true);
        auto *content=new QWidget; auto *layout=new QVBoxLayout(content);
        layout->setContentsMargins(0,0,0,0); layout->setSpacing(9);
        auto *hero=clientCard(); auto *heroLayout=new QHBoxLayout(hero);
        auto *identity=new QVBoxLayout;
        status_=new QLabel("未选择设备"); status_->setObjectName("clientStatusWarn");
        status_->setProperty("i18nSkip",true);
        identity->addWidget(status_,0,Qt::AlignLeft);
        deviceName_=new QLabel(UiI18n::instance()->text("请选择设备"));
        deviceName_->setObjectName("clientPageTitle"); deviceName_->setProperty("i18nSkip",true);
        identity->addWidget(deviceName_);
        ip_=clientHint("设备 IP：-"); ip_->setProperty("i18nSkip",true); identity->addWidget(ip_);
        heroLayout->addLayout(identity,1);
        auto *reload=new QPushButton("刷新数据"); heroLayout->addWidget(reload);
        layout->addWidget(hero);
        empty_=clientCard(); auto *emptyLayout=new QVBoxLayout(empty_);
        auto *emptyLabel=clientHint("请选择要监控的设备");
        emptyLabel->setAlignment(Qt::AlignCenter); emptyLayout->addWidget(emptyLabel);
        empty_->setMinimumHeight(210); layout->addWidget(empty_);
        details_=new QWidget; auto *detailsLayout=new QVBoxLayout(details_);
        detailsLayout->setContentsMargins(0,0,0,0); detailsLayout->setSpacing(9);
        auto *capability=clientHint("当前显示设备数据；屏幕监控和远程控制尚未接入，需要设备端授权支持。");capability->setObjectName("remoteScreenCapabilityHint");detailsLayout->addWidget(capability);
        auto *metrics=new QGridLayout; metrics->setSpacing(9);
        const QStringList titles={"运行状态","主轴转速","累计针数","当前花型"};
        for(int i=0;i<titles.size();++i) {
            auto *card=clientCard(); auto *cell=new QVBoxLayout(card);
            cell->setContentsMargins(15,12,15,12); cell->addWidget(clientHint(titles[i]));
            auto *value=new QLabel("-"); value->setObjectName("clientMetric");
            if(i==0 || i==3) value->setProperty("i18nSkip",true);
            cell->addWidget(value); metricValues_ << value; metrics->addWidget(card,0,i);
        }
        detailsLayout->addLayout(metrics);
        auto *middle=new QHBoxLayout; middle->setSpacing(9);
        auto *meta=clientCard(); auto *metaLayout=new QVBoxLayout(meta);
        metaLayout->addWidget(clientHeading("设备信息"));
        metaName_=clientHint("设备名称：-"); metaIp_=clientHint("设备 IP：-");
        metaName_->setProperty("i18nSkip",true); metaIp_->setProperty("i18nSkip",true);
        metaLayout->addWidget(metaName_); metaLayout->addWidget(metaIp_); metaLayout->addStretch();
        middle->addWidget(meta,1);
        auto *alarms=clientCard(); auto *alarmsLayout=new QVBoxLayout(alarms);
        alarmsLayout->addWidget(clientHeading("报警信息"));
        alarmTable_=new ClientTable;
        alarmTable_->setColumns({{"报警信息","alarmInfo",250},{"报警时间","alarmTime",170}});
        alarmTable_->setMinimumHeight(210); alarmTable_->setEmptyText("无报警");
        alarmsLayout->addWidget(alarmTable_,1);
        middle->addWidget(alarms,2); detailsLayout->addLayout(middle);
        auto *chartCard=clientCard(); auto *chartLayout=new QVBoxLayout(chartCard);
        chartLayout->addWidget(clientHeading("当日产量趋势（按小时）"));
        chart_=new ClientChart(ClientChart::Line); chart_->setMinimumHeight(230);
        chartLayout->addWidget(chart_,1); detailsLayout->addWidget(chartCard);
        details_->hide(); layout->addWidget(details_);
        layout->addStretch(); scroll->setWidget(content); outer->addWidget(scroll,1);

        connect(search,&QPushButton::clicked,this,&RemoteMonitorPage::reloadDevices);
        connect(deviceKeyword_,&QLineEdit::returnPressed,this,&RemoteMonitorPage::reloadDevices);
        connect(reset,&QPushButton::clicked,this,[this] {
            treeKeyword_->clear(); deviceKeyword_->clear(); dates_->clear(); refresh();
        });
        connect(treeKeyword_,&QLineEdit::textChanged,this,[this](const QString &keyword) {
            for(QTreeWidgetItemIterator it(tree_);*it;++it) {
                QTreeWidgetItem *item=*it;
                item->setHidden(!keyword.isEmpty() && item->childCount()==0 &&
                    !item->text(0).contains(keyword,Qt::CaseInsensitive));
            }
        });
        connect(tree_,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *item,int) {
            qint64 id=0;
            if(item->data(0,Qt::UserRole+1).toString()=="device")
                id=item->data(0,Qt::UserRole).toLongLong();
            else {
                std::function<qint64(QTreeWidgetItem*)> first=[&](QTreeWidgetItem *node) -> qint64 {
                    if(node->data(0,Qt::UserRole+1).toString()=="device")
                        return node->data(0,Qt::UserRole).toLongLong();
                    for(int i=0;i<node->childCount();++i)
                        if(const qint64 found=first(node->child(i));found>0) return found;
                    return 0;
                };
                id=first(item);
            }
            selectDevice(id);
        });
        connect(reload,&QPushButton::clicked,this,[this] {
            refresh(); window_->notify("列表已刷新");
        });
        connect(UiI18n::instance(),&UiI18n::languageChanged,this,[this] {
            renderLabels();
        });
        renderLabels();
        auto *timer=new QTimer(this); timer->setInterval(5000);
        connect(timer,&QTimer::timeout,this,[this] {
            if(isVisible() && !QApplication::activeModalWidget()) refresh();
        });
        timer->start();
    }
    void refresh() override {
        if(property("pendingDeviceId").toLongLong()>0){deviceKeyword_->clear();treeKeyword_->clear();dates_->setRange({},{});}
        reloadTree(); reloadDevices();
    }
private:
    void reloadTree() {
        if(api_->token()=="preview-token") return;
        api_->get("/device/tree",[this](const ApiResponse &result) {
            if(!result.ok) return;
            sourceTree_=result.data.toArray(); rebuildTree();
        });
    }
    void reloadDevices() {
        if(api_->token()=="preview-token") return;
        const quint64 generation=++deviceListGeneration_;
        QMap<QString,QString> query={{"page","1"},{"pageSize","2000"}};
        if(!deviceKeyword_->text().trimmed().isEmpty()) query["keyword"]=deviceKeyword_->text().trimmed();
        if(dates_->startDate().isValid()) query["startDate"]=dates_->startDate().toString("yyyy-MM-dd");
        if(dates_->endDate().isValid()) query["endDate"]=dates_->endDate().toString("yyyy-MM-dd");
        api_->get("/device/list"+requestQuery(query),[this,generation](const ApiResponse &result) {
            if(generation!=deviceListGeneration_)return;
            if(!result.ok) { window_->notify(result.error,true); return; }
            devices_=apiRows(result.data);
            const qint64 requested=property("pendingDeviceId").toLongLong();
            if(requested>0){selectedId_=requested;setProperty("pendingDeviceId",QVariant());}
            bool stillPresent=false;
            for(const QJsonValue &value:devices_)
                stillPresent|=value.toObject().value("id").toInteger()==selectedId_;
            if(!stillPresent) selectedId_=devices_.isEmpty() ? 0 : devices_.first().toObject().value("id").toInteger();
            rebuildTree();
            selectDevice(selectedId_);
        });
    }
    bool addTreeNode(QTreeWidgetItem *parent,const QJsonObject &node,const QSet<qint64> &allowed) {
        const bool device=node.value("type").toString()=="device";
        const qint64 id=node.value("id").toInteger();
        if(device && !allowed.contains(id)) return false;
        auto *item=parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
        item->setText(0,node.value("label").toString(node.value("name").toString()));
        item->setData(0,Qt::UserRole,node.value("id").toVariant());
        item->setData(0,Qt::UserRole+1,device ? "device" : "group");
        if(clientTreeBusinessLabel(node)) item->setData(0,UiI18n::PreserveDataRole,true);
        item->setIcon(0,device ? deviceStateIcon(node.value("status").toString()) : QIcon(":/folder.svg"));
        int children=0;
        for(const QJsonValue &value:node.value("children").toArray())
            if(addTreeNode(item,value.toObject(),allowed)) ++children;
        if(!device && children==0) { delete item; return false; }
        return true;
    }
    void rebuildTree() {
        const QString keyword=treeKeyword_->text();
        QSet<qint64> allowed;
        for(const QJsonValue &value:devices_) allowed.insert(value.toObject().value("id").toInteger());
        tree_->clear();
        for(const QJsonValue &value:sourceTree_) addTreeNode(nullptr,value.toObject(),allowed);
        tree_->expandAll();
        for(QTreeWidgetItemIterator it(tree_);*it;++it) {
            QTreeWidgetItem *item=*it;
            if(item->data(0,Qt::UserRole+1).toString()=="device" &&
               item->data(0,Qt::UserRole).toLongLong()==selectedId_) tree_->setCurrentItem(item);
            item->setHidden(!keyword.isEmpty() && item->childCount()==0 &&
                !item->text(0).contains(keyword,Qt::CaseInsensitive));
        }
    }
    void selectDevice(qint64 id) {
        selectedId_=id; ++detailGeneration_;
        QJsonObject device;
        for(const QJsonValue &value:devices_)
            if(value.toObject().value("id").toInteger()==id) { device=value.toObject(); break; }
        const bool exists=!device.isEmpty();
        selectedDevice_=device;
        empty_->setVisible(!exists); details_->setVisible(exists);
        renderLabels();
        if(!exists) return;
        metricValues_[1]->setText(device.value("status").toString()=="offline" ? QString("-") : QString::number(device.value("currentSpeed").toInt())+" RPM");
        metricValues_[2]->setText(QString::number(device.value("needleCountValue").toInteger()));
        metricValues_[3]->setText(device.value("currentPatternName").toString("-"));
        QJsonArray alarms;
        if(!device.value("alarmCode").toString().isEmpty())
            alarms.append(QJsonObject{{"alarmInfo",device.value("alarmCode")},
                {"alarmTime",device.value("lastProtocolAt")}});
        alarmTable_->setRecords(alarms);
        chart_->setSeries({},"hour","pieces");
        const quint64 generation=detailGeneration_;
        if(window_->hasPermission("dashboard"))
            api_->get("/statistics/dashboard?deviceId="+QString::number(id),[this,generation](const ApiResponse &result) {
                if(generation!=detailGeneration_ || !result.ok) return;
                const QJsonObject data=result.data.toObject();
                chart_->setSeries(data.value("hourlyProduction").toArray(),"hour","pieces");
            });
        if(window_->hasPermission("statusStatistics"))
            api_->get("/statistics/alarm?deviceId="+QString::number(id)+"&page=1&pageSize=5",
                [this,generation](const ApiResponse &result) {
                    if(generation!=detailGeneration_ || !result.ok) return;
                    QJsonArray rows;
                    for(const QJsonValue &value:apiRows(result.data)) {
                        QJsonObject row=value.toObject();
                        row["alarmInfo"]=row.value("alarmInfo").toString(
                            row.value("description").toString(row.value("alarmType").toString("报警")));
                        row["alarmTime"]=row.value("alarmTime").toString(row.value("startTime").toString("-"));
                        rows.append(row);
                    }
                    alarmTable_->setRecords(rows);
                });
    }
    void renderLabels() {
        auto *language=UiI18n::instance();
        const QString raw=selectedDevice_.value("status").toString();
        status_->setObjectName(selectedDevice_.isEmpty() ? "clientStatusWarn" : raw=="offline" ? "clientStatusNeutral" : raw=="working" ? "clientStatusBad" : "clientStatusOk");
        status_->style()->unpolish(status_);status_->style()->polish(status_);
        if(selectedDevice_.isEmpty()) {
            status_->setText(language->text("未选择设备"));
            deviceName_->setText(language->text("请选择设备"));
            ip_->setText(language->text("设备 IP：-"));
            return;
        }
        const QString state=language->text(displayStatus(selectedDevice_.value("status").toString()));
        status_->setText(state); metricValues_[0]->setText(state);
        const QString name=selectedDevice_.value("name").toString();
        const QString ip=selectedDevice_.value("ip").toString("-");
        deviceName_->setText(name);
        ip_->setText(language->text("设备 IP："+ip));
        metaName_->setText(language->text("设备名称："+name));
        metaIp_->setText(language->text("设备 IP："+ip));
    }
    ApiClient *api_; ClientWindow *window_;
    QLineEdit *treeKeyword_,*deviceKeyword_;
    DateRangeWidget *dates_;
    QTreeWidget *tree_;
    QWidget *details_;
    QFrame *empty_;
    QLabel *status_,*deviceName_,*ip_,*metaName_,*metaIp_;
    QList<QLabel*> metricValues_;
    ClientTable *alarmTable_;
    ClientChart *chart_;
    QJsonArray sourceTree_,devices_;
    QJsonObject selectedDevice_;
    qint64 selectedId_=0;
    quint64 detailGeneration_=0;
    quint64 deviceListGeneration_=0;
};
}

ClientPage *createRemoteMonitorPage(const QString &path,ApiClient *api,ClientWindow *window) {
    return path=="/device/monitor" ? new RemoteMonitorPage(api,window) : nullptr;
}
