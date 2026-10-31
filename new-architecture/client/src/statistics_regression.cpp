#include "login_regression.h"
#include "client_window.h"
#include "page_widgets.h"
#include "ui_controls.h"
#include "ui_i18n.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDate>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <memory>

int runStatisticsRegression(QApplication &app,const QString &host,int port) {
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());UiI18n::instance()->setEnglish(false);
    QFile log(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG"));if(!log.open(QIODevice::Append))return 2;int failures=0;
    auto record=[&](int row,const QString &name,bool passed) {
        log.write(QJsonDocument(QJsonObject{{"row",row},{"name",name},{"result",passed ? "PASS" : "FAIL"},{"scope","Qt客户端实际统计页面+真实Qt服务端；两已分组设备和一未分组设备/两员工/15件/30元/3报警已知临时数据"}}).toJson(QJsonDocument::Compact)+'\n');log.flush();qInfo("ROW %d %s %s",row,passed ? "PASS" : "FAIL",qPrintable(name));failures+=!passed;
    };
    auto wait=[&](std::function<bool()> condition,int timeout=4000) {QElapsedTimer t;t.start();while(!condition() && t.elapsed()<timeout){app.processEvents();QThread::msleep(2);}return condition();};
    ClientWindow window;window.show();auto *api=window.api();api->setServer(host,port);
    auto response=std::make_shared<ApiResponse>();auto done=std::make_shared<bool>(false);
    api->post("/auth/login",QJsonObject{{"username","admin"},{"password","admin123"}},[=](const ApiResponse &value){*response=value;*done=true;});if(!wait([&]{return *done;}) || !response->ok)return 3;
    QHash<QString,int> replies;QObject::connect(api,&ApiClient::requestObserved,&window,[&](const QString &path,int,bool){++replies[path];});
    window.testSession(host,port,response->data.toObject().value("user").toObject(),response->data.toObject().value("token").toString());
    auto navigate=[&](const QString &path){const QString endpoint="/api"+path;const int previous=replies.value(endpoint);window.navigate(path);return wait([&]{return replies.value(endpoint)>previous;});};
    auto table=[&] {for(auto *widget:window.findChildren<QTableWidget*>())if(auto *candidate=dynamic_cast<ClientTable*>(widget))if(candidate->isVisible())return candidate;return static_cast<ClientTable*>(nullptr);};
    auto dataCount=[&](ClientTable *t){int count=0;if(t)for(int r=0;r<t->rowCount();++r)count+=!t->recordAt(r).isEmpty();return count;};
    auto input=[&](const QString &placeholder){for(auto *line:window.findChildren<QLineEdit*>())if(line->isVisible() && line->placeholderText()==placeholder)return line;return static_cast<QLineEdit*>(nullptr);};
    auto search=[&] {const QString endpoint="/api"+window.currentPath();const int previous=replies.value(endpoint);for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()=="搜索"){button->click();return wait([&]{return replies.value(endpoint)>previous;});}return false;};
    auto dates=[&] {for(auto *widget:window.findChildren<QWidget*>())if(auto *range=dynamic_cast<DateRangeWidget*>(widget))if(range->isVisible())return range;return static_cast<DateRangeWidget*>(nullptr);};
    navigate("/statistics/salary");auto *salary=table();
    record(281,"工资实际页面显示两条有收入及未分组零收入明细",dataCount(salary)==3);
    QStringList headers;for(int c=0;c<salary->columnCount();++c)headers<<salary->horizontalHeaderItem(c)->text();const auto initial=salary->recordAt(0);UiI18n::instance()->setEnglish(true);app.processEvents();bool english=true;for(int c=0;c<salary->columnCount();++c)english&=!salary->horizontalHeaderItem(c)->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));UiI18n::instance()->setEnglish(false);app.processEvents();bool restored=true;for(int c=0;c<headers.size();++c)restored&=salary->horizontalHeaderItem(c)->text()==headers[c];record(282,"有统计数据时中英往返切换表头恢复且业务记录不变",english && restored && salary->recordAt(0)==initial && dataCount(salary)==3);

    auto *range=dates();if(!range)return 4;const QDate future=QDate::currentDate().addDays(30);range->setRange(future,future);search();
    record(279,"工资页面明确搜索未来日期返回空，不偷偷切回历史日期",dataCount(salary)==0 && range->startDate()==future && range->endDate()==future);
    const QDate fixtureDate=QDate::fromString(qEnvironmentVariable("BOER_STATS_DAY"),"yyyy-MM-dd");
    range->setRange(fixtureDate,fixtureDate);search();
    auto *minimum=input("最低"),*maximum=input("最高"),*deviceName=input("支持设备名模糊筛选");if(!minimum || !maximum || !deviceName)return 5;
    minimum->setText("10");maximum->setText("15");search();
    bool ten=dataCount(salary)==1 && salary->recordAt(0).value("salary").toDouble()==10;
    record(278,"工资区间10至15仅保留10元员工明细",ten);
    ClientChart *rank=nullptr,*trend=nullptr;for(auto *widget:window.findChildren<QWidget*>())if(auto *chart=dynamic_cast<ClientChart*>(widget))if(chart->isVisible()){if(chart->property("statisticsChart")=="salary-0")rank=chart;if(chart->property("statisticsChart")=="salary-1")trend=chart;}
    auto hover=[&](ClientChart *chart,QPointF point){if(!chart)return;QMouseEvent event(QEvent::MouseMove,point,chart->mapToGlobal(point.toPoint()),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(chart,&event);};
    if(rank)hover(rank,QPointF(100,30));if(trend)hover(trend,trend->plotArea().center());
    record(278,"工资区间筛选同步更新员工排行和日工资趋势",rank && trend && rank->toolTip().contains("员工甲") && rank->toolTip().endsWith("10元") && trend->toolTip().endsWith("10元"));
    window.resize(1120,700);minimum->setText(QString(50,'9'));maximum->setText(QString(50,'8'));app.processEvents();
    auto *salaryGroup=window.findChild<QWidget*>("salaryRangeGroup");
    record(280,"超长工资区间输入被限制在控件范围，不挤压或重叠相邻输入",salaryGroup && minimum->fontMetrics().horizontalAdvance(minimum->text())>minimum->width() && minimum->geometry().right()<maximum->geometry().left() && salaryGroup->rect().contains(minimum->geometry()) && salaryGroup->rect().contains(maximum->geometry()) && window.rect().contains(QRect(salaryGroup->mapTo(&window,QPoint()),salaryGroup->size())));
    window.grab().save(QFileInfo(log.fileName()).absoluteDir().filePath("salary-range-long.png"));window.resize(1440,900);app.processEvents();
    minimum->clear();maximum->clear();deviceName->setText("哈哈哈哈");search();
    record(277,"统计设备名称按用户确认的双向包含匹配",dataCount(salary)==1 && salary->recordAt(0).value("deviceName")=="哈哈哈");
    deviceName->clear();search();
    auto verifyTree=[&](int clearRow,int nameRow,int iconRow,int allRow) {
        QTreeWidget *tree=nullptr;for(auto *candidate:window.findChildren<QTreeWidget*>("groupTree"))if(candidate->isVisible())tree=candidate;if(!tree)return;
        QTreeWidgetItem *ungrouped=nullptr,*allNode=nullptr;bool all=false,hasIcons=false;
        for(QTreeWidgetItemIterator it(tree);*it;++it){if((*it)->text(0)=="全部分组"){all=true;allNode=*it;}if((*it)->text(0)=="未分组设备")ungrouped=*it;if((*it)->data(0,Qt::UserRole+3).toString()=="device")hasIcons|=!(*it)->icon(0).isNull();}
        record(nameRow,"统计设备树使用全部分组/未分组设备清楚命名",all && ungrouped);
        record(iconRow,"统计设备树包含设备状态图标",hasIcons);
        int legend=0;for(auto *label:window.findChildren<QLabel*>("statisticsStatusLegend"))if(label->isVisible() && ((label->text()=="空闲" && label->styleSheet().contains("#67c23a")) || (label->text()=="缝纫" && label->styleSheet().contains("#f56c6c")) || (label->text()=="关机" && label->styleSheet().contains("#909399"))))++legend;
        record(iconRow,"统计页状态图例区分空闲绿、缝纫红、关机灰",legend==3);
        if(allNode){const int previous=replies.value("/api"+window.currentPath());tree->setCurrentItem(allNode);tree->itemClicked(allNode,0);wait([&]{return replies.value("/api"+window.currentPath())>previous;});}
        record(allRow,"点击全部分组仍包含未分组零产量记录，共三条明细",allNode && dataCount(table())==3);
        if(ungrouped){tree->setCurrentItem(ungrouped);tree->itemClicked(ungrouped,0);wait([&]{return dataCount(table())==1;});}
        record(clearRow,"统计页选择未分组只显示对应的零产量记录",dataCount(table())==1 && table()->recordAt(0).value("deviceName")=="未分组设备丙");
        const QString endpoint="/api"+window.currentPath();const int before=replies.value(endpoint);for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()=="清空"){button->click();break;}wait([&]{return replies.value(endpoint)>before;});
        bool label=false;for(auto *value:window.findChildren<QLabel*>("clientSectionTitle"))label|=value->isVisible() && value->text()=="全部分组";
        record(clearRow,"统计页选择未分组再清空恢复全部范围",label && dataCount(table())==3);
    };
    verifyTree(274,275,276,273);
    navigate("/statistics/process");auto *process=table();record(288,"加工概况显示15件及未分组零产量记录",dataCount(process)==3);
    auto *keyword=input("按设备名称搜索明细");if(!keyword)return 6;keyword->setText("哈哈哈");search();
    bool five=false;for(auto *label:window.findChildren<QLabel*>("clientMetric"))if(label->isVisible() && label->text()=="5")five=true;
    record(287,"加工概况按设备名搜索后明细与上方加工件数均为5",dataCount(process)==1 && five);
    keyword->clear();search();verifyTree(284,285,286,283);
    navigate("/statistics/duration");record(294,"时长统计含两台生产设备及未分组零时长记录",dataCount(table())==3);
    navigate("/statistics/alarm");auto *alarms=table();
    window.resize(1120,700);UiI18n::instance()->setEnglish(true);app.processEvents();bool filterFits=true,checked=false;for(auto *line:window.findChildren<QLineEdit*>())if(line->isVisible() && line->placeholderText()==UiI18n::instance()->text("按设备名称搜索明细")){auto *filter=line->parentWidget();checked=true;for(auto *control:filter->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))if(control->isVisible())filterFits&=filter->rect().contains(control->geometry());}record(310,"1120英文报警筛选栏自动换行，各直接控件在容器内",checked && filterFits);window.grab().save(QFileInfo(log.fileName()).absoluteDir().filePath("statistics-alarm-en-small.png"));UiI18n::instance()->setEnglish(false);window.resize(1440,900);app.processEvents();
