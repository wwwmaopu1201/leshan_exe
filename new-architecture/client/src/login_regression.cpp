#include "ui_settings.h"
#include "login_regression.h"
#include "client_window.h"
#include "page_widgets.h"
#include "ui_controls.h"
#include "ui_i18n.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QDir>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QScrollArea>
#include <QScrollBar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <functional>

int runLoginRegression(QApplication &app) {
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsDir.path());
    UiI18n::instance()->setEnglish(false);
    QFile log(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG"));
    if(!log.open(QIODevice::WriteOnly|QIODevice::Append)) return 2;
    int failures=0,loginRequests=0;
    bool homeFixture=false,homeFailure=false,rankingFixture=false;
    QString deviceStatus="working";
    auto record=[&](int row,const QString &name,bool passed) {
        QJsonObject item{{"row",row},{"name",name},{"result",passed ? "PASS" : "FAIL"},
            {"scope","Qt客户端登录界面；本机HTTP替身；独立临时QSettings"}};
        log.write(QJsonDocument(item).toJson(QJsonDocument::Compact)+'\n'); log.flush();
        qInfo("ROW %d %s %s",row,passed ? "PASS" : "FAIL",qPrintable(name));
        if(!passed) ++failures;
    };
    auto wait=[&](std::function<bool()> condition,int timeout=2000) {
        QElapsedTimer timer; timer.start();
        while(!condition() && timer.elapsed()<timeout) { app.processEvents(); QThread::msleep(2); }
        return condition();
    };
    auto dashboardDevice=[](QTreeWidget *tree){if(tree)for(QTreeWidgetItemIterator it(tree);*it;++it)if(QStringList{"working","idle","offline"}.contains((*it)->data(0,Qt::UserRole+2).toString()))return *it;return static_cast<QTreeWidgetItem*>(nullptr);};
    QTcpServer server;
    if(!server.listen(QHostAddress::LocalHost,0)) return 3;
    QObject::connect(&server,&QTcpServer::newConnection,&server,[&] {
        while(server.hasPendingConnections()) {
            auto *socket=server.nextPendingConnection();
            auto bytes=std::make_shared<QByteArray>();
            QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket,bytes] {
                bytes->append(socket->readAll());
                const int split=bytes->indexOf("\r\n\r\n"); if(split<0) return;
                int length=0;
                for(auto line:bytes->left(split).split('\n'))
                    if(line.toLower().startsWith("content-length:")) length=line.mid(15).trimmed().toInt();
                if(bytes->size()<split+4+length) return;
                const bool login=bytes->startsWith("POST /api/auth/login ");
                QJsonObject body; int status=200;
                if(login) {
                    ++loginRequests;
                    const auto request=QJsonDocument::fromJson(bytes->mid(split+4,length)).object();
                    const QString account=request.value("username").toString(),password=request.value("password").toString();
                    if(account=="stall") return;
                    const bool accepted=(account=="A" && password=="aaaaaa") || (account=="B" && password=="bbbbbb");
                    status=accepted ? 200 : 401;
                    body={{"code",accepted ? 0 : 401},{"message",accepted ? "success" : "账号或密码错误"},
                        {"data",accepted ? QJsonObject{{"token","fixture-token"},{"user",QJsonObject{{"id",1},
                            {"username",account},{"role","admin"},{"permissions","{}"}}}} : QJsonObject{}}};
                } else if(homeFixture && bytes->startsWith("GET /api/statistics/home ")) {
                    status=homeFailure ? 503 : 200;
                    body={{"code",homeFailure ? 503 : 0},{"data",QJsonObject{
                        {"totalDevices",1},{"onlineDevices",1},{"offlineDevices",0},
                        {"deviceUsage",QJsonArray{QJsonObject{{"deviceId",1},{"name",QString(60,QChar(0x8bbe))},
                            {"status",deviceStatus},{"runningTime",1},{"processingTime",0},{"idleTime",1},{"efficiency",0}}}},
                        {"patternUsage",QJsonArray{QJsonObject{{"name",QString(80,QChar(0x82b1))},{"value",1}}}}}}};
                    if(rankingFixture) {
                        auto data=body.value("data").toObject(); auto usage=data.value("deviceUsage").toArray();
                        usage.append(QJsonObject{{"deviceId",2},{"name","关机测试设备"},{"status","offline"},{"runningTime",1},
                            {"processingTime",1},{"idleTime",0},{"efficiency",100}});
                        data["deviceUsage"]=usage; body["data"]=data;
                    }
                } else if(homeFixture && bytes->startsWith("GET /api/device/tree ")) {
                    const QJsonObject device{{"id",1},{"name","测试设备"},{"label","测试设备"},{"type","device"},{"status",deviceStatus},{"groupId",1}};
                    const QJsonObject group{{"id",1},{"name","总分组"},{"label","总分组"},{"type","group"},{"children",QJsonArray{device}}};
                    body={{"code",0},{"data",QJsonArray{QJsonObject{{"id","all"},{"label","全部分组"},{"type","group"},{"isVirtual",true},{"children",QJsonArray{group}}},QJsonObject{{"id","ungrouped"},{"label","未分组设备"},{"type","group"},{"isVirtual",true},{"children",QJsonArray{}}}}}};
                } else if(homeFixture && bytes->startsWith("GET /api/statistics/dashboard")) {
                    body={{"code",0},{"data",QJsonObject{{"scopeDeviceCount",1},{"onlineDeviceCount",deviceStatus=="offline" ? 0 : 1}}}};
                } else if(homeFixture && bytes->startsWith("GET /api/device/list")) {
                    body={{"code",0},{"data",QJsonObject{{"list",QJsonArray{QJsonObject{{"id",1},{"code","fixture-device"},{"name","测试设备"},{"initialName","初始设备原文"},{"status",deviceStatus},{"groupId",1},{"remark",QString(255,QChar(0x5907))}}}},{"total",1}}}};
                } else body={{"code",0},{"data",QJsonObject{}}};
                const QByteArray payload=QJsonDocument(body).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 "+QByteArray::number(status)+" OK\r\nContent-Type: application/json\r\nContent-Length: "+
                    QByteArray::number(payload.size())+"\r\nConnection: close\r\n\r\n"+payload);
                socket->disconnectFromHost();
            });
            QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        }
    });
    const QString host="127.0.0.1:"+QString::number(server.serverPort());
    auto fields=[](ClientWindow &window) { return window.findChildren<QLineEdit*>("clientLoginInput"); };
    auto setup=[&](ClientWindow &window) {
        window.show(); app.processEvents(); auto inputs=fields(window);
        inputs[0]->setText(host); inputs[1]->setText("A"); inputs[2]->setText("aaaaaa");
    };
    auto submit=[](ClientWindow &window) { window.findChild<QPushButton*>("clientLoginSubmit")->click(); };
    {
        ClientWindow window; setup(window); auto inputs=fields(window);
        const int before=loginRequests; inputs[0]->setText("122. 51.79.127"); submit(window); app.processEvents();
        record(8,"IP含内部空格立即校验，保持表单且不发请求",loginRequests==before && window.currentPath().isEmpty());
        inputs[0]->setText("999.999.999.999"); submit(window); app.processEvents();
        record(7,"非法IPv4数值立即校验，无网络等待",window.findChild<QPushButton*>("clientLoginSubmit")->isEnabled());
    }
    {
        ClientWindow window; setup(window); auto inputs=fields(window);
        inputs[1]->setText("stall"); inputs[2]->setText("aaaaaa");
        QElapsedTimer timer; timer.start(); submit(window);
        bool responsive=false; QTimer::singleShot(100,&window,[&] { responsive=true; });
        const bool completed=wait([&]{return window.findChild<QPushButton*>("clientLoginSubmit")->isEnabled();},6500);
        record(9,"不可响应服务器5秒内超时，界面可响应并允许重试",completed && responsive && timer.elapsed()<6500);
        const bool recovered=wait([&] {
            auto *toast=window.findChild<QLabel*>("clientToast");
            return toast && toast->text()=="服务器连接已恢复，请重新登录" && !toast->property("error").toBool();
        },4500);record(15,"服务恢复后自动健康检查更新旧网络错误提示",recovered);record(353,"服务可达性恢复时旧网络错误自动清除，不必重启客户端",recovered);
        inputs[1]->setText("A"); inputs[2]->setText("aaaaaa"); submit(window);
        record(15,"网络请求超时后重试恢复登录",wait([&]{return !window.currentPath().isEmpty();}));
    }
    {
        ClientWindow window; setup(window); auto inputs=fields(window);
        const int before=loginRequests;
        QMetaObject::invokeMethod(inputs[0],"returnPressed",Qt::DirectConnection);
        record(13,"服务器输入框Enter可登录",wait([&]{return !window.currentPath().isEmpty();}) && loginRequests==before+1);
    }
    for(int index:{1,2}) {
        ClientWindow window; setup(window); auto inputs=fields(window); inputs[2]->setText("wrong");
        int before=loginRequests; QMetaObject::invokeMethod(inputs[index],"returnPressed",Qt::DirectConnection);
        const bool failed=wait([&]{return loginRequests>before && window.findChild<QPushButton*>("clientLoginSubmit")->isEnabled();});
        record(6,"登录失败保持表单、非模态提示，可继续输入",failed && window.currentPath().isEmpty() && !QApplication::activeModalWidget());
        inputs[2]->setText("aaaaaa"); QMetaObject::invokeMethod(inputs[index],"returnPressed",Qt::DirectConnection);
        record(13,QString("第%1输入框失败后重试Enter正常登录").arg(index+1),wait([&]{return !window.currentPath().isEmpty();}));
    }
    {
        ClientWindow window; setup(window); auto *password=fields(window)[2];
        QAction *reveal=nullptr;
        for(auto *action:password->actions()) if(action->objectName()=="passwordVisibility") reveal=action;
        bool passed=false;
        if(reveal) {
            const auto before=reveal->icon().cacheKey(); reveal->trigger();
            passed=password->echoMode()==QLineEdit::Normal && reveal->icon().cacheKey()!=before;
            reveal->trigger(); passed=passed && password->echoMode()==QLineEdit::Password;
        }
        record(12,"密码明暗文切换及图标状态变化",passed);
    }
    for(const QString &account:{QString("A"),QString("B")}) {
        ClientWindow window; setup(window); auto inputs=fields(window); inputs[1]->setText(account);
        inputs[2]->setText(account=="A" ? "aaaaaa" : "bbbbbb");
        auto *remember=window.findChild<QCheckBox*>(); remember->setChecked(true);
        submit(window); record(11,"账号"+account+"勾选记住密码后登录成功",wait([&]{return !window.currentPath().isEmpty();}));
    }
    {
        ClientWindow window; setup(window); auto inputs=fields(window);
        inputs[1]->setText("B"); inputs[1]->setText("A"); app.processEvents();
        record(11,"切换账号A恢复A密码",inputs[2]->text()=="aaaaaa");
        inputs[1]->setText("B"); app.processEvents();
        record(11,"切换账号B恢复B密码",inputs[2]->text()=="bbbbbb");
        record(10,"已登录服务器和账号历史包含两账号",inputs[0]->completer()!=nullptr && inputs[1]->completer()!=nullptr &&
            uiSettings("QtClient").value("accountHistory").toStringList().contains("A") &&
            uiSettings("QtClient").value("accountHistory").toStringList().contains("B"));
        record(14,"重启客户端仅回到登录页，不复用Token进入首页",window.currentPath().isEmpty() && window.api()->token().isEmpty());
    }
    {
        homeFixture=true;
        ClientWindow window; window.show();
        window.testSession("127.0.0.1",server.serverPort(),QJsonObject{{"id",1},{"role","admin"}},"fixture-token");
        ClientTable *usage=nullptr,*patterns=nullptr;
        for(auto *widget:window.findChildren<QTableWidget*>()) {
            auto *table=dynamic_cast<ClientTable*>(widget); if(!table) continue;
            if(table->columnForKey("runningTimeLabel")>=0) usage=table;
            if(table->columnForKey("value")>=0) patterns=table;
        }
        const bool loaded=wait([&]{return usage && usage->rowCount()==1 && patterns && patterns->rowCount()==1;});
        auto *version=window.findChild<QLabel*>("clientServerTag");
        auto *productTitle=window.findChild<QLabel*>("clientTopTitle");
        record(16,"软件名称后显示客户端版本，语言切换前显示服务器版本",productTitle && productTitle->text().endsWith("v"+QCoreApplication::applicationVersion()) && version && version->text().contains("服务器版本"));
        record(17,"服务器地址包含管理端口，设备TCP端口在提示中单独标明",version && version->text().contains("服务器地址") && version->text().contains(QString::number(server.serverPort())) && version->toolTip().contains("设备TCP端口: 38400"));
        record(19,"首页表格待机设备运行0小时、待机1小时",loaded && usage->recordAt(0).value("runningTimeLabel").toString()=="0小时0分" && usage->recordAt(0).value("idleTimeLabel").toString()=="1小时0分");
        for(const QString &mode:{QString("设备组"),QString("生产线")}) {
            for(auto *button:window.findChildren<QPushButton*>()) if(button->isVisible() && button->text()==mode) button->click();
            app.processEvents();
            record(21,mode+"待机设备汇总运行0小时、待机1小时",usage && usage->rowCount()==1 && usage->recordAt(0).value("runningTimeLabel").toString()=="0小时0分" && usage->recordAt(0).value("idleTimeLabel").toString()=="1小时0分");
        }
        record(27,"超长花型名称可Hover查看原文",loaded && patterns->item(0,patterns->columnForKey("name"))->toolTip()==QString(80,QChar(0x82b1)));
        record(24,"实时花型表头与数据列对齐方式一致",loaded &&
            (patterns->horizontalHeaderItem(1)->textAlignment()&Qt::AlignHorizontal_Mask)==(patterns->item(0,1)->textAlignment()&Qt::AlignHorizontal_Mask) &&
            (patterns->horizontalHeaderItem(2)->textAlignment()&Qt::AlignHorizontal_Mask)==(patterns->item(0,2)->textAlignment()&Qt::AlignHorizontal_Mask));
        bool tooltip=false;
        for(auto *label:window.findChildren<QLabel*>()) if(label->toolTip()==QString(60,QChar(0x8bbe)) && label->text().size()<=60) tooltip=true;
        record(29,"效率排名长设备名具有完整Hover文本",tooltip);
        bool zero=true; auto bars=window.findChildren<QProgressBar*>("rankingHigh");
        for(auto *bar:bars) zero=zero && bar->value()==0;
        record(30,"零使用率进度条值为零、无彩色填充",!bars.isEmpty() && zero);
        UiI18n::instance()->setEnglish(true); window.navigate("/home");
        const bool english=wait([&]{return usage && !usage->recordAt(0).value("runningTimeLabel").toString().contains("小时");});
        record(50,"首页时分单位在英文下刷新后使用英文",english);
        for(const QSize size:{QSize(1000,650),QSize(1120,700)}) {window.resize(size);app.processEvents();bool fits=true;for(auto *scroll:window.findChildren<QScrollArea*>())if(scroll->isVisible())fits&=scroll->horizontalScrollBar()->maximum()==0;record(49,QString("首页英文%1宽长设备名不撑出横向页面滚动").arg(size.width()),fits);}

        record(51,"首页设备使用率表头与设备切换按钮均完成翻译，业务设备名保留原文",usage->horizontalHeaderItem(usage->columnForKey("name"))->text()==UiI18n::instance()->text("设备名称") && UiI18n::instance()->text("设备")!="设备");
        UiI18n::instance()->setEnglish(false);
        for(auto *button:window.findChildren<QPushButton*>()) if(button->isVisible() && button->text()=="设备") button->click();
        rankingFixture=true; window.navigate("/home");
        const bool bothLoaded=wait([&]{return usage->rowCount()==2;});
        bool onlyOnline=bothLoaded && window.findChildren<QProgressBar*>("rankingHigh").size()==1 && window.findChildren<QProgressBar*>("rankingLow").size()==1;
        for(auto *label:window.findChildren<QLabel*>()) if(label->isVisible() && label->text()=="关机测试设备") onlyOnline=false;
        record(32,"用户确认：100%效率的关机设备不进入高低排名",onlyOnline);
        homeFailure=true; window.navigate("/home");
        record(18,"请求失败时清除首页旧设备和花型数据",wait([&]{return usage->rowCount()==0 && patterns->rowCount()==0;}));
        bool empty=false;
        for(auto *label:window.findChildren<QLabel*>()) if(label->text()=="暂无数据") empty=true;
        record(31,"效率排名空数据显示暂无数据",empty);
        auto *networkToast=window.findChild<QLabel*>("clientToast");record(342,"中文请求失败提示不暴露英文Qt错误串和接口URL",networkToast && networkToast->text()=="服务器暂时不可用，请稍后重试");
        window.resize(1440,1000);app.processEvents();window.grab().save(QFileInfo(log.fileName()).absoluteDir().filePath("home-network-empty.png"));
        for(auto *scroll:window.findChildren<QScrollArea*>())if(scroll->isVisible())scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());app.processEvents();window.grab().save(QFileInfo(log.fileName()).absoluteDir().filePath("home-network-empty-charts.png"));
        UiI18n::instance()->setEnglish(true);window.navigate("/home");record(342,"服务器故障提示随英文界面翻译",wait([&]{return networkToast && networkToast->text()==UiI18n::instance()->text("服务器暂时不可用，请稍后重试");}));UiI18n::instance()->setEnglish(false);app.processEvents();
        window.navigate("/dashboard");
        auto *tree=window.findChild<QTreeWidget*>("groupTree");
        const bool working=wait([&]{return tree && tree->topLevelItemCount()>0 && tree->topLevelItem(0)->childCount()>0 && dashboardDevice(tree) && dashboardDevice(tree)->data(0,Qt::UserRole+2).toString()=="working";});
        bool red=false; QLabel *legend=nullptr;
        for(auto *label:window.findChildren<QLabel*>("dashboardStatusLegend")) if(label->text()=="缝纫") { legend=label; red=label->styleSheet().contains("#f56c6c"); }
        record(52,"看板缝纫状态图例为红色，设备树有状态图标",working && red && !dashboardDevice(tree)->icon(0).isNull());
        bool sameLine=false;
        if(legend) for(auto *label:window.findChildren<QLabel*>()) if(label->text()=="设备组信息")
            sameLine=legend->mapToGlobal(QPoint(0,0)).x()>label->mapToGlobal(QPoint(0,0)).x() && qAbs(legend->mapToGlobal(QPoint(0,0)).y()-label->mapToGlobal(QPoint(0,0)).y())<30;
        record(55,"设备状态图例位于设备组信息标题右侧",sameLine);
        UiI18n::instance()->setEnglish(true);app.processEvents();bool dailyOutput=false;for(auto *label:window.findChildren<QLabel*>())if(label->isVisible() && label->text()==UiI18n::instance()->text("今日产量（件）"))dailyOutput=label->text()!="今日产量（件）";record(63,"看板当天产量字段随英文界面切换",dailyOutput);window.resize(1120,700);app.processEvents();bool dashboardFits=true;for(auto *scroll:window.findChildren<QScrollArea*>())if(scroll->isVisible())dashboardFits&=scroll->horizontalScrollBar()->maximum()==0;for(auto *label:window.findChildren<QLabel*>("dashboardCardTitle"))if(label->isVisible())dashboardFits&=label->width()>=label->fontMetrics().horizontalAdvance(label->text());record(64,"1120英文看板卡片标题不裁切，页面不被长字段撑出横向滚动",dashboardFits);window.grab().save(QFileInfo(log.fileName()).absoluteDir().filePath("dashboard-en-small.png"));UiI18n::instance()->setEnglish(false);app.processEvents();
        deviceStatus="idle"; window.navigate("/dashboard");
        record(53,"刷新后设备树状态从缝纫变为空闲",wait([&]{return dashboardDevice(tree)->data(0,Qt::UserRole+2).toString()=="idle";}));
        deviceStatus="offline"; window.navigate("/dashboard");
        record(54,"刷新后设备树状态与关机状态一致",wait([&]{return dashboardDevice(tree)->data(0,Qt::UserRole+2).toString()=="offline";}));
        window.navigate("/device/list"); app.processEvents();
        bool header=false;
        wait([&] {
            for(auto *table:window.findChildren<QTableWidget*>()) if(table->isVisible())
                for(int column=0;column<table->columnCount();++column)
                    if(table->horizontalHeaderItem(column) && table->horizontalHeaderItem(column)->text()=="设备编号") header=true;
            return header;
        });
        record(98,"设备列表字段统一为设备编号",header);
        int legends=0;for(auto *label:window.findChildren<QLabel*>("deviceStatusLegend"))legends+=label->isVisible();record(68,"设备列表树显示空闲、缝纫、关机三态图例",legends==3);
        ClientTable *deviceList=nullptr;for(auto *widget:window.findChildren<QTableWidget*>())if(auto *candidate=dynamic_cast<ClientTable*>(widget))if(candidate->isVisible() && candidate->columnForKey("status")>=0)deviceList=candidate;
        if(!deviceList)return 4;
        for(const auto &state:QList<QPair<QString,QString>>{{"working","#f56c6c"},{"idle","#67c23a"},{"offline","#909399"}}) {
            deviceStatus=state.first;window.navigate("/device/list");const bool loaded=wait([&]{return deviceList->recordAt(0).value("status")==state.first;});
            const auto *cell=deviceList->item(0,deviceList->columnForKey("status"));
            record(466,"客户端设备状态颜色与服务端和图例一致："+state.first,loaded && cell && cell->foreground().color().name()==state.second);
        }
        const auto originalName=deviceList->item(0,deviceList->columnForKey("initialName"))->text();
        UiI18n::instance()->setEnglish(true);app.processEvents();
        window.resize(1120,700);app.processEvents();
        auto *tag=window.findChild<QLabel*>("clientServerTag");bool fits=tag && !tag->wordWrap() && tag->height()>=tag->fontMetrics().height()*2+8;
        if(tag)for(const auto &line:tag->text().split('\n'))fits=fits && tag->width()>=tag->fontMetrics().horizontalAdvance(line)+20;
        if(tag && !fits){qInfo("HEADER width=%d height=%d fontHeight=%d",tag->width(),tag->height(),tag->fontMetrics().height());for(const auto &line:tag->text().split('\n'))qInfo("HEADER lineWidth=%d",tag->fontMetrics().horizontalAdvance(line));}
        record(119,"1120宽英文窗口顶栏版本信息两行完整容纳",fits);
        record(120,"切换英文保留初始设备名称原文",deviceList->item(0,deviceList->columnForKey("initialName"))->text()==originalName && originalName=="初始设备原文");
        QComboBox *statusFilter=nullptr;for(auto *box:window.findChildren<QComboBox*>())if(box->isVisible() && box->findData("working")>=0)statusFilter=box;
        if(statusFilter)statusFilter->setCurrentIndex(statusFilter->findData("working"));
        record(122,"英文状态筛选选择后回显且保留working枚举",statusFilter && statusFilter->currentData()=="working" && statusFilter->currentText()!=QString("缝纫"));
        UiI18n::instance()->setEnglish(false);app.processEvents();
        record(120,"切回中文初始设备名称不被改写",deviceList->item(0,deviceList->columnForKey("initialName"))->text()==originalName);
        record(122,"切回中文状态筛选仍回显缝纫",statusFilter && statusFilter->currentData()=="working" && statusFilter->currentText()=="缝纫");
        if(statusFilter)statusFilter->setCurrentIndex(0);
        QLabel *selection=nullptr; QTreeWidget *devicesTree=nullptr;
        for(auto *label:window.findChildren<QLabel*>("clientSectionTitle")) if(label->isVisible() && label->text()=="全部分组") selection=label;
        for(auto *candidate:window.findChildren<QTreeWidget*>("groupTree")) if(candidate->isVisible()) devicesTree=candidate;
        const bool treeLoaded=wait([&]{return devicesTree && devicesTree->topLevelItemCount()==2;});
        record(65,"设备树当前选择用全部分组表示全部数据",selection && selection->text()=="全部分组");
        record(67,"虚拟节点使用全部分组和未分组设备两个明确名称",treeLoaded && devicesTree->topLevelItem(0)->text(0)=="全部分组" && devicesTree->topLevelItem(1)->text(0)=="未分组设备");
        if(treeLoaded) {
            devicesTree->setCurrentItem(devicesTree->topLevelItem(1)); devicesTree->itemClicked(devicesTree->topLevelItem(1),0); app.processEvents();
            for(auto *button:window.findChildren<QPushButton*>()) if(button->isVisible() && button->text()=="清空") { button->click(); break; }
            app.processEvents();
        }
        record(66,"选择未分组后清空，当前选择恢复全部分组",treeLoaded && selection && selection->text()=="全部分组");
        QTreeWidgetItem *groupNode=nullptr;for(QTreeWidgetItemIterator it(devicesTree);*it;++it)if((*it)->data(0,Qt::UserRole+1)=="group" && (*it)->data(0,Qt::UserRole).toLongLong()==1)groupNode=*it;
        QLineEdit *deviceKeyword=nullptr;for(auto *line:window.findChildren<QLineEdit*>())if(line->isVisible() && line->placeholderText().contains("设备名/编号"))deviceKeyword=line;if(deviceKeyword){deviceKeyword->setText("测试设备");QMetaObject::invokeMethod(deviceKeyword,"returnPressed");}if(groupNode){devicesTree->setCurrentItem(groupNode);devicesTree->itemClicked(groupNode,0);}const bool groupLoaded=wait([&]{return deviceList->isEnabled() && deviceList->rowCount()==1 && deviceList->recordAt(0).value("groupId").toInteger()==1;});for(QTreeWidgetItemIterator it(devicesTree);*it;++it)if((*it)->data(0,Qt::UserRole+1)=="group" && (*it)->data(0,Qt::UserRole).toLongLong()==1)groupNode=*it;auto *nameCell=deviceList->item(0,deviceList->columnForKey("name"));record(101,"设备搜索后选择真实所在分组，已分组行保留且名称无错误红色标记",deviceKeyword && groupNode && groupLoaded && nameCell && nameCell->foreground().color()!=QColor("#f56c6c") && groupNode->foreground(0).color()!=QColor("#f56c6c"));
        record(94,"搜索栏使用简短设备名/编号提示，Hover明确支持类型、员工和主板号",deviceKeyword && deviceKeyword->placeholderText()=="设备名/编号" && deviceKeyword->toolTip().contains("主板号"));record(93,"1120宽设备搜索栏控件不越出页面",deviceKeyword && window.rect().contains(QRect(deviceKeyword->mapTo(&window,QPoint()),deviceKeyword->size())));
        auto moveMouse=[&](const QPoint &point){QMouseEvent event(QEvent::MouseMove,QPointF(point),QPointF(deviceList->viewport()->mapToGlobal(point)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(deviceList->viewport(),&event);app.processEvents();};const QPoint blank(10,deviceList->viewport()->height()-12);moveMouse(blank);const auto beforeHover=deviceList->grab().toImage();moveMouse(deviceList->visualItemRect(nameCell).center());moveMouse(blank);record(102,"实际鼠标移过数据再移到表格空白，行渲染恢复原样且数据不变",!deviceList->indexAt(blank).isValid() && beforeHover==deviceList->grab().toImage() && nameCell->text()=="测试设备");
        auto *remarkCell=deviceList->item(0,deviceList->columnForKey("remark"));record(103,"设备列表统一单行省略，255字字段保留完整Hover原文",!deviceList->wordWrap() && deviceList->textElideMode()==Qt::ElideRight && remarkCell && remarkCell->toolTip()==QString(255,QChar(0x5907)));
        UiI18n::instance()->setEnglish(true);app.processEvents();record(121,"默认总分组与设备表头翻译为英文，自定义设备名保留原文",groupNode && groupNode->text(0)==UiI18n::instance()->text("总分组") && groupNode->text(0)!="总分组" && deviceList->horizontalHeaderItem(deviceList->columnForKey("name"))->text()==UiI18n::instance()->text("设备名称") && nameCell->text()=="测试设备");UiI18n::instance()->setEnglish(false);app.processEvents();if(deviceKeyword){deviceKeyword->clear();QMetaObject::invokeMethod(deviceKeyword,"returnPressed");}

        deviceList->cellDoubleClicked(0,deviceList->columnForKey("name"));app.processEvents();
        record(114,"双击设备数据单元格不擅自弹出编辑窗口",QApplication::activeModalWidget()==nullptr);
        record(114,"设备列表加载完成后才允许操作编辑按钮",wait([&]{return deviceList->isEnabled();}));
        QPushButton *edit=nullptr;
        for(auto *button:window.findChildren<QPushButton*>()) if(button->isVisible() && button->text()=="编辑") { edit=button; break; }
        if(edit) {
            QTimer::singleShot(40,&window,[&] {
                auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                bool label=false;
                if(dialog) for(auto *text:dialog->findChildren<QLabel*>()) if(text->text().contains("设备编号")) label=true;
                record(104,"编辑设备弹窗字段统一为设备编号",label);
                record(114,"点击编辑按钮才打开设备编辑窗口",dialog!=nullptr);
                if(dialog){UiI18n::instance()->setEnglish(true);app.processEvents();bool fits=dialog->width()<=640;for(auto *label:dialog->findChildren<QLabel*>())if(label->isVisible() && !label->property("i18nSkip").toBool())fits&=label->wordWrap() || label->width()>=label->fontMetrics().horizontalAdvance(label->text());record(126,"英文设备编辑实际弹窗标签换行或完整容纳，不越出640宽弹窗",fits);UiI18n::instance()->setEnglish(false);app.processEvents();}

                if(dialog)for(const auto &field:QStringList{"type","electricControlType"}) {
                    QPushButton *create=nullptr;QComboBox *choice=nullptr;
                    for(auto *button:dialog->findChildren<QPushButton*>())if(button->property("quickCreateField")==field)create=button;
                    for(auto *box:dialog->findChildren<QComboBox*>())if(box->property("formFieldKey")==field)choice=box;
                    const QString value=field=="type" ? "类型新建原文" : "电控新建原文";
                    if(!create || !choice){record(111,"设备编辑包含新增类型入口"+field,false);continue;}
                    QTimer::singleShot(20,&window,[&] {
                        auto *prompt=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto *input=prompt ? prompt->findChild<QLineEdit*>("promptInput") : nullptr;
                        auto *save=prompt ? prompt->findChild<QPushButton*>("primaryButton") : nullptr;if(!input || !save)qFatal("quick catalog prompt missing");save->click();app.processEvents();
                        auto *marker=prompt->findChild<QLabel*>("requiredMarker");auto *error=prompt->findChild<QLabel*>("promptError");
                        record(111,"快速新增"+field+"有必填星号，空名称留在表单显示红字",marker && marker->text()=="*" && error && error->isVisible() && prompt->isVisible());
                        input->setText(QString(101,'x'));record(110,"快速新增"+field+"统一限制100字符",input->text().size()==100);input->setText(value);save->click();
                    });
                    create->click();const bool selected=wait([&]{return choice->currentData().toString()==value;});
                    UiI18n::instance()->setEnglish(true);app.processEvents();
                    record(127,"设备类型选择后英文回显保留业务原文："+field,selected && choice->currentText()==value && choice->currentData().toString()==value);
                    UiI18n::instance()->setEnglish(false);app.processEvents();
                    record(127,"切回中文类型选择仍保持："+field,choice->currentText()==value && choice->currentData().toString()==value);
                }
                bool copied=dialog!=nullptr;
                if(dialog) for(auto *input:dialog->findChildren<QLineEdit*>()) {
                    const QString saved=input->text(); input->selectAll(); input->copy();
                    copied=copied && (saved.isEmpty() || QApplication::clipboard()->text()==saved);
                }
                record(115,"编辑设备各行输入框支持复制现有内容",copied);
                QLineEdit *name=nullptr;
                if(dialog) for(auto *input:dialog->findChildren<QLineEdit*>()) if(input->property("formFieldKey").toString()=="name") name=input;
                if(name) {
                    const QString saved=name->text(); name->setText("   "); app.processEvents();
                    auto *error=dialog->findChild<QLabel*>("promptError");
                    record(106,"设备名称空格在输入时即时提示，不等提交",error && error->isVisible() && error->text().contains("设备名称"));
                    name->setText(QString(101,QChar(0x8bbe)));
                    record(105,"设备名称输入框限制100字符",name->text().size()==100);
                    record(108,"设备名称必填输入框带输入提示",name->placeholderText()=="请输入设备名称");
                    name->setText(saved);
                } else { record(105,"设备名称输入框限制100字符",false); record(106,"设备名称空格在输入时即时提示，不等提交",false); record(108,"设备名称必填输入框带输入提示",false); }
                if(dialog && name) {
                    name->setText("已修改待放弃");
                    QTimer::singleShot(30,&window,[&] {
                        auto *confirm=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                        if(!confirm || confirm==dialog) qFatal("Discard confirmation missing");
                        for(auto *button:confirm->findChildren<QPushButton*>()) if(button->text()=="取消") { button->click(); return; }
                        qFatal("Discard cancel missing");
                    });
                    dialog->reject();
                    record(116,"编辑有改动时取消放弃可保留弹窗",dialog->isVisible() && name->text()=="已修改待放弃");
                    QTimer::singleShot(30,&window,[&] {
                        auto *confirm=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                        if(!confirm || confirm==dialog) qFatal("Discard confirmation missing");
                        for(auto *button:confirm->findChildren<QPushButton*>()) if(button->text()=="确定") { button->click(); return; }
                        qFatal("Discard accept missing");
                    });
                    dialog->reject(); record(116,"确认放弃后关闭编辑且不提交",!dialog->isVisible());
                } else if(dialog) dialog->reject();
            });
            edit->click();
        } else { record(104,"编辑设备弹窗字段统一为设备编号",false); record(115,"编辑设备各行输入框支持复制现有内容",false); }
        homeFixture=false;
    }
    return failures ? 1 : 0;
}

