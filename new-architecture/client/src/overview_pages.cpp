#include "overview_pages.h"
#include "api_client.h"
#include "client_window.h"
#include "page_widgets.h"
#include "ui_controls.h"
#include "ui_i18n.h"

#include <QApplication>
#include <QDate>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QProgressBar>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>

namespace {
class DashboardMetrics : public QWidget {
public:
    DashboardMetrics() { grid_=new QGridLayout(this);grid_->setContentsMargins(0,0,0,0);grid_->setSpacing(9); }
    void addCard(QWidget *card) { cards_.append(card);arrange(); }
protected:
    void resizeEvent(QResizeEvent *event) override { QWidget::resizeEvent(event);arrange(); }
private:
    void arrange() { const int columns=width()>=960 ? 4 : 2;for(int i=0;i<cards_.size();++i)grid_->addWidget(cards_[i],i/columns,i%columns); }
    QGridLayout *grid_;QList<QWidget*> cards_;
};
QPixmap statusIcon(int index,const QColor &color) {
    QPixmap pixmap(80,80); pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2,2); painter.setPen(Qt::NoPen); painter.setBrush(color);
    painter.drawRoundedRect(QRectF(0,0,40,40),7,7);
    painter.setPen(QPen(Qt::white,2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    if(index==2) {
        painter.drawArc(QRectF(11,12,18,18),55*16,255*16);
        painter.drawLine(QPointF(20,8),QPointF(20,21));
    } else {
        painter.drawRoundedRect(QRectF(10,11,20,15),2,2);
        painter.drawLine(QPointF(20,26),QPointF(20,30));
        painter.drawLine(QPointF(15,30),QPointF(25,30));
        if(index==3) painter.drawLine(QPointF(14,19),QPointF(26,19));
    }
    painter.end(); pixmap.setDevicePixelRatio(2); return pixmap;
}
QString hoursText(double value) {
    const qint64 seconds=qMax<qint64>(0,qRound64(value*3600));
    const QString text=QString("%1小时%2分").arg(seconds/3600).arg((seconds%3600)/60);
    return UiI18n::instance() ? UiI18n::instance()->text(text) : text;
}
QWidget *scrollBody(QWidget *content,QWidget *parent) {
    auto *scroll=new QScrollArea(parent); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true); scroll->setWidget(content); return scroll;
}
QLabel *makeMetricCell(QGridLayout *grid,int row,int column,const QString &title) {
    auto *panel=new QFrame; panel->setObjectName("clientCard");
    auto *layout=new QVBoxLayout(panel); layout->setContentsMargins(14,10,14,10); layout->setSpacing(6);
    layout->addWidget(clientHint(title));
    auto *number=new QLabel("0"); number->setObjectName("clientMetric"); layout->addWidget(number);
    grid->addWidget(panel,row,column); return number;
}
QFrame *chartCard(const QString &title,ClientChart **chart,ClientChart::Kind kind) {
    auto *card=clientCard(); auto *layout=new QVBoxLayout(card);
    layout->setContentsMargins(12,10,12,10); layout->setSpacing(7);
    layout->addWidget(clientHeading(title));
    *chart=new ClientChart(kind); layout->addWidget(*chart,1);
    return card;
}

class HomePage : public ClientPage {
public:
    HomePage(ApiClient *api,ClientWindow *window):api_(api),window_(window) {
        setObjectName("clientPage");
        auto *layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
        auto *body=new QWidget; auto *outer=new QVBoxLayout(body);
        outer->setContentsMargins(19,17,19,17); outer->setSpacing(10);
        auto *top=new QHBoxLayout; top->setSpacing(9);
        auto *stack=new QVBoxLayout; stack->setSpacing(9);
        auto *statusCard=clientCard(); auto *statusLayout=new QVBoxLayout(statusCard);
        statusLayout->addWidget(clientHeading("设备状态统计"));
        auto *statusGrid=new QGridLayout; statusGrid->setSpacing(7);
        const QStringList labels={"设备总数","设备开机数","设备关机数","设备开机率"};
        const QStringList tileColors={"#c9ad8b","#1cc859","#ffb000","#289ef9"};
        for(int i=0;i<labels.size();++i) {
            auto *cell=new QWidget;
            auto *cellLayout=new QVBoxLayout(cell); cellLayout->setContentsMargins(10,6,10,6);
            cellLayout->setAlignment(Qt::AlignHCenter); cellLayout->setSpacing(6);
            auto *icon=new QLabel; icon->setAlignment(Qt::AlignCenter); icon->setFixedSize(40,40);
            icon->setPixmap(statusIcon(i,QColor(tileColors[i])));
            cellLayout->addWidget(icon,0,Qt::AlignHCenter);
            auto *caption=clientHint(labels[i]); caption->setAlignment(Qt::AlignCenter);
            cellLayout->addWidget(caption);
            auto *value=new QLabel("0"); value->setObjectName("homeStatusValue");
            value->setAlignment(Qt::AlignCenter); cellLayout->addWidget(value);
            statusValues_ << value; statusGrid->addWidget(cell,i/3,i%3);
        }
        statusLayout->addLayout(statusGrid); statusCard->setFixedHeight(300); stack->addWidget(statusCard);
        auto *usageCard=clientCard(); auto *usageLayout=new QVBoxLayout(usageCard);
        auto *usageHeader=new QHBoxLayout; usageHeader->addWidget(clientHeading("设备使用率统计")); usageHeader->addStretch();
        const QStringList tabs={"设备","设备组","生产线"};
        for(int i=0;i<tabs.size();++i) {
            auto *button=new QPushButton(tabs[i]); button->setObjectName("clientLink");
            usageHeader->addWidget(button);
            connect(button,&QPushButton::clicked,this,[this,i]{ usageMode_=i; rebuildUsage(); });
        }
        usageLayout->addLayout(usageHeader);
        usage_=new ClientTable; usage_->setColumns({{"序号","#",55},{"设备名称","name",155},
            {"运行时长","runningTimeLabel",120},{"待机时长","idleTimeLabel",120},{"使用率","efficiencyLabel",100}});
        usage_->setMinimumHeight(225); usageLayout->addWidget(usage_);
        usageCard->setMinimumHeight(302); stack->addWidget(usageCard);
        top->addLayout(stack,47);
        auto *patternCard=clientCard(); auto *patternLayout=new QVBoxLayout(patternCard);
        patternLayout->addWidget(clientHeading("实时花型款号数据"));
        patterns_=new ClientTable; patterns_->setColumns({{"序号","#",55},{"花型名称","name",150},{"次数","value",70}});
        patterns_->horizontalHeaderItem(1)->setTextAlignment(Qt::AlignLeft|Qt::AlignVCenter);
        patterns_->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignCenter);
        patterns_->horizontalHeaderItem(2)->setTextAlignment(Qt::AlignCenter);
        patterns_->setProperty("highlightFirst",true);
        patternLayout->addWidget(patterns_); top->addWidget(patternCard,31);
        auto *rankingCard=clientCard(); auto *rankingLayout=new QVBoxLayout(rankingCard);
        rankingLayout->addWidget(clientHeading("设备效率排名"));
        auto *rankColumns=new QHBoxLayout; rankColumns->setSpacing(9);
        for(int side=0;side<2;++side) {
            auto *column=new QVBoxLayout; column->setSpacing(7);
            column->addWidget(clientHint(side==0 ? "效率排名（由高到低）" : "低效率排名（由低到高）"));
            auto *entries=new QVBoxLayout; entries->setSpacing(9);
            column->addLayout(entries); column->addStretch(); rankColumns->addLayout(column,1);
            if(side==0) highRanking_=entries; else lowRanking_=entries;
        }
        rankingLayout->addLayout(rankColumns,1); top->addWidget(rankingCard,39);
        outer->addLayout(top,2);
        auto *bottom=new QHBoxLayout; bottom->setSpacing(9);
        bottom->addWidget(chartCard("当前设备运行状态",&runningChart_,ClientChart::Line),2);
        runningChart_->setSecondaryValue("offline",QColor("#27c35b"),QColor("#f5a623"));
        auto *productionCard=clientCard(); auto *productionLayout=new QVBoxLayout(productionCard);
        auto *productionHeader=new QHBoxLayout; productionHeader->addWidget(clientHeading("产量统计"));
        productionHeader->addStretch();
        auto *week=new QPushButton("近7日"), *month=new QPushButton("近1月");
        week->setObjectName("clientLink"); month->setObjectName("clientLink");
        productionHeader->addWidget(week); productionHeader->addWidget(month);
        dates_=new DateRangeWidget; dates_->setFixedWidth(245);
        productionHeader->addWidget(dates_);
        productionLayout->addLayout(productionHeader);
        productionChart_=new ClientChart(ClientChart::Bar); productionLayout->addWidget(productionChart_,1);
        bottom->addWidget(productionCard,3); outer->addLayout(bottom,1);
        layout->addWidget(scrollBody(body,this));
        auto setDays=[this](int days) {
            dates_->setRange(QDate::currentDate().addDays(1-days),QDate::currentDate());
            refreshProduction();
        };
        connect(week,&QPushButton::clicked,this,[setDays]{ setDays(7); });
        connect(month,&QPushButton::clicked,this,[setDays]{ setDays(30); });
        dates_->setRange(QDate::currentDate().addDays(-6),QDate::currentDate());
        auto *timer=new QTimer(this); timer->setInterval(10000);
        connect(timer,&QTimer::timeout,this,[this]{ if(isVisible() && !QApplication::activeModalWidget()) refresh(); });
        timer->start();
        if(UiI18n::instance()) connect(UiI18n::instance(),&UiI18n::languageChanged,this,[this]{ rebuildUsage(); });
    }
    void refresh() override {
        if(api_->token()=="preview-token") return;
        api_->get("/device/list?fullModel=1",[this](const ApiResponse &result) {
            if(!result.ok) return;
            deviceMeta_.clear();
            for(const QJsonValue &value:apiRows(result.data)) {
                const QJsonObject device=value.toObject();
                deviceMeta_.insert(device.value("id").toInteger(),device);
            }
            rebuildUsage();
        });
        api_->get("/device/groups",[this](const ApiResponse &result) {
            if(!result.ok) return;
            groupMeta_.clear();
            std::function<void(const QJsonObject&)> add=[this,&add](const QJsonObject &group) {
                groupMeta_.insert(group.value("id").toInteger(),group);
                for(const QJsonValue &child:group.value("children").toArray()) add(child.toObject());
            };
            for(const QJsonValue &value:result.data.toArray()) add(value.toObject());
            rebuildUsage();
        });
        api_->get("/statistics/home",[this](const ApiResponse &result) {
            if(!result.ok) {
                for(auto *label:statusValues_) label->setText("--");
                usageSource_=QJsonArray{}; rebuildUsage(); patterns_->setRecords(QJsonArray{});
                fillRanking(highRanking_,{},false); fillRanking(lowRanking_,{},true);
                runningChart_->setSeries(QJsonArray{},"hour","online");
                window_->notify(result.error,true); return;
            }
            const QJsonObject data=result.data.toObject();
            const qint64 total=data.value("totalDevices").toInteger(),
                         online=data.value("onlineDevices").toInteger();
            statusValues_[0]->setText(QString::number(total)+" 台");
            statusValues_[1]->setText(QString::number(online)+" 台");
            statusValues_[2]->setText(QString::number(data.value("offlineDevices").toInteger())+" 台");
            statusValues_[3]->setText(QString::number(total>0 ? qRound(online*100.0/total) : 0)+" %");
            usageSource_=data.value("deviceUsage").toArray(); rebuildUsage();
            patterns_->setRecords(data.value("patternUsage").toArray());
            for(int row=0;row<patterns_->rowCount();++row) {
                patterns_->item(row,1)->setTextAlignment(Qt::AlignLeft|Qt::AlignVCenter);
                patterns_->item(row,2)->setTextAlignment(Qt::AlignCenter);
            }
            QVector<QJsonObject> ranked;
            for(const QJsonValue &value:usageSource_) {
                const QJsonObject row=value.toObject();
                const QString status=row.value("status").toString(deviceMeta_.value(row.value("deviceId").toInteger()).value("status").toString());
                if(status=="idle" || status=="working") ranked.append(row);
            }
            std::sort(ranked.begin(),ranked.end(),[](const QJsonObject &a,const QJsonObject &b) {
                return a.value("efficiency").toDouble()>b.value("efficiency").toDouble();
            });
            fillRanking(highRanking_,ranked,false);
            fillRanking(lowRanking_,ranked,true);
            runningChart_->setSeries(data.value("runningStatusByHour").toArray(),"hour","online");
        });
        refreshProduction();
    }
private:
    void fillRanking(QVBoxLayout *layout,const QVector<QJsonObject> &ranked,bool low) {
        while(QLayoutItem *item=layout->takeAt(0)) { delete item->widget(); delete item; }
        if(ranked.isEmpty()) { layout->addWidget(clientHint("暂无数据")); return; }
        for(int i=0;i<qMin(10,ranked.size());++i) {
            const QJsonObject row=ranked[low ? ranked.size()-i-1 : i];
            auto *entry=new QWidget; auto *box=new QVBoxLayout(entry);
            box->setContentsMargins(0,1,0,1); box->setSpacing(4);
            auto *line=new QHBoxLayout; auto *name=clientBusinessLabel(row.value("name").toString());
            name->setProperty("i18nSkip",true); name->setToolTip(name->text());
            auto *rate=new QLabel(QString::number(row.value("efficiency").toDouble(),'f',1)+"%");
            line->addWidget(name,1); line->addWidget(rate); box->addLayout(line);
            auto *bar=new QProgressBar; bar->setObjectName(low ? "rankingLow" : "rankingHigh");
            bar->setTextVisible(false); bar->setFixedHeight(8);
            bar->setRange(0,100); bar->setValue(qBound(0,int(row.value("efficiency").toDouble()),100));
            box->addWidget(bar); layout->addWidget(entry);
        }
    }
    void refreshProduction() {
        if(api_->token()=="preview-token") return;
        QMap<QString,QString> params;
        if(dates_->startDate().isValid()) params["startDate"]=dates_->startDate().toString("yyyy-MM-dd");
        if(dates_->endDate().isValid()) params["endDate"]=dates_->endDate().toString("yyyy-MM-dd");
        api_->get("/statistics/production-trend"+requestQuery(params),[this](const ApiResponse &result) {
            if(result.ok) productionChart_->setSeries(result.data.toObject().value("productionTrend").toArray(),"date","pieces");
            else productionChart_->setSeries(QJsonArray{},"date","pieces");
        });
    }
    void rebuildUsage() {
        QJsonArray decorated;
        for(const QJsonValue &value:usageSource_) {
            QJsonObject item=value.toObject();
            // The home column is sewing time, while the API's runningTime is total powered-on time.
            item["runningTime"]=item.value("processingTime");
            const QJsonObject device=deviceMeta_.value(item.value("deviceId").toInteger());
            const qint64 groupId=item.value("groupId").toInteger(device.value("groupId").toInteger());
            QList<QJsonObject> chain;
            QSet<qint64> visited;
            for(qint64 cursor=groupId;cursor>0 && groupMeta_.contains(cursor) && !visited.contains(cursor);) {
                visited.insert(cursor);
                const QJsonObject group=groupMeta_.value(cursor);
                chain.prepend(group);
                cursor=group.value("parentId").toInteger();
            }
            if(chain.size()>1 && chain.first().value("parentId").toInteger()==0) chain.removeFirst();
            const QString fallback=item.value("groupName").toString(device.value("groupName").toString("未分组"));
            item["groupName"]=chain.isEmpty() ? fallback : chain.last().value("name").toString();
            item["lineName"]=chain.isEmpty() ? QString("未分线") : chain.first().value("name").toString();
            item["groupId"]=groupId;
            item["lineId"]=chain.isEmpty() ? 0 : chain.first().value("id").toInteger();
            if(item.value("name").toString().isEmpty()) item["name"]=device.value("name");
            decorated.append(item);
        }
        QJsonArray source;
        if(usageMode_==0) source=decorated;
        else {
            QMap<QString,QJsonObject> aggregate;
            for(const QJsonValue &value:decorated) {
                const QJsonObject item=value.toObject();
                const QString name=usageMode_==1 ? item.value("groupName").toString("未分组") :
                    item.value("lineName").toString("未分线");
                const QString key=QString::number(item.value(usageMode_==1 ? "groupId" : "lineId").toInteger())+"|"+name;
                QJsonObject sum=aggregate.value(key,{{"name",name},{"runningTime",0.0},
                    {"idleTime",0.0},{"efficiencySum",0.0},{"deviceCount",0}});
                sum["runningTime"]=sum.value("runningTime").toDouble()+item.value("runningTime").toDouble();
                sum["idleTime"]=sum.value("idleTime").toDouble()+item.value("idleTime").toDouble();
                sum["efficiencySum"]=sum.value("efficiencySum").toDouble()+item.value("efficiency").toDouble();
                sum["deviceCount"]=sum.value("deviceCount").toInt()+1;
                aggregate[key]=sum;
            }
            for(QJsonObject item:aggregate) {
                item["efficiency"]=item.value("deviceCount").toInt()>0 ?
                    item.value("efficiencySum").toDouble()/item.value("deviceCount").toInt() : 0;
                source.append(item);
            }
        }
        QJsonArray rows;
        for(const QJsonValue &value:source) {
            QJsonObject item=value.toObject();
            const double running=item.value("runningTime").toDouble(), idle=item.value("idleTime").toDouble();
            const double efficiency=item.value("efficiency").toDouble();
            item["runningTimeLabel"]=hoursText(running); item["idleTimeLabel"]=hoursText(idle);
            item["efficiencyLabel"]=QString::number(efficiency,'f',1)+"%";
            rows.append(item);
        }
        QJsonArray sorted;
        QVector<QJsonObject> ordered; for(const QJsonValue &value:rows) ordered.append(value.toObject());
        std::sort(ordered.begin(),ordered.end(),[](const QJsonObject &a,const QJsonObject &b) {
            return a.value("efficiency").toDouble()>b.value("efficiency").toDouble();
        });
        for(const QJsonObject &item:ordered) sorted.append(item);
        usage_->setRecords(sorted);
    }
    ApiClient *api_; ClientWindow *window_;
    QList<QLabel*> statusValues_;
    ClientTable *usage_,*patterns_;
    QVBoxLayout *highRanking_,*lowRanking_;
    ClientChart *runningChart_,*productionChart_;
    DateRangeWidget *dates_;
    QJsonArray usageSource_;
    QHash<qint64,QJsonObject> deviceMeta_,groupMeta_;
    int usageMode_=0;
};