record(307,"报警页面显示文案，隐藏单独编号列",alarms && alarms->columnForKey("alarmInfo")>=0 && alarms->columnForKey("alarmCode")==-1);record(298,"报警统计真实页面显示3次报警",dataCount(alarms)==3);
    QComboBox *alarmType=nullptr;for(auto *box:window.findChildren<QComboBox*>())if(box->isVisible() && box->findData("报警E.025")>=0)alarmType=box;
    record(297,"报警类型候选正常加载",alarmType!=nullptr);
    if(alarmType){alarmType->setCurrentIndex(alarmType->findData("报警E.025"));search();record(297,"选择报警类型后只显示一种报警",dataCount(alarms)==1);alarmType->setCurrentIndex(0);search();record(297,"清除报警类型后恢复所有候选和三条数据",dataCount(alarms)==3 && alarmType->findData("报警E.026")>=0);}
    const int initialDashboard=replies.value("/api/statistics/dashboard");
    navigate("/device/list");auto *deviceTable=table();int monitorRow=-1;for(int r=0;deviceTable && r<deviceTable->rowCount();++r)if(deviceTable->recordAt(r).value("id").toInteger()==61)monitorRow=r;
    auto *frozen=deviceTable ? deviceTable->findChild<QTableWidget*>("clientFrozenActions") : nullptr;QPushButton *monitor=nullptr;
    if(frozen && monitorRow>=0)if(auto *actions=frozen->cellWidget(monitorRow,0))for(auto *button:actions->findChildren<QPushButton*>())if(button->text()=="监控")monitor=button;
    const int beforeDevices=replies.value("/api/device/list");if(monitor)monitor->click();
    record(118,"设备列表监控按钮打开监控页面",monitor && window.currentPath()=="/device/monitor");
    wait([&]{return replies.value("/api/device/list")>=beforeDevices+2;});
    wait([&]{return replies.value("/api/statistics/dashboard")>initialDashboard;});
    QTreeWidget *monitorTree=nullptr;for(auto *tree:window.findChildren<QTreeWidget*>("groupTree"))if(tree->isVisible())monitorTree=tree;
    QTreeWidgetItem *monitorDevice=nullptr;
    wait([&]{if(monitorTree)for(QTreeWidgetItemIterator it(monitorTree);*it;++it)if((*it)->data(0,Qt::UserRole+1)=="device" && (*it)->data(0,Qt::UserRole).toLongLong()==61)monitorDevice=*it;return monitorDevice!=nullptr;});
    if(!monitorDevice)return 7;
    record(118,"从设备行进入监控时自动选中对应设备",monitorTree->currentItem()==monitorDevice);
    bool rpm=false,needles=false,pattern=false;for(auto *label:window.findChildren<QLabel*>("clientMetric"))if(label->isVisible()){rpm|=label->text()=="2000 RPM";needles|=label->text()=="12345";pattern|=label->text()=="监控花型";}
    record(118,"监控实际页面保留实时速度2000RPM与累计针数12345，不被历史统计覆盖",rpm && needles && pattern);
    // Exercise deletion through the actual client, then revisit history through its UI.
    navigate("/employee/list");auto *employees=table();
    if(!employees || !wait([&]{return dataCount(employees)==2;}))return 8;
    int employeeRow=-1;for(int row=0;row<employees->rowCount();++row)if(employees->recordAt(row).value("id").toInteger()==71)employeeRow=row;
    if(employeeRow<0)return 9;
    employees->cellClicked(employeeRow,employees->columnForKey("code"));
    QPushButton *removeEmployees=nullptr;for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()=="批量删除")removeEmployees=button;
    if(!removeEmployees)return 10;
    bool confirmed=false;QTimer::singleShot(30,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(dialog && dialog->windowTitle()=="批量删除"){confirmed=true;dialog->findChild<QPushButton*>("primaryButton")->click();}});removeEmployees->click();
    record(322,"补测客户端实际批量删除一名有历史记录员工，确认后列表仅剩一名",confirmed && wait([&]{return dataCount(employees)==1;}));
    navigate("/statistics/salary");range=dates();if(!range)return 11;range->setRange(fixtureDate,fixtureDate);search();salary=table();
    bool historical=false;double salarySum=0;int piecesSum=0;
    if(salary)for(int row=0;row<salary->rowCount();++row){const auto record=salary->recordAt(row);if(record.isEmpty())continue;salarySum+=record.value("salary").toDouble();piecesSum+=record.value("totalPieces").toInt();historical|=record.value("employeeCode")=="0001001" && record.value("employeeName")=="员工甲";}
    record(322,"补测客户端删除后工资页仍显示历史工号姓名、15件30元",historical && piecesSum==15 && salarySum==30);
    window.grab().save(QFileInfo(log.fileName()).absoluteDir().filePath("employee-deleted-history-salary.png"));
    QPushButton *exportSalary=nullptr;for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()=="导出 Excel")exportSalary=button;
    if(!exportSalary)return 12;
    for(const QString &mode:{QString("all"),QString("merged")}) {
        const QString target=QFileInfo(log.fileName()).absoluteDir().filePath("supplement-history-"+mode+".xlsx");QFile::remove(target);
        const QString actionText=mode=="all" ? "导出全部" : "导出合并后";
        QTimer::singleShot(30,&window,[&,target,actionText]{auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)qFatal("salary export menu missing");QAction *chosen=nullptr;for(auto *action:menu->actions())if(action->text()==actionText)chosen=action;if(!chosen)qFatal("salary export mode missing");QTimer::singleShot(30,&window,[&,target]{auto *dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("salary export file dialog missing");dialog->selectFile(target);QMetaObject::invokeMethod(dialog,"accept");});menu->setActiveAction(chosen);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);});
        exportSalary->click();record(322,"补测员工删除后客户端实际导出历史工资文件："+mode,wait([&]{return QFileInfo(target).size()>0;}));
    }
    return failures ? 1 : 0;
}
