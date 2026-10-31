#include "statistics_pages.h"
#include "api_client.h"
#include "client_window.h"
#include "client_file_dialog.h"
#include "excel_export.h"
#include "page_widgets.h"
#include "ui_controls.h"
#include "ui_dialogs.h"
#include "ui_i18n.h"

#include <QDate>
#include <QDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <memory>
#include <functional>
#include <QSet>
#include <algorithm>

namespace {
struct StatsSpec {
    QString kind,title,description,firstChart,secondChart,firstSeries,secondSeries;
    QString firstValue="value",secondValue="value";
    ClientChart::Kind firstKind=ClientChart::Bar,secondKind=ClientChart::Line;
    QList<TableColumn> columns;
    QList<QPair<QString,QString>> metrics;
};
StatsSpec statsSpec(const QString &path) {
    StatsSpec spec;
    spec.kind=path.section('/',-1);
    if(spec.kind=="salary") {
        spec.title="工资统计"; spec.description="按员工、设备与日期查看工资和加工记录。";
        spec.firstChart="员工工资排行"; spec.secondChart="日工资趋势";
        spec.firstSeries="salaryRank"; spec.secondSeries="salaryTrend";
        spec.firstKind=ClientChart::HorizontalBar;
        spec.metrics={{"工资总额","totalSalary"},{"加工总件数","totalPieces"},
            {"平均工资","averageSalary"}};
        spec.columns={{"序号","#",60},{"员工姓名","employeeName",135},{"员工工号","employeeCode",125},
            {"设备名称","deviceName",150},{"工作天数","workDays",95},{"花型名称","patternName",165},
            {"订单号","orderNo",125},{"加工件数","totalPieces",105},{"单价(元)","unitPrice",105},
            {"工资(元)","salary",110},{"合计(元)","totalAmount",120},{"日期","date",130}};
    } else if(spec.kind=="process") {
        spec.title="加工概况"; spec.description="查看设备加工量、效率和生产趋势。";
        spec.firstChart="日产量趋势"; spec.secondChart="设备产量分布";
        spec.firstSeries="productionTrend"; spec.secondSeries="deviceDistribution";
        spec.secondKind=ClientChart::Pie;
        spec.metrics={{"加工总件数","totalPieces"},{"总底线用量","totalThread"},
            {"总加工时长","totalHours"},{"平均效率","avgEfficiency"}};
        spec.columns={{"序号","#",60},{"设备名称","deviceName",145},{"员工工号","employeeCode",110},
            {"员工姓名","employeeName",120},{"日期","date",125},{"花型名称","patternName",160},
            {"花型针数","patternStitches",105},{"平均缝纫速度(针/分钟)","sewSpeed",155},
            {"开始时间","startTime",175},{"加工次数","processCount",95},
            {"平均加工时长","avgProcessDuration",150},{"花型缝纫次数","patternSewCount",130},
            {"报警信息","alarmInfo",155},{"报警时间","alarmTime",175},
            {"累计开机时长","cumulativeUpTime",140}};
    } else if(spec.kind=="duration") {
        spec.title="时长统计"; spec.description="对照加工、空闲、报警和设备运行时间。";
        spec.firstChart="时长分布"; spec.secondChart="日运行时长趋势";
        spec.firstSeries="durationPie"; spec.secondSeries="durationTrend";
        spec.firstKind=ClientChart::Pie; spec.secondValue="totalTime";
        spec.metrics={{"运行总时长","totalTime"},{"加工时长","runningTime"},
            {"空闲时长","idleTime"},{"报警时长","alarmTime"}};
        spec.columns={{"序号","#",60},{"设备名称","deviceName",145},{"员工工号","employeeCode",110},
            {"员工姓名","employeeName",120},{"日期","date",125},{"花型名称","patternName",160},
            {"开始时间","startTime",175},{"结束时间","endTime",175},
            {"缝纫时长","sewDuration",135},{"平均缝纫时长","avgSewDuration",155}};
    } else {
        spec.kind="alarm"; spec.title="报警统计"; spec.description="查看报警组成、处理情况和趋势。";
        spec.firstChart="报警类型分布"; spec.secondChart="日报警趋势";
        spec.firstSeries="alarmTypePie"; spec.secondSeries="alarmTrend";
        spec.firstKind=ClientChart::Pie; spec.secondValue="count";
        spec.metrics={{"报警总数","totalAlarms"},{"持续时长(分)","totalDuration"},
            {"受影响设备","affectedDevices"},{"已处理比例","resolvedRate"}};
        spec.columns={{"序号","#",60},{"设备名称","deviceName",145},{"员工工号","employeeCode",110},
            {"员工姓名","employeeName",120},{"报警时间","alarmTime",180},
            {"报警信息","alarmInfo",225},
            {"持续时长","duration",115},{"处理状态","status",115}};
    }
    return spec;
}
QFrame *metricCard(const QString &title,QLabel **value) {
    auto *card=clientCard(); auto *layout=new QVBoxLayout(card);
    layout->setContentsMargins(14,11,14,11);
    layout->addWidget(clientHint(title)); *value=new QLabel("0");
    (*value)->setObjectName("clientMetric"); layout->addWidget(*value); return card;
}

class StatisticsPage : public ClientPage {
public:
    StatisticsPage(ApiClient *api,ClientWindow *window,const QString &path)
        :api_(api),window_(window),spec_(statsSpec(path)) {
        setObjectName("clientPage");
        auto *outer=new QHBoxLayout(this); outer->setContentsMargins(9,9,9,9); outer->setSpacing(8);
        auto *side=clientCard(); side->setFixedWidth(250); auto *sideLayout=new QVBoxLayout(side);
        sideLayout->addWidget(clientHeading("设备树筛选"));
        auto *legend=new QHBoxLayout;
        for(const auto &state:{QPair<QString,QString>{"空闲","#67c23a"},{"缝纫","#f56c6c"},{"关机","#909399"}}) {
            auto *label=new QLabel(state.first);label->setObjectName("statisticsStatusLegend");label->setStyleSheet("color:"+state.second+";");legend->addWidget(label);
        }
        legend->addStretch();sideLayout->addLayout(legend);
        auto *treeSearch=new QLineEdit; treeSearch->setPlaceholderText("搜索设备、员工或分组");
        sideLayout->addWidget(treeSearch);
        auto *selection=clientCard(); auto *selectionRow=new QHBoxLayout(selection);
        selectionRow->setContentsMargins(6,2,6,2);
        selectionRow->addWidget(clientHint("当前选择"));
        selectionName_=clientSelectionLabel();
        selectionRow->addWidget(selectionName_,1);
        auto *clearSelection=new QPushButton("清空"); clearSelection->setObjectName("clientLink");
        selectionRow->addWidget(clearSelection); sideLayout->addWidget(selection);
        tree_=new QTreeWidget; tree_->setObjectName("groupTree"); tree_->setHeaderHidden(true);
        tree_->setFocusPolicy(Qt::NoFocus); sideLayout->addWidget(tree_,1); outer->addWidget(side);
        connect(tree_,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *item,int) {
            selectedIds_=item->data(0,Qt::UserRole).toString();
            if(spec_.kind=="salary") {
                QSignalBlocker blocked(employee_);
                employee_->setCurrentIndex(0);
                keyword_->clear();
                if(item->data(0,Qt::UserRole+3).toString()=="device") {
                    const QString code=item->data(0,Qt::UserRole+1).toString().trimmed();
                    const QString name=item->data(0,Qt::UserRole+2).toString().trimmed();
                    for(const QJsonValue &value:allRows_) {
                        const QJsonObject row=value.toObject();
                        if((!code.isEmpty() && row.value("employeeCode").toString()==code) ||
                           (!name.isEmpty() && row.value("employeeName").toString()==name)) {
                            const int index=employee_->findData(row.value("employeeId").toVariant());
                            if(index>=0) employee_->setCurrentIndex(index);
                            break;
                        }
                    }
                }
            }
            setClientTreeSelection(selectionName_,item); pages_->reset(); fetchData();
        });
        connect(clearSelection,&QPushButton::clicked,this,[this] {
            tree_->clearSelection(); selectedIds_.clear(); setClientSelection(selectionName_);
            pages_->reset(); fetchData();
        });
        connect(treeSearch,&QLineEdit::textChanged,this,[this](const QString &text) {
            for(QTreeWidgetItemIterator it(tree_);*it;++it)
                (*it)->setHidden(!text.isEmpty() && (*it)->childCount()==0 &&
                    !(*it)->text(0).contains(text,Qt::CaseInsensitive));
        });
        auto *scroll=new QScrollArea; scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true);
        auto *content=new QWidget; auto *layout=new QVBoxLayout(content);
        layout->setContentsMargins(0,0,0,0); layout->setSpacing(9);
        auto *filter=clientCard(); auto *flow=new UiFlowLayout(filter,8,8);
        flow->setContentsMargins(12,10,12,10);
        dates_=new DateRangeWidget; dates_->setFixedWidth(375);
        dates_->setRange(QDate::currentDate().addDays(-29),QDate::currentDate());
        flow->addWidget(dates_);
        if(spec_.kind=="salary") {
            employee_=new UiComboBox; employee_->setFixedWidth(160);
            employee_->addItem("全部员工",QVariant()); flow->addWidget(employee_);
            connect(employee_,&QComboBox::currentIndexChanged,this,[this](int) {
                const qint64 id=employee_->currentData().toLongLong();
                if(id<1) {
                    selectedIds_.clear(); tree_->clearSelection(); setClientSelection(selectionName_);
                } else {
                    QString code,name;
                    for(const QJsonValue &value:allRows_) {
                        const QJsonObject row=value.toObject();
                        if(row.value("employeeId").toInteger()==id) {
                            code=row.value("employeeCode").toString();
                            name=row.value("employeeName").toString(); break;
                        }
                    }
                    for(const QJsonValue &value:devices_) {
                        const QJsonObject device=value.toObject();
                        if((!code.isEmpty() && device.value("employeeCode").toString()==code) ||
                           (!name.isEmpty() && device.value("employeeName").toString()==name)) {
                            selectedIds_=QString::number(device.value("id").toInteger());
                            setClientSelection(selectionName_,device.value("name").toString(),true);
                            for(QTreeWidgetItemIterator it(tree_);*it;++it)
                                if((*it)->data(0,Qt::UserRole+3).toString()=="device" &&
                                   (*it)->data(0,Qt::UserRole).toString()==selectedIds_) {
                                    tree_->setCurrentItem(*it); break;
                                }
                            break;
                        }
                    }
                }
                pages_->reset(); fetchData();
            });
        }
        if(spec_.kind=="alarm") {
            alarmType_=new UiComboBox; alarmType_->setFixedWidth(150);
            alarmType_->addItem("全部类型",QString()); flow->addWidget(alarmType_);
        }
        keyword_=new QLineEdit; keyword_->setPlaceholderText(spec_.kind=="salary" ? "姓名 / 工号" : "按设备名称搜索明细");
        keyword_->setFixedWidth(178); flow->addWidget(keyword_);
        if(spec_.kind=="salary") {
            deviceKeyword_=new QLineEdit; deviceKeyword_->setPlaceholderText("支持设备名模糊筛选");
            deviceKeyword_->setFixedWidth(175); flow->addWidget(deviceKeyword_);
            salaryMin_=new QLineEdit; salaryMax_=new QLineEdit;
            salaryMin_->setPlaceholderText("最低"); salaryMax_->setPlaceholderText("最高");
            salaryMin_->setFixedWidth(106); salaryMax_->setFixedWidth(106);
            auto *range=new QWidget;range->setObjectName("salaryRangeGroup");auto *rangeLayout=new QHBoxLayout(range);rangeLayout->setContentsMargins(0,0,0,0);rangeLayout->setSpacing(7);
            rangeLayout->addWidget(clientHint("工资区间"));rangeLayout->addWidget(salaryMin_);rangeLayout->addWidget(clientHint("至"));rangeLayout->addWidget(salaryMax_);flow->addWidget(range);
        }
        auto *search=new QPushButton("搜索"), *reset=new QPushButton("重置");
        search->setObjectName("primaryButton"); flow->addWidget(search); flow->addWidget(reset);
        layout->addWidget(filter);
        connect(search,&QPushButton::clicked,this,[this]{ pages_->reset(); fetchData(); });
        connect(keyword_,&QLineEdit::returnPressed,this,[this]{ pages_->reset(); fetchData(); });
        connect(reset,&QPushButton::clicked,this,[this] {
            dates_->setRange(QDate::currentDate().addDays(-29),QDate::currentDate());
            keyword_->clear();
            if(employee_) { QSignalBlocker blocked(employee_); employee_->setCurrentIndex(0); }
            if(deviceKeyword_) deviceKeyword_->clear();
            if(salaryMin_) salaryMin_->clear(); if(salaryMax_) salaryMax_->clear();
            if(alarmType_) alarmType_->setCurrentIndex(0);
            selectedIds_.clear(); tree_->clearSelection(); pages_->reset(); fetchData();
        });
        auto *metricRow=new QGridLayout; metricRow->setSpacing(8);
        for(int i=0;i<spec_.metrics.size();++i) {
            QLabel *value=nullptr; metricRow->addWidget(metricCard(spec_.metrics[i].first,&value),i/4,i%4);
            metrics_ << value;
        }
        layout->addLayout(metricRow);
        auto *charts=new QHBoxLayout; charts->setSpacing(9);
        for(int i=0;i<2;++i) {
            auto *card=clientCard(); auto *box=new QVBoxLayout(card);
            box->addWidget(clientHeading(i==0 ? spec_.firstChart : spec_.secondChart));
            auto *chart=new ClientChart(i==0 ? spec_.firstKind : spec_.secondKind);
            box->addWidget(chart,1); charts->addWidget(card,1);
            chart->setProperty("statisticsChart",spec_.kind+"-"+QString::number(i));
            if(i==0) firstChart_=chart; else secondChart_=chart;
        }
        layout->addLayout(charts);
        if(spec_.kind=="salary") {firstChart_->setValueLabels("工资",{},"currency");secondChart_->setValueLabels("工资",{},"currency");}
        else if(spec_.kind=="alarm") {secondChart_->setValueLabels("报警次数");secondChart_->setHoverDetail("avgDuration","平均报警时长","minutes");}
        else if(spec_.kind=="duration")secondChart_->setValueLabels("运行总时长",{},"hours");
        auto *tableCard=clientCard(); auto *tableLayout=new QVBoxLayout(tableCard);
        auto *head=new QHBoxLayout; head->addWidget(clientHeading(spec_.title+"明细")); head->addStretch();
        auto *exportButton=new QPushButton("导出 Excel");
        exportButton->setObjectName("primaryButton");
        head->addWidget(exportButton); tableLayout->addLayout(head);
        connect(exportButton,&QPushButton::clicked,this,[this,exportButton] {
            if(spec_.kind!="salary") { exportFile("all"); return; }
            QMenu menu(exportButton); menu.setObjectName("groupContextMenu");
            auto *current=menu.addAction("导出当前页"), *all=menu.addAction("导出全部"),
                 *merged=menu.addAction("导出合并后");
            QAction *chosen=menu.exec(exportButton->mapToGlobal(QPoint(0,exportButton->height()+2)));
            if(chosen==current) exportFile("current");
            else if(chosen==all) exportFile("all");
            else if(chosen==merged) exportFile("merged");
        });
        table_=new ClientTable; table_->setColumns(spec_.columns);
        table_->setMinimumHeight(300); tableLayout->addWidget(table_,1);
        pages_=new ClientPagination; tableLayout->addWidget(pages_);
        connect(pages_,&ClientPagination::changed,this,&StatisticsPage::renderTable);
        if(spec_.kind=="salary") connect(table_,&QTableWidget::cellDoubleClicked,this,[this](int row,int) {
            showSalaryDetail(table_->recordAt(row));
        });
        tableCard->setMinimumHeight(400); layout->addWidget(tableCard,1);
        scroll->setWidget(content); outer->addWidget(scroll,1);
    }
    void refresh() override {
        if(api_->token()=="preview-token") return;
        if(tree_->topLevelItemCount()==0) api_->get("/device/tree",[this](const ApiResponse &result) {
            if(!result.ok) return;
            tree_->clear();
            for(const QJsonValue &value:result.data.toArray()) addNode(nullptr,value.toObject());
            tree_->expandAll();
        });
        if(spec_.kind=="salary" && devices_.isEmpty())
            api_->get("/device/list?fullModel=1",[this](const ApiResponse &result) {
                if(result.ok) devices_=apiRows(result.data);
            });
        fetchData();
    }