int runChartRegression(QApplication &app) {
    QFile log(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG"));
    if(!log.open(QIODevice::WriteOnly|QIODevice::Append)) return 2;
    int failures=0;
    const QString directory=qEnvironmentVariable("BOER_CHART_SNAPSHOT_DIR");
    if(!directory.isEmpty()) QDir().mkpath(directory);
    auto record=[&](int row,const QString &name,bool passed) {
        log.write(QJsonDocument(QJsonObject{{"row",row},{"name",name},{"result",passed ? "PASS" : "FAIL"},
            {"scope","Qt真实图表控件，合成边界数据；截图与几何/鼠标事件验证"}}).toJson(QJsonDocument::Compact)+'\n'); log.flush();
        qInfo("ROW %d %s %s",row,passed ? "PASS" : "FAIL",qPrintable(name));
        if(!passed) ++failures;
    };
    auto hover=[&](ClientChart &chart,const QPointF &at) {
        QMouseEvent event(QEvent::MouseMove,at,chart.mapToGlobal(at.toPoint()),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&chart,&event); app.processEvents();
    };
    DateRangeWidget dates; dates.resize(245,28); dates.show();
    dates.setRange(QDate(2026,12,31),QDate(2026,1,1)); app.processEvents();
    record(46,"日期控件自动交换颠倒的起止日期",dates.startDate()==QDate(2026,1,1) && dates.endDate()==QDate(2026,12,31));
    bool dateFits=true;
    for(auto *button:dates.findChildren<QPushButton*>("datePart"))
        dateFits=dateFits && button->width()>=button->fontMetrics().horizontalAdvance(button->text())+4;
    record(40,"245像素日期筛选内完整日期文字可容纳，不与日历图标重叠",dateFits);
    if(!directory.isEmpty()) dates.grab().save(directory+"/date-range.png");
    ClientChart chart(ClientChart::Bar); chart.resize(600,240); chart.show();
    chart.setSeries(QJsonArray{QJsonObject{{"date","2026-10-23"},{"value",1234567}}}); app.processEvents();
    record(58,"百万级Y轴数字预留足够宽度，不使用固定29像素框",chart.plotArea().left()-5>=chart.fontMetrics().horizontalAdvance("1234567"));
    if(!directory.isEmpty()) chart.grab().save(directory+"/large-y-axis.png");
    QJsonArray month;
    for(int i=1;i<=30;++i) month.append(QJsonObject{{"date",QString("2026-09-%1").arg(i,2,10,QChar('0'))},{"value",i}});
    chart.setSeries(month); app.processEvents();
    const QRectF plot=chart.plotArea(); hover(chart,QPointF(plot.right()-1,plot.center().y()));
    record(39,"30天产量图最右日期可Hover获取完整日期",chart.toolTip().startsWith("2026-09-30"));
    if(!directory.isEmpty()) chart.grab().save(directory+"/month-30-days.png");
    auto barRuns=[&](int count) {QJsonArray values;for(int i=0;i<count;++i)values.append(QJsonObject{{"date",QString::number(i)},{"value",10}});chart.setSeries(values);app.processEvents();const auto pixels=chart.grab().toImage().scaled(chart.size());const auto area=chart.plotArea();int runs=0,widest=0,width=0;for(int x=int(area.left());x<=int(area.right());++x){if(pixels.pixelColor(x,int(area.center().y())).name()=="#73b3fb"){if(width==0)++runs;++width;widest=qMax(widest,width);}else width=0;}return qMakePair(runs,widest);};
    const auto five=barRuns(5),thirty=barRuns(30),hundred=barRuns(100);
    record(44,"实际渲染5、30、100个柱条均独立且随数据增加自动变细，无重叠",five.first==5 && thirty.first==30 && hundred.first==100 && five.second>thirty.second && thirty.second>hundred.second);
    chart.setSeries(QJsonArray{QJsonObject{{"date","2025-12-31"},{"value",1}},QJsonObject{{"date","2026-01-01"},{"value",2}}});
    app.processEvents(); hover(chart,QPointF(chart.plotArea().left()+1,100));
    const bool firstYear=chart.toolTip().startsWith("2025-12-31");
    hover(chart,QPointF(chart.plotArea().right()-1,100));
    record(42,"跨年日期Hover分别保留2025和2026年份",firstYear && chart.toolTip().startsWith("2026-01-01"));
    ClientChart hours(ClientChart::Line); hours.resize(600,240); hours.show();
    QJsonArray hourly;
    for(int i=0;i<24;++i) hourly.append(QJsonObject{{"date",QString("%1:00").arg(i,2,10,QChar('0'))},{"value",i}});
    hours.setSeries(hourly); app.processEvents();
    hover(hours,QPointF(hours.plotArea().right()-1,100));
    record(35,"24小时时间图最后23时数据可访问",hours.toolTip().startsWith("23:00"));
    if(!directory.isEmpty()) hours.grab().save(directory+"/hours-24.png");
    ClientChart horizontal(ClientChart::HorizontalBar); horizontal.resize(600,300); horizontal.show();
    const QString longName=QString(50,QChar(0x8bbe));
    horizontal.setSeries(QJsonArray{QJsonObject{{"name","first"},{"value",10}},QJsonObject{{"name",longName},{"value",20}},
        QJsonObject{{"name","third"},{"value",30}}},"name"); app.processEvents();
    hover(horizontal,QPointF(550,25+38*1.5));
    record(59,"横向图Hover按行定位并显示完整长设备名",horizontal.toolTip().startsWith(longName));
    if(!directory.isEmpty()) horizontal.grab().save(directory+"/long-device-name.png");
    chart.setSeries(QJsonArray{}); app.processEvents();
    record(38,"切换空数据后清除上一条图表Hover提示",chart.toolTip().isEmpty());
    if(!directory.isEmpty()) chart.grab().save(directory+"/empty-chart.png");
    ClientChart runtime(ClientChart::Line); runtime.resize(600,240); runtime.show();
    runtime.setSecondaryValue("processingTime"); runtime.setValueLabels("运行时长","加工时长","hours");
    runtime.setSeries(QJsonArray{QJsonObject{{"date","2026-10-24"},{"runningTime",3661.0/3600},{"processingTime",.5}}},"date","runningTime");
    UiI18n::instance()->setEnglish(false); app.processEvents(); hover(runtime,QPointF(runtime.plotArea().center()));
    record(61,"时长Hover按小时分秒显示，保留秒值",runtime.toolTip().contains("1小时1分1秒") && runtime.toolTip().contains("0小时30分0秒"));
    UiI18n::instance()->setEnglish(true); app.processEvents(); hover(runtime,QPointF(runtime.plotArea().center()));
    record(60,"运行与加工时长Hover字段随英文界面翻译",!runtime.toolTip().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")) && runtime.toolTip().contains(": "));
    record(61,"英文时长Hover使用h/m/s单位",runtime.toolTip().contains("1h 1m 1s") && runtime.toolTip().contains("0h 30m 0s"));
    ClientChart utilization(ClientChart::Bar); utilization.resize(600,240); utilization.show();
    utilization.setValueLabels("设备使用率",{},"percent"); utilization.setSeries(QJsonArray{QJsonObject{{"date","2026-10-24"},{"value",12.5}}});
    app.processEvents(); hover(utilization,QPointF(utilization.plotArea().center()));
    record(62,"设备使用率Hover字段翻译且含百分号",!utilization.toolTip().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")) && utilization.toolTip().contains("12.5%"));
    ClientChart alarms(ClientChart::Line);alarms.resize(600,240);alarms.show();alarms.setValueLabels("报警次数");alarms.setHoverDetail("avgDuration","平均报警时长","minutes");
    alarms.setSeries(QJsonArray{QJsonObject{{"date","2026-10-24"},{"count",3},{"avgDuration",2}}},"date","count");app.processEvents();hover(alarms,alarms.plotArea().center());
    record(309,"日报警趋势Hover字段和平均时长分钟单位均英文显示",!alarms.toolTip().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")) && alarms.toolTip().endsWith("2 min"));
    UiI18n::instance()->setEnglish(false);
    hover(alarms,alarms.plotArea().center());record(304,"日报警趋势Hover可查看平均报警时长2分",alarms.toolTip().contains("平均报警时长: 2分"));
    ClientChart pie(ClientChart::Pie);pie.resize(600,300);pie.show();
    QJsonArray quarters;for(const QString &name:QStringList{"东北","东南","西南","西北"})quarters.append(QJsonObject{{"name",name},{"value",1}});pie.setSeries(quarters,"name");app.processEvents();
    const auto center=pie.pieArea().center();bool sectors=true;const QList<QPointF> points={center+QPointF(40,-40),center+QPointF(40,40),center+QPointF(-40,40),center+QPointF(-40,-40)};
    for(int i=0;i<points.size();++i){hover(pie,points[i]);sectors=sectors && pie.toolTip().startsWith(quarters[i].toObject().value("name").toString());}
    record(303,"饼图Hover根据实际扇区定位，四象限类别正确",sectors);
    QJsonArray many;for(int i=1;i<=25;++i)many.append(QJsonObject{{"name",QString("报警类别%1").arg(i)+QString(30,QChar(0x62a5))},{"value",1}});pie.setSeries(many,"name");app.processEvents();
    hover(pie,center+QPointF(-11.3,-89.3));record(303,"25种报警最后一类扇区可Hover获取完整名称",pie.toolTip().startsWith(many.last().toObject().value("name").toString()));
    hover(pie,QPointF(5,5));record(303,"鼠标离开饼图圆形范围后清除错误Hover",pie.toolTip().isEmpty());record(302,"饼图边界外移会清除Hover，避免显示错误类别",pie.toolTip().isEmpty());
    if(!directory.isEmpty())pie.grab().save(directory+"/pie-25-alarms.png");
    return failures ? 1 : 0;
}