class DashboardPage : public ClientPage {
public:
    DashboardPage(ApiClient *api,ClientWindow *window):api_(api),window_(window) {
        setObjectName("clientPage");
        auto *outer=new QHBoxLayout(this); outer->setContentsMargins(19,17,19,17); outer->setSpacing(17);
        auto *side=clientCard(); side->setFixedWidth(250); auto *sideLayout=new QVBoxLayout(side);
        auto *groupHeader=new QHBoxLayout; auto *groupTitle=clientHeading("设备组信息");
        groupTitle->setWordWrap(true); groupHeader->addWidget(groupTitle,1);
        for(const auto &state:{QPair<QString,QString>{"空闲","#67c23a"},{"缝纫","#f56c6c"},{"关机","#909399"}}) {
            auto *legend=new QLabel(state.first); legend->setObjectName("dashboardStatusLegend");
            legend->setStyleSheet("color:"+state.second+";"); groupHeader->addWidget(legend);
        }
        sideLayout->addLayout(groupHeader);
        tree_=new QTreeWidget; tree_->setObjectName("groupTree"); tree_->setHeaderHidden(true);
        tree_->setFocusPolicy(Qt::NoFocus); sideLayout->addWidget(tree_,1); outer->addWidget(side);
        connect(tree_,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *item,int) {
            selectedIds_=item->data(0,Qt::UserRole).toString(); refreshDashboard();
        });
        auto *content=new QWidget; auto *main=new QVBoxLayout(content);
        main->setContentsMargins(0,0,0,0); main->setSpacing(10);
        auto *metricRow=new DashboardMetrics;
        const QStringList cardTitles={"产量统计","设备使用率","报警统计","开机设备数"};
        const QStringList labels={"今日产量（件）","总产量（件）","当天使用率","平均使用率",
            "今日报警数","总报警数","开机设备总数","总设备数"};
        const QStringList themes={"dashboardBlue","dashboardGreen","dashboardOrange","dashboardCyan"};
        for(int cardIndex=0;cardIndex<4;++cardIndex) {
            auto *card=new QFrame; card->setObjectName(themes[cardIndex]); card->setMinimumHeight(108);
            auto *box=new QVBoxLayout(card); box->setContentsMargins(15,12,15,12); box->setSpacing(7);
            auto *heading=new QLabel(cardTitles[cardIndex]); heading->setObjectName("dashboardCardTitle");
            heading->setWordWrap(true);
            box->addWidget(heading);
            auto *pair=new QHBoxLayout; pair->setSpacing(8);
            for(int metricIndex=0;metricIndex<2;++metricIndex) {
                auto *stack=new QVBoxLayout; stack->setSpacing(2);
                auto *caption=new QLabel(labels[cardIndex*2+metricIndex]); caption->setObjectName("dashboardCardLabel");
                caption->setWordWrap(true);caption->setMinimumWidth(0);
                auto *number=new QLabel("0"); number->setObjectName("dashboardCardValue");
                stack->addWidget(caption); stack->addWidget(number); pair->addLayout(stack,1);
                values_ << number;
            }
            box->addLayout(pair); metricRow->addCard(card);
        }
        main->addWidget(metricRow);
        auto *charts=new QHBoxLayout; charts->setSpacing(9);
        auto *runtimeCard=clientCard(); auto *runtimeBox=new QVBoxLayout(runtimeCard);
        runtimeBox->addWidget(clientHeading("运行/加工时长统计"));
        auto *runtimeRow=new QHBoxLayout;
        auto *runtimeSummary=new QVBoxLayout;
        runtimeSummary->addWidget(clientHint("当天运行时长"));
        runSummary_=new QLabel("0小时0分"); runSummary_->setObjectName("clientMetric");
        runtimeSummary->addWidget(runSummary_); runtimeSummary->addSpacing(15);
        runtimeSummary->addWidget(clientHint("当天加工时长"));
        processSummary_=new QLabel("0小时0分"); processSummary_->setObjectName("clientMetric");
        runtimeSummary->addWidget(processSummary_); runtimeSummary->addStretch();
        runtimeRow->addLayout(runtimeSummary,1);
        runtimeChart_=new ClientChart(ClientChart::Line); runtimeRow->addWidget(runtimeChart_,2);
        runtimeChart_->setSecondaryValue("processingTime");
        runtimeChart_->setValueLabels("运行时长","加工时长","hours");
        runtimeBox->addLayout(runtimeRow,1); charts->addWidget(runtimeCard,1);
        auto *utilCard=clientCard(); auto *utilBox=new QVBoxLayout(utilCard);
        utilBox->addWidget(clientHeading("设备使用率"));
        auto *utilRow=new QHBoxLayout; auto *utilSummary=new QVBoxLayout;
        utilSummary->addWidget(clientHint("当天使用率"));
        utilSummary_=new QLabel("0%"); utilSummary_->setObjectName("clientMetric");
        utilSummary->addWidget(utilSummary_); utilSummary->addStretch();
        utilRow->addLayout(utilSummary,1);
        utilizationChart_=new ClientChart(ClientChart::Bar); utilRow->addWidget(utilizationChart_,2);
        utilizationChart_->setValueLabels("设备使用率",{},"percent");
        utilBox->addLayout(utilRow,1); charts->addWidget(utilCard,1);
        main->addLayout(charts,1);
        main->addWidget(chartCard("加工产量统计",&productionChart_,ClientChart::Bar),1);
        outer->addWidget(scrollBody(content,this),1);
        auto *timer=new QTimer(this); timer->setInterval(10000);
        connect(timer,&QTimer::timeout,this,[this]{ if(isVisible() && !QApplication::activeModalWidget()) refresh(); });
        timer->start();
    }
    void refresh() override {
        if(api_->token()=="preview-token") return;
        const QString selectedKey=tree_->currentItem() ? tree_->currentItem()->data(0,Qt::UserRole+1).toString() : QString();
        const bool firstLoad=tree_->topLevelItemCount()==0;
        QSet<QString> expanded;
        for(QTreeWidgetItemIterator it(tree_);*it;++it) if((*it)->isExpanded()) expanded.insert((*it)->data(0,Qt::UserRole+1).toString());
        api_->get("/device/tree",[this,selectedKey,firstLoad,expanded](const ApiResponse &result) {
                if(result.ok) {
                    const QJsonArray roots=result.data.toArray();
                    tree_->clear();
                    for(const QJsonValue &value:roots) addNode(nullptr,value.toObject());
                    for(QTreeWidgetItemIterator it(tree_);*it;++it) {
                        const QString key=(*it)->data(0,Qt::UserRole+1).toString();
                        (*it)->setExpanded(firstLoad || expanded.contains(key));
                        if(key==selectedKey) tree_->setCurrentItem(*it);
                    }
                    if(!tree_->currentItem() && tree_->topLevelItemCount()>0) tree_->setCurrentItem(tree_->topLevelItem(0));
                }
                refreshDashboard();
            });
    }