private:
    QStringList idsFor(const QJsonObject &node) const {
        QStringList ids;
        if(node.value("type").toString()=="device") ids << QString::number(node.value("id").toInteger());
        for(const QJsonValue &value:node.value("children").toArray()) ids+=idsFor(value.toObject());
        ids.removeDuplicates(); return ids;
    }
    void addNode(QTreeWidgetItem *parent,const QJsonObject &node) {
        auto *item=parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
        item->setText(0,node.value("id").toString()=="all" ? "全部分组" :
            node.value("label").toString(node.value("name").toString()));
        if(node.value("type").toString()=="device") {
            item->setIcon(0,deviceStateIcon(node.value("status").toString()));
            item->setToolTip(0,node.value("label").toString(node.value("name").toString()));
        }
        const QString joined=idsFor(node).join(',');
        item->setData(0,Qt::UserRole,node.value("id").toString()=="all" ? QString() : joined.isEmpty() ? QString("0") : joined);
        item->setData(0,Qt::UserRole+1,node.value("employeeCode").toString());
        item->setData(0,Qt::UserRole+2,node.value("employeeName").toString());
        item->setData(0,Qt::UserRole+3,node.value("type").toString());
        if(node.value("id").toString()=="ungrouped") item->setForeground(0,QColor("#f56c6c"));
        if(clientTreeBusinessLabel(node))
            item->setData(0,UiI18n::PreserveDataRole,true);
        for(const QJsonValue &value:node.value("children").toArray()) addNode(item,value.toObject());
    }
    QMap<QString,QString> query() const {
        QMap<QString,QString> params;
        if(dates_->startDate().isValid()) params["startDate"]=dates_->startDate().toString("yyyy-MM-dd");
        if(dates_->endDate().isValid()) params["endDate"]=dates_->endDate().toString("yyyy-MM-dd");
        if(!selectedIds_.isEmpty()) params["deviceIds"]=selectedIds_;
        if(spec_.kind=="salary") {
            if(employee_->currentData().toLongLong()>0)
                params["employeeId"]=employee_->currentData().toString();
            if(!keyword_->text().trimmed().isEmpty()) params["employeeKeyword"]=keyword_->text().trimmed();
            if(deviceKeyword_ && !deviceKeyword_->text().trimmed().isEmpty())
                params["deviceName"]=deviceKeyword_->text().trimmed();
            if(allowInitialFallback_) params["fallbackLatest"]="1";
        } else if(spec_.kind=="alarm" && !alarmType_->currentData().toString().isEmpty())
            params["alarmType"]=alarmType_->currentData().toString();
        if(spec_.kind!="salary" && !keyword_->text().trimmed().isEmpty())
            params["deviceName"]=keyword_->text().trimmed();
        return params;
    }
    void fetchData() {
        if(api_->token()=="preview-token") return;
        const quint64 generation=++generation_;
        const auto collected=std::make_shared<QJsonArray>();
        const auto firstData=std::make_shared<QJsonObject>();
        const QMap<QString,QString> base=query();
        allowInitialFallback_=false;
        auto load=std::make_shared<std::function<void(int)>>();
        *load=[this,generation,base,collected,firstData,load](int page) {
            QMap<QString,QString> params=base;
            params["page"]=QString::number(page); params["pageSize"]="200";
            api_->get("/statistics/"+spec_.kind+requestQuery(params),
                [this,generation,base,collected,firstData,load,page](const ApiResponse &result) {
                    if(generation!=generation_) return;
                    if(!result.ok) { window_->notify(result.error,true); return; }
                    const QJsonObject data=result.data.toObject();
                    if(page==1) *firstData=data;
                    const QJsonArray chunk=data.value("list").toArray();
                    for(const QJsonValue &value:chunk) collected->append(value);
                    const int total=data.value("total").toInt();
                    if(!chunk.isEmpty() && collected->size()<total && page<50) { (*load)(page+1); return; }
                    allRows_=*collected; lastData_=*firstData;
                    if(spec_.kind=="salary") updateEmployeeOptions();
                    const QJsonObject fallback=lastData_.value("fallbackRange").toObject();
                    if(spec_.kind=="salary" && !fallback.isEmpty())
                        dates_->setRange(QDate::fromString(fallback.value("startDate").toString(),"yyyy-MM-dd"),
                            QDate::fromString(fallback.value("endDate").toString(),"yyyy-MM-dd"));
                    renderTable();
                    if(spec_.kind!="salary") {firstChart_->setSeries(lastData_.value(spec_.firstSeries).toArray(),
                        spec_.firstKind==ClientChart::Pie ? "name" : spec_.kind=="salary" ? "name" : "date",spec_.firstValue);
                    secondChart_->setSeries(lastData_.value(spec_.secondSeries).toArray(),"date",spec_.secondValue);}
                    if(spec_.kind=="alarm") {
                        const QString current=alarmType_->currentData().toString();
                        alarmType_->clear(); alarmType_->addItem("全部类型",QString());
                        for(const QJsonValue &value:lastData_.value("alarmTypePie").toArray()) {
                            const QString name=value.toObject().value("name").toString();
                            alarmType_->addItem(name,name);
                            alarmType_->setItemData(alarmType_->count()-1,true,UiI18n::PreserveDataRole);
                        }
                        const int at=alarmType_->findData(current); if(at>=0) alarmType_->setCurrentIndex(at);
                    }
                });
        };
        (*load)(1);
    }
    void renderTable() {
        const QJsonArray filtered=filteredRows();
        pages_->setTotal(filtered.size());
        QJsonArray page;
        const int start=(pages_->page()-1)*pages_->pageSize();
        for(int i=start;i<qMin(start+pages_->pageSize(),filtered.size());++i) page.append(filtered[i]);
        if(spec_.kind=="salary") {
            qint64 pieces=0; double salary=0,amount=0;
            for(const QJsonValue &value:page) {
                const QJsonObject row=value.toObject();
                pieces+=row.value("totalPieces").toInteger();
                salary+=row.value("salary").toDouble();
                amount+=row.value("totalAmount").toDouble();
            }
            table_->setSummary({{"#","合计"},{"totalPieces",pieces},
                {"salary",QString::number(salary,'f',2)},
                {"totalAmount",QString::number(amount,'f',2)}});
        }
        table_->setRecords(page,start);
        const QJsonObject summary=lastData_.value("summary").toObject();
        if(spec_.kind=="salary") {
            double totalSalary=0; qint64 pieces=0; QSet<QString> employees;
            QMap<qint64,QPair<QString,double>> employeeAmounts;QMap<QString,double> dateAmounts;
            for(const QJsonValue &value:filtered) {
                const QJsonObject row=value.toObject();
                totalSalary+=row.value("salary").toDouble(row.value("totalAmount").toDouble());
                pieces+=row.value("totalPieces").toInteger();
                employees.insert(row.value("employeeId").toVariant().toString());
                const auto id=row.value("employeeId").toInteger();
                employeeAmounts[id].first=row.value("employeeName").toString();employeeAmounts[id].second+=row.value("salary").toDouble();
                dateAmounts[row.value("date").toString()]+=row.value("salary").toDouble();
            }
            metrics_[0]->setText(QString::number(totalSalary,'f',2));
            metrics_[1]->setText(QString::number(pieces));
            metrics_[2]->setText(QString::number(employees.isEmpty() ? 0 : totalSalary/employees.size(),'f',2));
            auto ranked=employeeAmounts.values();std::sort(ranked.begin(),ranked.end(),[](const auto &a,const auto &b){return a.second>b.second;});
            QJsonArray rank,trend;for(const auto &amount:ranked)rank.append(QJsonObject{{"name",amount.first},{"value",qRound64(amount.second*100.0)/100.0}});
            for(auto it=dateAmounts.cbegin();it!=dateAmounts.cend();++it)trend.append(QJsonObject{{"date",it.key()},{"value",qRound64(it.value()*100.0)/100.0}});
            firstChart_->setSeries(rank,"name","value");secondChart_->setSeries(trend,"date","value");
        } else for(int i=0;i<metrics_.size();++i) {
            const QString key=spec_.metrics[i].second;
            const QJsonValue value=summary.contains(key) ? summary.value(key) : lastData_.value(key);
            const bool percent=key=="avgEfficiency" || key=="resolvedRate";
            metrics_[i]->setText(value.toVariant().toString()+(percent ? "%" : QString()));
        }
    }
    QJsonArray filteredRows() const {
        QJsonArray filtered;
        const QString keyword=spec_.kind=="salary" ?
            (deviceKeyword_ ? deviceKeyword_->text().trimmed() : QString()) : keyword_->text().trimmed();
        bool minValid=false,maxValid=false;
        const double minimum=salaryMin_ ? salaryMin_->text().toDouble(&minValid) : 0;
        const double maximum=salaryMax_ ? salaryMax_->text().toDouble(&maxValid) : 0;
        for(const QJsonValue &value:allRows_) {
            const QJsonObject row=value.toObject();
            const QString name=row.value("deviceName").toString();
            if(!keyword.isEmpty() && !name.contains(keyword,Qt::CaseInsensitive) &&
               (name.isEmpty() || !keyword.contains(name,Qt::CaseInsensitive))) continue;
            if(spec_.kind=="salary") {
                const double amount=row.value("salary").toDouble(row.value("totalAmount").toDouble());
                if(minValid && amount<minimum) continue;
                if(maxValid && amount>maximum) continue;
            }
            filtered.append(value);
        }
        return filtered;
    }
    void updateEmployeeOptions() {
        const QVariant selected=employee_->currentData();
        QSignalBlocker blocked(employee_);
        employee_->clear(); employee_->addItem("全部员工",QVariant());
        QSet<qint64> seen;
        for(const QJsonValue &value:allRows_) {
            const QJsonObject row=value.toObject();
            const qint64 id=row.value("employeeId").toInteger();
            if(id<1 || seen.contains(id)) continue;
            seen.insert(id);
            employee_->addItem(row.value("employeeName").toString(),id);
            employee_->setItemData(employee_->count()-1,true,UiI18n::PreserveDataRole);
        }
        int index=employee_->findData(selected);
        if((index<1) && tree_->currentItem() &&
           tree_->currentItem()->data(0,Qt::UserRole+3).toString()=="device") {
            const QString code=tree_->currentItem()->data(0,Qt::UserRole+1).toString().trimmed();
            const QString name=tree_->currentItem()->data(0,Qt::UserRole+2).toString().trimmed();
            for(const QJsonValue &value:allRows_) {
                const QJsonObject row=value.toObject();
                if((!code.isEmpty() && row.value("employeeCode").toString()==code) ||
                   (!name.isEmpty() && row.value("employeeName").toString()==name)) {
                    index=employee_->findData(row.value("employeeId").toVariant());
                    break;
                }
            }
        }
        employee_->setCurrentIndex(index>=0 ? index : 0);
    }
    void exportFile(const QString &mode) {
        QString target=ClientFileDialog::getSaveFileName(this,"导出 Excel",spec_.title+".xlsx","Excel (*.xlsx)");
        if(target.isEmpty()) return;
        window_->notify("正在导出 Excel...");
        if(!target.endsWith(".xlsx",Qt::CaseInsensitive)) target+=".xlsx";
        const auto temporary=std::make_shared<QTemporaryDir>();
        if(!temporary->isValid()) { window_->notify("无法创建临时文件",true); return; }
        const QString csvPath=temporary->filePath("statistics.csv");
        if(spec_.kind=="salary") {
            QJsonArray rows=filteredRows();
            if(mode=="current") {
                QJsonArray page;
                const int first=(pages_->page()-1)*pages_->pageSize();
                for(int i=first;i<qMin(first+pages_->pageSize(),rows.size());++i) page.append(rows[i]);
                rows=page;
            } else if(mode=="merged") {
                QMap<QString,QJsonObject> grouped;
                for(const QJsonValue &value:rows) {
                    const QJsonObject row=value.toObject();
                    const QString key=row.value("employeeCode").toString()+"|"+
                        row.value("deviceId").toVariant().toString()+"|"+
                        row.value("unitPrice").toVariant().toString();
                    QJsonObject item=grouped.value(key,row);
                    if(grouped.contains(key)) for(const QString &field:{"totalPieces","salary","totalAmount"})
                        item[field]=item.value(field).toDouble()+row.value(field).toDouble();
                    grouped[key]=item;
                }
                rows={}; for(const QJsonObject &row:grouped) rows.append(row);
            }
            const QStringList fields=mode=="merged" ?
                QStringList{"employeeCode","employeeName","deviceName","totalPieces","unitPrice","salary","totalAmount"} :
                QStringList{"employeeCode","employeeName","deviceName","patternName","orderNo","totalPieces","unitPrice","salary","totalAmount","date"};
            const QStringList headers=mode=="merged" ?
                QStringList{"员工工号","员工姓名","设备名称","加工件数","单价(元)","工资(元)","合计(元)"} :
                QStringList{"员工工号","员工姓名","设备名称","花型名称","订单号","加工件数","单价(元)","工资(元)","合计(元)","日期"};
            auto quote=[](QString text){ text.replace('"',"\"\""); return "\""+text+"\""; };
            QSaveFile csv(csvPath);
            if(!csv.open(QIODevice::WriteOnly)) { window_->notify("导出失败",true); return; }
            csv.write("\xEF\xBB\xBF"+(headers.join(',')+'\n').toUtf8());
            for(const QJsonValue &value:rows) {
                QStringList cells; const QJsonObject row=value.toObject();
                for(const QString &field:fields) cells << quote(row.value(field).toVariant().toString());
                csv.write((cells.join(',')+'\n').toUtf8());
            }
            if(!csv.commit()) { window_->notify("导出失败",true); return; }
            QString error;
            window_->notify(csvToXlsx(csvPath,target,&error) ? "导出成功" : error,!error.isEmpty());
            return;
        }
        QMap<QString,QString> params=query();
        api_->downloadToFile("/statistics/export/"+spec_.kind+requestQuery(params),csvPath,
            [this,target,csvPath,temporary](const ApiResponse &result) {
                if(!result.ok) { window_->notify(result.error,true); return; }
                QString error;
                const bool saved=csvToXlsx(csvPath,target,&error);
                window_->notify(saved ? "导出成功" : error,!saved);
            });
    }
    void showSalaryDetail(const QJsonObject &row) {
        if(row.isEmpty()) return;
        QMap<QString,QString> params;
        params["employeeId"]=QString::number(row.value("employeeId").toInteger());
        params["date"]=row.value("date").toString();
        api_->get("/statistics/salary/detail"+requestQuery(params),[this,row](const ApiResponse &result) {
            if(!result.ok) { window_->notify(result.error,true); return; }
            QDialog dialog(this); dialog.setObjectName("editorDialog");
            dialog.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint); dialog.resize(1100,550);
            UiModalDialog::attach(&dialog,this);
            auto *layout=new QVBoxLayout(&dialog); layout->setContentsMargins(12,12,12,12);
            layout->addWidget(clientHeading(row.value("employeeName").toString()+" · 工资明细"));
            auto *detail=new ClientTable;
            detail->setColumns({{"序号","#",60},{"设备名称","deviceName",150},{"花型名称","patternName",180},
                {"花型针数","patternStitches",110},{"开始时间","startTime",175},
                {"结束时间","endTime",175},{"缝纫次数","sewCount",100},
                {"单价(元)","unitPrice",105},{"订单号","orderNo",130},{"金额(元)","totalAmount",110}});
            detail->setRecords(apiRows(result.data)); layout->addWidget(detail,1);
            auto *close=new QPushButton("关闭"); layout->addWidget(close,0,Qt::AlignRight);
            connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);
            dialog.exec();
        });
    }
    ApiClient *api_; ClientWindow *window_; StatsSpec spec_;
    QTreeWidget *tree_; DateRangeWidget *dates_; QLabel *selectionName_;
    QComboBox *employee_=nullptr,*alarmType_=nullptr;
    QLineEdit *keyword_,*deviceKeyword_=nullptr,*salaryMin_=nullptr,*salaryMax_=nullptr;
    ClientTable *table_; ClientPagination *pages_;
    ClientChart *firstChart_,*secondChart_;
    QList<QLabel*> metrics_; QString selectedIds_; quint64 generation_=0;
    QJsonArray allRows_; QJsonObject lastData_;
    QJsonArray devices_;
    bool allowInitialFallback_=true;
};
}

ClientPage *createStatisticsPage(const QString &path,ApiClient *api,ClientWindow *window) {
    if(path.startsWith("/statistics/")) return new StatisticsPage(api,window,path);
    return nullptr;
}