private:
    QStringList descendantIds(const QJsonObject &item) const {
        QStringList result;
        if(item.value("type").toString()=="device") result << QString::number(item.value("id").toInteger());
        for(const QJsonValue &child:item.value("children").toArray()) result+=descendantIds(child.toObject());
        result.removeDuplicates(); return result;
    }
    void addNode(QTreeWidgetItem *parent,const QJsonObject &item) {
        auto *node=parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
        node->setText(0,item.value("id").toString()=="all" ? "全部分组" :
            item.value("label").toString(item.value("name").toString()));
        if(item.value("id").toString()=="ungrouped") node->setForeground(0,QColor("#f56c6c"));
        node->setData(0,UiI18n::PreserveDataRole,clientTreeBusinessLabel(item));
        node->setData(0,Qt::UserRole+1,item.value("type").toString()+":"+item.value("id").toVariant().toString());
        if(item.value("type").toString()=="device") {
            const QString status=item.value("status").toString();
            const QColor color=status=="working" ? QColor("#f56c6c") : status=="idle" ? QColor("#67c23a") : QColor("#909399");
            QPixmap dot(14,14); dot.fill(Qt::transparent); QPainter painter(&dot);
            painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen); painter.setBrush(color); painter.drawEllipse(3,3,8,8);
            painter.end(); node->setIcon(0,QIcon(dot)); node->setData(0,Qt::UserRole+2,status);
            node->setToolTip(0,node->text(0));
        }
        const QString joined=descendantIds(item).join(',');
        node->setData(0,Qt::UserRole,joined.isEmpty() && item.value("id").toString()!="all" ? "0" : joined);
        for(const QJsonValue &child:item.value("children").toArray()) addNode(node,child.toObject());
    }
    void refreshDashboard() {
        if(api_->token()=="preview-token") return;
        QMap<QString,QString> params; if(!selectedIds_.isEmpty()) params["deviceIds"]=selectedIds_;
        api_->get("/statistics/dashboard"+requestQuery(params),[this](const ApiResponse &result) {
            if(!result.ok) { window_->notify(result.error,true); return; }
            const QJsonObject data=result.data.toObject();
            const QStringList fields={"todayPieces","totalPieces","utilizationRate","utilizationRate",
                "todayAlarmCount","totalAlarmCount","onlineDeviceCount","scopeDeviceCount"};
            for(int i=0;i<values_.size();++i)
                values_[i]->setText(i==2 || i==3 ? QString::number(data.value(fields[i]).toDouble(),'f',1)+"%" :
                    QString::number(data.value(fields[i]).toInteger()));
            const QJsonArray utilization=data.value("utilizationTrend").toArray();
            if(!utilization.isEmpty()) {
                double sum=0; for(const QJsonValue &value:utilization) sum+=value.toObject().value("value").toDouble();
                values_[3]->setText(QString::number(sum/utilization.size(),'f',1)+"%");
            }
            runSummary_->setText(hoursText(data.value("runningTime").toDouble()));
            processSummary_->setText(hoursText(data.value("processingTime").toDouble()));
            utilSummary_->setText(QString::number(data.value("utilizationRate").toDouble(),'f',1)+"%");
            runtimeChart_->setSeries(data.value("runningProcessingTrend").toArray(),"date","runningTime");
            utilizationChart_->setSeries(utilization);
            productionChart_->setSeries(data.value("hourlyProduction").toArray());
        });
    }
    ApiClient *api_; ClientWindow *window_;
    QTreeWidget *tree_; QString selectedIds_;
    QList<QLabel*> values_;
    ClientChart *runtimeChart_,*utilizationChart_,*productionChart_;
    QLabel *runSummary_,*processSummary_,*utilSummary_;
};
}

ClientPage *createOverviewPage(const QString &path,ApiClient *api,ClientWindow *window) {
    if(path=="/home") return new HomePage(api,window);
    if(path=="/dashboard") return new DashboardPage(api,window);
    return nullptr;
}
