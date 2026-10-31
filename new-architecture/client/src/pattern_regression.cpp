#include "login_regression.h"
#include "client_window.h"
#include "page_widgets.h"
#include "ui_controls.h"
#include "ui_i18n.h"
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressDialog>
#include <QProgressBar>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QUrl>
#include <memory>

int runPatternRegression(QApplication &app,const QString &host,int port) {
    QTemporaryDir files; QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,files.path());
    UiI18n::instance()->setEnglish(false);
    QFile log(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG")); if(!log.open(QIODevice::WriteOnly|QIODevice::Append)) return 2;
    int failures=0;
    auto record=[&](int row,const QString &name,bool passed) {
        log.write(QJsonDocument(QJsonObject{{"row",row},{"name",name},{"result",passed ? "PASS" : "FAIL"},
            {"scope","Qt客户端真实花型页面+Qt服务端+DT模拟设备"}}).toJson(QJsonDocument::Compact)+'\n'); log.flush();
        qInfo("ROW %d %s %s",row,passed ? "PASS" : "FAIL",qPrintable(name)); failures+=!passed;
    };
    auto wait=[&](std::function<bool()> test,int timeout=6000) {
        QElapsedTimer timer; timer.start();
        while(!test() && timer.elapsed()<timeout) { app.processEvents(); QThread::msleep(5); }
        return test();
    };
    ClientWindow window; window.resize(1600,1000); window.show(); auto *api=window.api(); api->setServer(host,port);
    QTimer::singleShot(45000,&window,[]{qFatal("Pattern UI regression timed out");});
    auto request=[&](const QByteArray &method,const QString &path,const QJsonObject &body=QJsonObject{}) {
        auto response=std::make_shared<ApiResponse>(); auto done=std::make_shared<bool>(false);
        api->request(method,path,body,[response,done](const ApiResponse &value){ *response=value; *done=true; });
        if(!wait([&]{return *done;})) qFatal("Pattern request timeout"); return *response;
    };
    auto login=request("POST","/auth/login",{{"username","admin"},{"password","admin123"}});
    if(!login.ok) return 3;
    window.testSession(host,port,login.data.toObject().value("user").toObject(),login.data.toObject().value("token").toString());
    const auto devices=request("GET","/device/list?keyword=88555").data.toObject().value("list").toArray();
    if(devices.isEmpty()) return 4; const qint64 deviceId=devices.first().toObject().value("id").toInteger();
    QFile source(files.filePath("existing.NTP")); if(!source.open(QIODevice::WriteOnly)) return 5;
    source.write("ORIGINAL"); source.close();
    auto uploaded=std::make_shared<ApiResponse>(); auto uploadDone=std::make_shared<bool>(false);
    api->uploadFile("/pattern/upload",source.fileName(),{{"name","设备花型A"},{"unitPrice",2.5},{"patternType","原类型"}},
        [uploaded,uploadDone](const ApiResponse &value){ *uploaded=value; *uploadDone=true; });
    if(!wait([&]{return *uploadDone;}) || !uploaded->ok) return 6;
    const qint64 originalId=uploaded->data.toObject().value("id").toInteger();
    const QString originalTime=uploaded->data.toObject().value("uploadedAt").toString();
    window.navigate("/file/pattern"); app.processEvents();
    auto *deviceCard=window.findChild<QWidget*>("clientDevicePatterns");
    auto *deviceCombo=window.findChild<QComboBox*>("patternDeviceSelect");
    auto *deviceTable=deviceCard ? dynamic_cast<ClientTable*>(deviceCard->findChild<QTableWidget*>("clientTable")) : nullptr;
    if(!deviceCombo || !deviceTable || !wait([&]{return deviceCombo->findData(deviceId)>=0;})) return 7;
    deviceCombo->setCurrentIndex(deviceCombo->findData(deviceId));
    QPushButton *read=nullptr,*upload=nullptr,*queue=nullptr;
    for(auto *button:deviceCard->findChildren<QPushButton*>()) {
        if(button->text()=="实时读取设备") read=button;
        if(button->text()=="回传选中文件") upload=button;
        if(button->text()=="上传队列") queue=button;
    }
    if(!read || !upload || !queue) return 8;
    if(qEnvironmentVariableIsSet("BOER_TEST_MULTI_UPLOAD")) {
        read->click();if(!wait([&]{return deviceTable->rowCount()==2;}))return 48;for(int row=0;row<2;++row)deviceTable->cellWidget(row,0)->findChild<QCheckBox*>()->setChecked(true);QTimer conflict;conflict.setInterval(10);QObject::connect(&conflict,&QTimer::timeout,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog || dialog->windowTitle()!="同名花型处理")return;conflict.stop();for(auto *combo:dialog->findChildren<QComboBox*>())if(combo->property("formFieldKey")=="conflictMode")combo->setCurrentIndex(combo->findData("overwrite"));dialog->findChild<QPushButton*>("primaryButton")->click();});conflict.start();upload->click();if(!wait([&]{return request("GET","/pattern/upload-queue").data.toObject().value("total").toInt()==2;}))return 49;conflict.stop();QSet<int> intermediate;bool done=false;QTimer observe;observe.setInterval(20);QObject::connect(&observe,&QTimer::timeout,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog || dialog->windowTitle()!="上传队列")return;auto *overall=dialog->findChild<QProgressBar*>("uploadOverallProgress");ClientTable *table=nullptr;for(auto *widget:dialog->findChildren<QTableWidget*>())if(auto *candidate=dynamic_cast<ClientTable*>(widget))table=candidate;if(!overall || !table || !table->isEnabled())return;if(overall->value()>0 && overall->value()<100)intermediate.insert(overall->value());if(overall->value()!=100 || table->rowCount()!=2)return;bool completed=true;for(int row=0;row<2;++row)completed&=table->recordAt(row).value("status")=="completed" && table->recordAt(row).value("progress").toInt()==100;record(212,"两项真实回传总进度出现多个中间值，最终100%且均完成",completed && intermediate.size()>=2);auto *counts=dialog->findChild<QLabel*>("uploadOverallCounts");record(212,"总处理进度同时明确成功失败数量，不以百分比冒充全成功",counts && counts->text().contains("完成 2") && counts->text().contains("失败 0"));done=true;observe.stop();dialog->accept();});observe.start();queue->click();observe.stop();if(!done)return 50;return failures ? 1 : 0;
    }
    if(qEnvironmentVariableIsSet("BOER_TEST_DEVICE_FILE_QUEUE")) {
        read->click();if(!wait([&]{return deviceTable->rowCount()==3;}))return 38;UiI18n::instance()->setEnglish(true);app.processEvents();QTreeWidget *targets=nullptr;for(auto *tree:window.findChildren<QTreeWidget*>("groupTree"))if(tree->isVisible())targets=tree;if(!targets)return 39;QTreeWidgetItem *all=nullptr,*ungrouped=nullptr;for(QTreeWidgetItemIterator it(targets);*it;++it){if((*it)->data(0,Qt::UserRole).toString()=="all")all=*it;if((*it)->data(0,Qt::UserRole).toString()=="ungrouped")ungrouped=*it;}record(153,"花型设备树中英文统一全部分组与未分组设备命名",all && ungrouped && all->text(0)==UiI18n::instance()->text("全部分组") && ungrouped->text(0)==UiI18n::instance()->text("未分组设备"));record(225,"花型设备树英文静态分组字段已翻译，设备业务名不强制翻译",all && ungrouped && all->text(0)==UiI18n::instance()->text("全部分组") && ungrouped->text(0)==UiI18n::instance()->text("未分组设备"));if(!all)return 40;all->setCheckState(0,Qt::Checked);
        auto *serverCard=window.findChild<QWidget*>("clientServerPatterns");auto *patterns=serverCard ? dynamic_cast<ClientTable*>(serverCard->findChild<QTableWidget*>("clientTable")) : nullptr;if(!patterns || !wait([&]{return patterns->rowCount()==1 && patterns->isEnabled();}))return 41;patterns->cellClicked(0,patterns->columnForKey("name"));QPushButton *send=nullptr;for(auto *button:serverCard->findChildren<QPushButton*>())if(button->text()==UiI18n::instance()->text("批量下发"))send=button;if(!send)return 42;send->click();if(!wait([&]{return request("GET","/pattern/queue").data.toObject().value("total").toInt()==1;}))return 43;auto tasks=request("GET","/pattern/queue").data.toObject().value("list").toArray();const auto task=tasks.first().toObject();record(155,"全选分组包含未分组设备，缝纫中的真实任务保持等待而非错误完成",task.value("deviceId").toInteger()==deviceId && task.value("status")=="waiting");QLabel *caption=nullptr;for(auto *label:window.findChildren<QLabel*>())if(label->isVisible() && label->property("selectionSource")=="全部分组")caption=label;record(151,"虚拟全部分组作用于全部目标，当前选择不误写成实体总分组",caption && caption->text()==UiI18n::instance()->text("全部分组") && task.value("deviceId").toInteger()==deviceId);
        const auto current=request("GET","/pattern/device-files?deviceId="+QString::number(deviceId)).data.toObject();record(219,"存在等待空闲的未完成下发任务时仍能查询3个设备文件",current.value("list").toArray().size()==3);request("POST","/pattern/queue/"+QString::number(task.value("id").toInteger())+"/pause");const qint64 old=deviceTable->recordAt(0).value("id").toInteger();read->click();record(219,"任务暂停仍可实时读取设备文件并刷新缓存ID",wait([&]{return deviceTable->rowCount()==3 && deviceTable->recordAt(0).value("id").toInteger()!=old;}));
        const QString longType=QString(100,QChar(0x578b)),longOrder=QString(100,'A'),longDevice=QString(100,QChar(0x8bbe));request("PUT","/pattern/"+QString::number(originalId),{{"patternType",longType},{"orderNo",longOrder}});request("PUT","/device/"+QString::number(deviceId),{{"name",longDevice}});QPushButton *query=nullptr;for(auto *button:deviceCard->findChildren<QPushButton*>())if(button->text()==UiI18n::instance()->text("查询"))query=button;if(!query)return 44;query->click();int a=-1;wait([&]{for(int row=0;row<deviceTable->rowCount();++row)if(deviceTable->recordAt(row).value("fileName")=="设备花型A.NTP" && deviceTable->recordAt(row).value("orderNo")==longOrder){a=row;return true;}return false;});record(221,"100字类型及100位订单在设备文件表单行省略，Hover保留全文",a>=0 && !deviceTable->wordWrap() && deviceTable->item(a,deviceTable->columnForKey("patternType"))->toolTip()==longType && deviceTable->item(a,deviceTable->columnForKey("orderNo"))->toolTip()==longOrder);
        window.resize(1120,700);app.processEvents();for(auto *scroll:window.findChildren<QScrollArea*>())if(scroll->isVisible() && scroll->isAncestorOf(deviceCard))scroll->ensureWidgetVisible(deviceCard);app.processEvents();const bool named=wait([&]{return deviceCombo->currentText().startsWith(longDevice);});QLineEdit *filter=nullptr;for(auto *line:deviceCard->findChildren<QLineEdit*>())if(line->placeholderText()==UiI18n::instance()->text("文件名/订单号"))filter=line;if(filter)filter->setText(QString(1000,'X'));record(222,"1120小窗长设备名与长筛选输入不撑出设备文件区",named && deviceCombo->width()==220 && filter && filter->width()==175 && window.rect().contains(QRect(deviceCombo->mapTo(&window,QPoint()),deviceCombo->size())) && window.rect().contains(QRect(filter->mapTo(&window,QPoint()),filter->size())));if(filter)filter->clear();window.grab().save(QFileInfo(log.fileName()).absoluteDir().filePath("pattern-device-long-fields-en.png"));
        window.navigate("/device/monitor");app.processEvents();QTreeWidget *monitorTree=nullptr;QTreeWidgetItem *monitorNode=nullptr;wait([&]{for(auto *tree:window.findChildren<QTreeWidget*>("groupTree"))if(tree->isVisible())monitorTree=tree;if(monitorTree)for(QTreeWidgetItemIterator it(monitorTree);*it;++it)if((*it)->data(0,Qt::UserRole+1)=="device" && (*it)->data(0,Qt::UserRole).toLongLong()==deviceId)monitorNode=*it;return monitorNode!=nullptr;});if(!monitorNode)return 46;monitorTree->setCurrentItem(monitorNode);monitorTree->itemClicked(monitorNode,0);record(118,"真实DT上报2000RPM、12345针和花型后，监控实际页面完整显示",wait([&]{bool speed=false,needles=false,pattern=false;for(auto *label:window.findChildren<QLabel*>("clientMetric"))if(label->isVisible()){speed|=label->text()=="2000 RPM";needles|=label->text()=="12345";pattern|=label->text()=="监控花型";}return speed && needles && pattern;}));window.navigate("/file/pattern");app.processEvents();if(!wait([&]{return deviceTable->rowCount()==3 && deviceCombo->currentData().toLongLong()==deviceId;}))return 47;
        const auto snapshot=deviceTable->recordAt(0);QFile signal(qEnvironmentVariable("BOER_TEST_DEVICE_CLOSE_SIGNAL"));if(!signal.open(QIODevice::WriteOnly))return 45;signal.write("close");signal.close();record(191,"真实DT断开后无需手动刷新，设备选择器和图标自动显示关机",wait([&]{bool offline=false;for(QTreeWidgetItemIterator it(targets);*it;++it)if((*it)->data(0,Qt::UserRole+1)=="device" && (*it)->data(0,Qt::UserRole).toLongLong()==deviceId)offline=(*it)->data(0,Qt::UserRole+2)=="offline";return offline && deviceCombo->currentText().contains(UiI18n::instance()->text("关机"));}));record(190,"关机状态自动清空设备文件缓存视图",wait([&]{return deviceTable->rowCount()==0;}));const auto rejected=request("POST","/pattern/device-files/upload",{{"deviceId",deviceId},{"fileIds",QJsonArray{snapshot.value("id")}}, {"selectedFiles",QJsonArray{snapshot}}});record(190,"旧文件快照在设备断开后回传请求立即拒绝，不创建卡住任务",!rejected.ok && request("GET","/pattern/upload-queue").data.toObject().value("total").toInt()==0);return failures ? 1 : 0;
    }
    if(qEnvironmentVariableIsSet("BOER_TEST_DEVICE_DELETE")) {
        read->click();if(!wait([&]{return deviceTable->rowCount()==6;}))return 35;UiI18n::instance()->setEnglish(true);app.processEvents();auto *bulk=window.findChild<QPushButton*>("clientDeleteDeviceFiles");if(!bulk)return 36;
        auto select=[&](const QStringList &names){for(int row=0;row<deviceTable->rowCount();++row)if(auto *check=deviceTable->cellWidget(row,0)->findChild<QCheckBox*>())check->setChecked(names.contains(deviceTable->recordAt(row).value("fileName").toString()));};
        auto has=[&](const QString &name){for(int row=0;row<deviceTable->rowCount();++row)if(deviceTable->recordAt(row).value("fileName")==name)return true;return false;};
        auto confirm=[&]{QTimer::singleShot(20,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("device file delete confirmation missing");bool english=true;for(auto *label:dialog->findChildren<QLabel*>())english&=!label->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));record(230,"英文设备文件删除二次确认文案完整翻译",english);dialog->findChild<QPushButton*>("primaryButton")->click();});};
        auto execute=[&](bool cancel,int remaining){bool loading=false,canceled=false;QTimer watch;watch.setInterval(10);QObject::connect(&watch,&QTimer::timeout,&window,[&]{auto *progress=qobject_cast<QProgressDialog*>(QApplication::activeModalWidget());if(!progress || !progress->isVisible())return;if(!loading){loading=true;auto *label=progress->findChild<QLabel*>();bool buttonsEnglish=true;for(auto *button:progress->findChildren<QPushButton*>())buttonsEnglish&=!button->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));record(230,"实际设备文件删除Loading显示英文进度与停止后续删除按钮",label && label->text().startsWith("Deleting device files") && buttonsEnglish);progress->grab().save(QFileInfo(log.fileName()).absoluteDir().filePath("device-file-delete-progress-en.png"));}if(cancel && !canceled){canceled=true;QPushButton *stop=nullptr;for(auto *button:progress->findChildren<QPushButton*>())if(button->text()=="Stop Remaining Deletions")stop=button;if(!stop)qFatal("delete stop button missing");stop->click();}});watch.start();confirm();bulk->click();const bool done=wait([&]{return deviceCard->isEnabled() && deviceTable->rowCount()==remaining && !QApplication::activeModalWidget();},10000);watch.stop();return done && loading;};
        select({"设备花型A.NTP","设备花型B.NTP"});const QString firstDeleted=deviceTable->checkedRecords().first().value("fileName").toString(),kept=firstDeleted=="设备花型A.NTP" ? "设备花型B.NTP" : "设备花型A.NTP";record(224,"批量停止后当前首项完成，其余选中文件保留",execute(true,5) && !has(firstDeleted) && has(kept));
        select({kept,"设备花型D.NTP"});record(224,"实际一次选择两个剩余文件删除，仅移除成功项",execute(false,3) && !has(kept) && !has("设备花型D.NTP") && has("设备花型C.NTP"));
        select({"设备花型C.NTP"});record(224,"设备拒绝删除时保留文件并明确报告失败文件",execute(false,3) && has("设备花型C.NTP"));auto *toast=window.findChild<QLabel*>("clientToast");record(230,"英文删除失败提示保留业务文件名且原因完成翻译",toast && toast->text().contains("设备花型C.NTP") && toast->text().contains("rejected"));
        int same=-1;for(int row=0;row<deviceTable->rowCount();++row)if(deviceTable->recordAt(row).value("fileName")=="同名.NTP")same=row;auto *frozen=deviceTable->findChild<QTableWidget*>("clientFrozenActions");QPushButton *remove=nullptr;if(frozen && same>=0 && frozen->cellWidget(same,0))remove=frozen->cellWidget(same,0)->findChild<QPushButton*>();if(!remove)return 37;confirm();remove->click();const bool guarded=wait([&]{auto *tip=window.findChild<QLabel*>("clientToast");return deviceCard->isEnabled() && tip && tip->text().contains("same name");});record(203,"同名不同编号删除不能按协议区分时拒绝，不误删任意一个",guarded && deviceTable->rowCount()==3 && has("同名.NTP"));return failures ? 1 : 0;
    }
    if(qEnvironmentVariableIsSet("BOER_TEST_MANY_CONFLICTS")) {
        read->click();auto *pagination=deviceCard->findChild<ClientPagination*>();if(!pagination)return 31;auto *size=pagination->findChild<QComboBox*>();if(!size)return 32;size->setCurrentIndex(size->findData(100));if(!wait([&]{return deviceTable->rowCount()==40;}))return 33;
        for(int row=0;row<40;++row)deviceTable->cellClicked(row,deviceTable->columnForKey("fileName"));
        QTimer poll;poll.setInterval(10);bool finished=false;QObject::connect(&poll,&QTimer::timeout,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog || dialog->windowTitle()!="同名花型处理")return;poll.stop();auto *pages=dialog->findChild<ClientPagination*>("formPagination");if(!pages)qFatal("conflict pagination missing");
            QList<QLineEdit*> names;for(auto *line:dialog->findChildren<QLineEdit*>())if(line->property("formFieldKey").toString().toLongLong()>0)names<<line;auto visible=[&]{int count=0;for(auto *name:names)count+=name->isVisible();return count;};
            record(202,"40个实际同名文件冲突分成每页10项，处理方式固定显示",names.size()==40 && visible()==10 && pages->page()==1);auto *first=names.first();first->setText("分页保留A");QPushButton *next=nullptr;for(auto *button:pages->findChildren<QPushButton*>())if(button->text()=="›")next=button;if(!next)qFatal("conflict next page missing");next->click();app.processEvents();QLineEdit *second=nullptr;for(auto *name:names)if(name->isVisible()){second=name;break;}if(!second)qFatal("conflict second page missing");second->setText("分页保留B");
            record(202,"冲突下一页只显示10项，第一页修改仍保留",pages->page()==2 && visible()==10 && !first->isVisible() && first->text()=="分页保留A");pages->setPage(4);app.processEvents();record(202,"冲突末页可查看第31至40项且保留第二页输入",visible()==10 && !second->isVisible() && second->text()=="分页保留B");
            first->clear();dialog->findChild<QPushButton*>("primaryButton")->click();app.processEvents();record(202,"隐藏页面存在空名称时返回该页并提示，不漏校验",dialog->isVisible() && pages->page()==1 && first->isVisible() && dialog->findChild<QLabel*>("promptError")->isVisible());first->setText("分页保留A");QComboBox *mode=nullptr;for(auto *combo:dialog->findChildren<QComboBox*>())if(combo->property("formFieldKey")=="conflictMode")mode=combo;if(!mode)qFatal("conflict mode missing");mode->setCurrentIndex(mode->findData("overwrite"));bool disabled=true;for(auto *name:names)disabled&=!name->isEnabled() && name->property("skipRequired").toBool();record(202,"覆盖模式统一禁用所有页面重命名字段",disabled);mode->setCurrentIndex(mode->findData("rename"));dialog->findChild<QPushButton*>("primaryButton")->click();finished=true;});
        poll.start();upload->click();if(!wait([&]{return finished;}))return 34;const bool queued=wait([&]{return request("GET","/pattern/upload-queue").data.toObject().value("total").toInt()==40;});const auto tasks=request("GET","/pattern/upload-queue?pageSize=100").data.toObject().value("list").toArray();QSet<QString> targetNames;for(const auto &task:tasks)targetNames.insert(task.toObject().value("targetName").toString());record(202,"跨页修改一次提交生成40项，重命名不丢失且不重名",queued && targetNames.size()==40 && targetNames.contains("分页保留A") && targetNames.contains("分页保留B"));return failures ? 1 : 0;
    }
    read->click(); record(218,"实时读取设备从按钮发起并显示设备花型列表",wait([&]{return deviceTable->rowCount()==2;}));
    record(203,"设备文件列表提供编号77/78区分文件，未知大小不显示为0B",deviceTable->columnForKey("patternNo")>=0 && deviceTable->recordAt(0).value("patternNo").toInt()!=deviceTable->recordAt(1).value("patternNo").toInt() && deviceTable->item(0,deviceTable->columnForKey("size"))->text()=="未知");
    int fileRow=-1;
    for(int row=0;row<deviceTable->rowCount();++row) if(deviceTable->recordAt(row).value("fileName").toString()=="设备花型A.NTP") fileRow=row;
    if(fileRow<0) return 9;
    const qint64 staleId=deviceTable->recordAt(fileRow).value("id").toInteger();
    deviceTable->cellClicked(fileRow,deviceTable->columnForKey("fileName"));
    // Refresh only the server's cache, leaving the selected native row unchanged.
    if(!request("POST","/pattern/device-files/refresh",{{"deviceId",deviceId}}).ok) return 10;
    const auto fresh=request("GET","/pattern/device-files?deviceId="+QString::number(deviceId)).data.toObject().value("list").toArray();
    bool idChanged=true; for(const QJsonValue &value:fresh) if(value.toObject().value("id").toInteger()==staleId) idChanged=false;
    record(199,"回传前构造列表ID已刷新而界面仍选旧ID的场景",idChanged);
    const QString renamedTarget=QString(240,QChar(0x56de));
    bool conflictDone=false,overwrite=false; QTimer conflict; conflict.setInterval(10);
    QObject::connect(&conflict,&QTimer::timeout,&window,[&] {
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog || dialog->windowTitle()!="同名花型处理") return; conflict.stop();
        QComboBox *mode=nullptr; QLineEdit *name=nullptr;
        for(auto *combo:dialog->findChildren<QComboBox*>()) if(combo->property("formFieldKey").toString()=="conflictMode") mode=combo;
        for(auto *input:dialog->findChildren<QLineEdit*>()) if(input->property("formFieldKey").toString().toLongLong()>0) name=input;
        record(200,"同名处理默认选择重命名新增",mode && mode->currentData().toString()=="rename");
        bool labeled=false,numbered=false; for(auto *label:dialog->findChildren<QLabel*>()) if(label->text().startsWith("花型名称：") && label->toolTip().contains("设备花型A.NTP")){labeled=true;numbered=label->toolTip().contains("#77");}
        record(201,"冲突列表明确显示花型名称前缀与文件名",labeled);
        record(203,"同名处理字段显示设备文件编号，完整名称可Hover核对",numbered);
        if(!name || !mode) qFatal("Pattern rename field missing");
        if(overwrite) {
            mode->setCurrentIndex(mode->findData("overwrite")); name->clear();
            record(194,"选择覆盖时不要求填写无关的重命名字段",!name->isEnabled() && name->property("skipRequired").toBool());
        } else {
            name->clear(); app.processEvents(); auto *error=dialog->findChild<QLabel*>("promptError");
            record(198,"重命名字段清空立即显示必填提示",error && error->isVisible());
            name->setText(renamedTarget);
        }
        conflictDone=true;
        for(auto *button:dialog->findChildren<QPushButton*>()) if(button->text()=="确定") { button->click(); return; }
        qFatal("Pattern conflict submit missing");
    });
    conflict.start(); upload->click();
    record(196,"界面携带文件快照，旧ID仍能进入同名处理",wait([&]{return conflictDone;}));
    conflict.stop(); if(!conflictDone) return 11;
    bool queueDone=false,middle=false,cleaning=false,activeChecked=false,expectFailure=false;
    QTimer observe; observe.setInterval(20);
    QObject::connect(&observe,&QTimer::timeout,&window,[&] {
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog || dialog->windowTitle()!="上传队列") return;
        ClientTable *table=nullptr;
        for(auto *widget:dialog->findChildren<QTableWidget*>()) if(auto *candidate=dynamic_cast<ClientTable*>(widget)) table=candidate;
        QPushButton *clear=nullptr; for(auto *button:dialog->findChildren<QPushButton*>()) if(button->text()=="清理已完成") clear=button;
        if(!table || !clear || !table->isEnabled()) return;
        if(cleaning) {
            if(table->rowCount()==0 && !clear->isEnabled()) { record(209,"上传队列清空后清理按钮置灰",true); queueDone=true; observe.stop(); dialog->accept(); }
            return;
        }
        if(table->rowCount()!=1) return;
        const auto row=table->recordAt(0);
        if(row.value("status").toString()=="uploading" && row.value("progress").toInt()>1 && row.value("progress").toInt()<100) {
            middle=true;
            if(!activeChecked) {
                bool disabled=true; auto *actions=table->findChild<QTableWidget*>("clientFrozenActions");
                if(actions && actions->cellWidget(0,0)) for(auto *button:actions->cellWidget(0,0)->findChildren<QPushButton*>()) if(button->isEnabled()) disabled=false;
                record(215,"正在回传时暂停继续取消按钮均禁用",actions && disabled); activeChecked=true;
            }
        }
        if(row.value("status").toString()!=(expectFailure ? "failed" : "completed")) return;
        if(expectFailure) {
            record(214,"设备拒绝回传时队列明确显示失败而非已完成",row.value("status").toString()=="failed");
            record(205,"回传失败时可看到具体失败文件名",row.value("fileName").toString()=="设备花型B.NTP");
            record(206,"回传失败显示设备拒绝原因与状态码",row.value("message").toString().contains("设备拒绝") && row.value("message").toString().contains("1"));
        } else {
            record(208,"上传队列在完成前实际显示中间进度",middle);
            record(207,"界面队列设备文件名完整显示",row.value("fileName").toString()=="设备花型A.NTP");
        }
        if(!expectFailure && !overwrite){const int targetColumn=table->columnForKey("patternName");record(210,"上传队列显示服务器目标花型名称，240字名称保留完整Hover且单行省略",targetColumn>=0 && table->item(0,targetColumn)->toolTip()==renamedTarget && !table->wordWrap());}
        UiI18n::instance()->setEnglish(true);app.processEvents();
        const int messageColumn=table->columnForKey("message"),statusColumn=table->columnForKey("status");
        record(229,expectFailure ? "失败回传任务的英文备注完成翻译" : "完成回传任务的英文备注完成翻译",messageColumn>=0 && !table->item(0,messageColumn)->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")));
        record(228,"上传队列状态随英文界面翻译",statusColumn>=0 && !table->item(0,statusColumn)->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")));
        UiI18n::instance()->setEnglish(false);app.processEvents();
        bool disabled=true; auto *actions=table->findChild<QTableWidget*>("clientFrozenActions");
        if(actions && actions->cellWidget(0,0)) for(auto *button:actions->cellWidget(0,0)->findChildren<QPushButton*>()) if(button->isEnabled()) disabled=false;
        record(215,expectFailure ? "失败回传任务的暂停继续取消按钮均禁用" : "已完成回传任务的暂停继续取消按钮均禁用",actions && disabled);
        cleaning=true;
        QTimer::singleShot(20,&window,[&] {
            auto *confirm=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            record(211,"上传队列清理前出现二次确认",confirm && confirm->windowTitle()!=QString("上传队列"));
            if(!confirm) qFatal("Queue cleanup confirmation missing");
            for(auto *button:confirm->findChildren<QPushButton*>()) if(button->text()=="确定") { button->click(); return; }
            qFatal("Queue cleanup confirm missing");
        });
        clear->click();
    });
    observe.start(); QTimer::singleShot(20,&window,[&]{if(!expectFailure)record(193,"回传入队后明确提示点击上传队列查看进度",wait([&]{auto *toast=window.findChild<QLabel*>("clientToast");return toast && toast->text().contains("上传队列") && toast->text().contains("查看进度");}));queue->click();});
    record(214,"回传队列完整执行并可清理关闭",wait([&]{return queueDone;},14000)); observe.stop();
    const auto list=request("GET","/pattern/list").data.toObject().value("list").toArray();
    bool renamed=false,original=false;
    for(const QJsonValue &value:list) {
        const auto item=value.toObject();
        if(item.value("name").toString()==renamedTarget) renamed=true;
        if(item.value("id").toInteger()==originalId && item.value("unitPrice").toDouble()==2.5) original=true;
    }
    record(196,"真实界面回传落库为用户指定名称",renamed);
    record(200,"默认重命名保留原花型及其工价",original && renamed);
    overwrite=true; conflictDone=false; queueDone=false; middle=false; cleaning=false; activeChecked=false;
    for(int row=0;row<deviceTable->rowCount();++row) if(auto *check=deviceTable->cellWidget(row,0)->findChild<QCheckBox*>()) check->setChecked(false);
    fileRow=-1;
    wait([&]{for(int row=0;row<deviceTable->rowCount();++row) if(deviceTable->recordAt(row).value("fileName").toString()=="设备花型A.NTP") {fileRow=row;return true;}return false;});
    if(fileRow<0) return 18;
    deviceTable->cellClicked(fileRow,deviceTable->columnForKey("fileName")); conflict.start(); upload->click();
    if(!wait([&]{return conflictDone;})) return 19; conflict.stop();
    observe.start(); QTimer::singleShot(20,&window,[&]{if(!expectFailure)record(193,"回传入队后明确提示点击上传队列查看进度",wait([&]{auto *toast=window.findChild<QLabel*>("clientToast");return toast && toast->text().contains("上传队列") && toast->text().contains("查看进度");}));queue->click();});
    record(194,"通过实际覆盖弹窗与回传队列完成覆盖",wait([&]{return queueDone;},14000)); observe.stop();
    auto *serverPanel=window.findChild<QWidget*>("clientServerPatterns");
    auto *visiblePatterns=serverPanel ? dynamic_cast<ClientTable*>(serverPanel->findChild<QTableWidget*>("clientTable")) : nullptr;
    int originalRow=-1;
    const bool refreshed=wait([&]{
        if(!visiblePatterns) return false;
        for(int row=0;row<visiblePatterns->rowCount();++row) {
            const auto value=visiblePatterns->recordAt(row);
            if(value.value("id").toInteger()==originalId && value.value("uploadTime").toString()>originalTime) { originalRow=row; return true; }
        }
        return false;
    });
    record(195,"关闭回传队列后主列表自动显示新的上传时间",refreshed);
    record(194,"覆盖后主列表仍显示原类型和工价",refreshed && visiblePatterns->recordAt(originalRow).value("patternType").toString()=="原类型" && visiblePatterns->recordAt(originalRow).value("unitPrice").toDouble()==2.5);
    QPushButton *download=nullptr;
    if(refreshed) if(auto *actions=visiblePatterns->findChild<QTableWidget*>("clientFrozenActions"))
        if(auto *buttons=actions->cellWidget(originalRow,0)) for(auto *button:buttons->findChildren<QPushButton*>()) if(button->text()=="下载") download=button;
    const QString downloaded=files.filePath("设备回传下载.NTP");
    if(download) {
        QTimer::singleShot(40,&window,[&] {
            auto *chooser=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if(!chooser) qFatal("Pattern download chooser missing");
            auto *filename=chooser->findChild<QLineEdit*>("fileNameEdit");
            record(197,"回传NTP文件下载对话框默认保留NTP扩展名",filename && filename->text().endsWith(".NTP",Qt::CaseInsensitive));
            chooser->selectFile(downloaded); if(filename) filename->setText(downloaded);
            QMetaObject::invokeMethod(chooser,"accept",Qt::DirectConnection);
        });
        download->click();
    }
    const bool downloadedOk=wait([&]{return QFileInfo(downloaded).size()>0;});
    QFile result(downloaded); const bool opened=result.open(QIODevice::ReadOnly);
    QByteArray expectedBody;
    QFile expected(qEnvironmentVariable("BOER_PATTERN_FIXTURE"));
    if(!expected.fileName().isEmpty() && expected.open(QIODevice::ReadOnly)) expectedBody=expected.readAll();
    else { for(int i=0;i<256;++i) expectedBody.append(char(i)); expectedBody=expectedBody.repeated(30); }
    record(197,"设备NTP经回传再从页面下载，字节完全一致",downloadedOk && opened && result.readAll()==expectedBody);
    expectFailure=true; queueDone=false; middle=false; cleaning=false; activeChecked=false;
    int rejectedRow=-1;
    for(int row=0;row<deviceTable->rowCount();++row) {
        if(auto *check=deviceTable->cellWidget(row,0)->findChild<QCheckBox*>()) check->setChecked(false);
        if(deviceTable->recordAt(row).value("fileName").toString()=="设备花型B.NTP") rejectedRow=row;
    }
    if(rejectedRow<0) return 12;
    deviceTable->cellClicked(rejectedRow,deviceTable->columnForKey("fileName")); upload->click();
    observe.start(); QTimer::singleShot(20,&window,[&]{if(!expectFailure)record(193,"回传入队后明确提示点击上传队列查看进度",wait([&]{auto *toast=window.findChild<QLabel*>("clientToast");return toast && toast->text().contains("上传队列") && toast->text().contains("查看进度");}));queue->click();});
    record(214,"失败回传队列可更新并安全清理关闭",wait([&]{return queueDone;},12000)); observe.stop();
    auto *serverCard=window.findChild<QWidget*>("clientServerPatterns");
    auto *serverTable=serverCard ? dynamic_cast<ClientTable*>(serverCard->findChild<QTableWidget*>("clientTable")) : nullptr;
    if(!serverTable) return 13;
    QStringList paths;
    for(const QString &filename:{QString("拖动A.ntp"),QString("拖动B.slw"),QString("不支持.txt")}) {
        QFile file(files.filePath(filename)); if(!file.open(QIODevice::WriteOnly)) return 14;
        file.write("UI-DROP-CONTENT"); file.close(); paths.append(file.fileName());
    }
    QPushButton *add=nullptr;
    for(auto *button:serverCard->findChildren<QPushButton*>()) if(button->text()=="添加文件") add=button;
    if(!add) return 15;
    QTimer::singleShot(40,&window,[&] {
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto *drop=dialog ? dialog->findChild<QFrame*>("clientPatternDrop") : nullptr;
        auto *table=dialog ? dialog->findChild<QTableWidget*>("clientTable") : nullptr;
        QLineEdit *name=nullptr;
        if(dialog) for(auto *input:dialog->findChildren<QLineEdit*>()) if(input->placeholderText()=="请输入花型名称") name=input;
        if(!dialog || !drop || !table || !name) qFatal("Pattern upload dialog missing");
        QMimeData mime; QList<QUrl> urls; for(const QString &path:paths) urls.append(QUrl::fromLocalFile(path)); mime.setUrls(urls);
        QDragEnterEvent enter(QPoint(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(drop,&enter);
        QDropEvent event(QPointF(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(drop,&event);
        record(164,"拖入NTP和SLW花型均正确识别并列出",table->rowCount()==2);
        bool rejected=true; for(int row=0;row<table->rowCount();++row) if(table->item(row,0)->text().endsWith(".txt")) rejected=false;
        record(165,"混合拖入时拒绝TXT文件，不列入上传列表",table->rowCount()==2 && rejected);
        bool typePrompt=false;for(auto *combo:dialog->findChildren<QComboBox*>())typePrompt|=combo->itemText(0)=="请选择花型类型";record(158,"添加文件花型类型提示明确说明所选对象",typePrompt);

        name->setText(QString(256,QChar(0x82b1))); record(157,"上传名称输入框限制255字符",name->text().size()==255);
        name->clear(); app.processEvents(); auto *error=dialog->findChild<QLabel*>("promptError");
        record(166,"上传名称为空时立即显示内联错误",error && error->isVisible());
        QPushButton *submit=nullptr;
        for(auto *button:dialog->findChildren<QPushButton*>()) if(button->text()=="开始上传") submit=button;
        if(!submit) qFatal("Pattern upload submit missing"); submit->click();
        record(166,"空名称不关闭弹窗、不发起上传",dialog->isVisible());
        for(auto *stepper:dialog->findChildren<NumericStepper*>()) {
            stepper->setValue(1e20);
            record(stepper->decimals()==0 ? 160 : 161,"上传数值控件拦截越界值"+QString::number(stepper->decimals()),
                stepper->value()==(stepper->decimals()==0 ? 2147483647.0 : 999999999.999)); stepper->setValue(0);
        }
        name->setText("拖动测试A"); table->setCurrentCell(1,0); name->setText("拖动测试B"); name->setFocus();
        QKeyEvent submitKey(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier); QApplication::sendEvent(name,&submitKey);
    });
    add->click();
    record(164,"两个拖入文件通过页面提交后显示在服务器列表",wait([&]{return serverTable->rowCount()==4;}));
    const auto added=request("GET","/pattern/list?keyword=拖动测试").data.toObject().value("list").toArray();
    bool emptyMetadata=added.size()==2;
    for(const QJsonValue &value:added) emptyMetadata=emptyMetadata && value.toObject().value("patternType").toString().isEmpty() && value.toObject().value("orderNo").toString().isEmpty();
    record(168,"未选类型和订单时不把请选择提示文字保存到业务字段",emptyMetadata);
    if(added.isEmpty()) return 16;
    const qint64 previewId=added.first().toObject().value("id").toInteger();
    const QString longName(255,QChar(0x82b1));
    request("PUT","/pattern/"+QString::number(previewId),{{"name",longName},{"patternType",QString(100,QChar(0x7c7b))}});
    window.navigate("/file/pattern");
    int previewRow=-1;
    wait([&]{for(int row=0;row<serverTable->rowCount();++row) if(serverTable->recordAt(row).value("id").toInteger()==previewId && serverTable->recordAt(row).value("name").toString()==longName) { previewRow=row; return true; } return false;});
    auto *frozen=serverTable->findChild<QTableWidget*>("clientFrozenActions");
    QPushButton *preview=nullptr;
    if(frozen && previewRow>=0) if(auto *actions=frozen->cellWidget(previewRow,0))
        for(auto *button:actions->findChildren<QPushButton*>()) if(button->text()=="预览") preview=button;
    if(!preview) return 17;
    QPushButton *edit=nullptr;if(frozen && previewRow>=0)if(auto *actions=frozen->cellWidget(previewRow,0))for(auto *button:actions->findChildren<QPushButton*>())if(button->text()=="编辑")edit=button;
    record(185,"花型修改入口可达",edit!=nullptr);if(edit){QTimer::singleShot(40,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("pattern edit missing");QLineEdit *name=nullptr;QComboBox *type=nullptr;int numbers=0;for(auto *line:dialog->findChildren<QLineEdit*>())if(line->property("formFieldKey")=="name")name=line;for(auto *combo:dialog->findChildren<QComboBox*>())if(combo->property("formFieldKey")=="patternType")type=combo;for(auto *stepper:dialog->findChildren<NumericStepper*>()){stepper->setValue(stepper->decimals()==0 ? 12345 : 3.5);++numbers;}if(!name || !type)qFatal("pattern edit fields missing");name->clear();dialog->findChild<QPushButton*>("primaryButton")->click();record(185,"预览相邻修改入口使用同一元数据表单，空名禁止提交",dialog->isVisible() && dialog->findChild<QLabel*>("promptError")->isVisible());name->setText(longName);type->setCurrentText("复测类型");record(185,"修改表单提供类型针数工价及订单字段",numbers==2 && dialog->findChildren<QComboBox*>().size()>=2);dialog->findChild<QPushButton*>("primaryButton")->click();});edit->click();record(185,"实际修改针数12345、工价3.5及类型后服务器保存一致",wait([&]{const auto saved=request("GET","/pattern/"+QString::number(previewId)).data.toObject();return saved.value("stitches").toInt()==12345 && saved.value("unitPrice").toDouble()==3.5 && saved.value("patternType")=="复测类型";}));}else record(185,"花型修改入口可达",false);

    preview=nullptr;wait([&]{for(int row=0;row<serverTable->rowCount();++row)if(serverTable->recordAt(row).value("id").toInteger()==previewId && serverTable->recordAt(row).value("patternType")=="复测类型"){previewRow=row;if(auto *actions=frozen->cellWidget(row,0))for(auto *button:actions->findChildren<QPushButton*>())if(button->text()=="预览")preview=button;}return preview!=nullptr;});if(!preview)return 51;
    bool aligned=true;
    for(int column=0;column<serverTable->columnCount();++column) if(serverTable->item(previewRow,column) && serverTable->horizontalHeaderItem(column))
        aligned=aligned && (serverTable->item(previewRow,column)->textAlignment()&Qt::AlignHorizontal_Mask)==
            (serverTable->horizontalHeaderItem(column)->textAlignment()&Qt::AlignHorizontal_Mask);
    record(181,"花型列表各数据列与表头采用一致对齐方式",aligned);
    record(180,"超长花型名称在列表中保留完整Hover文本",serverTable->item(previewRow,serverTable->columnForKey("name"))->toolTip()==longName);
    QTimer::singleShot(40,&window,[&] {
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog) qFatal("Pattern preview missing");
        bool label=false,wrapped=false;
        for(auto *value:dialog->findChildren<QLabel*>()) {
            if(value->text()=="花型名称") label=true;
            if(value->text()==longName) wrapped=value->wordWrap() && value->toolTip()==longName;
        }
        auto *scroll=dialog->findChild<QScrollArea*>("patternPreviewScroll");
        record(183,"预览信息字段统一为花型名称",label);
        record(184,"超长预览文本换行、保留Hover原文并在固定弹窗内滚动",wrapped && scroll && dialog->height()<=520 && !scroll->horizontalScrollBar()->isVisible());
        const QString screenshot=QFileInfo(log.fileName()).absolutePath()+"/pattern-preview-long.png"; dialog->grab().save(screenshot);
        dialog->reject();
    });
    preview->click();
    int sampleRow=-1;
    for(int row=0;row<serverTable->rowCount();++row) if(serverTable->recordAt(row).value("id").toInteger()==originalId) sampleRow=row;
    QPushButton *samplePreview=nullptr;
    if(frozen && sampleRow>=0) if(auto *actions=frozen->cellWidget(sampleRow,0))
        for(auto *button:actions->findChildren<QPushButton*>()) if(button->text()=="预览") samplePreview=button;
    if(samplePreview) {
        QTimer::singleShot(40,&window,[&] {
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog) qFatal("Real sample preview missing");
            if(dialog->findChild<QLabel*>("patternPreviewUnavailable")) {
                log.write(QJsonDocument(QJsonObject{{"row",182},{"name","图形预览尚未实现，当前仅有文件信息与明确提示"},{"result","BLOCKED"},
                    {"scope","实际打开仓库NTP样本回传后的Qt预览弹窗"},{"reason","已找到旧版NTP样本并做坐标研究；仍缺SDK/可靠的指令语义与其他格式规范，不能将候选轨迹当作正式预览"}}).toJson(QJsonDocument::Compact)+'\n'); log.flush();
            }
            dialog->reject();
        });
        samplePreview->click();
    }
    int total=request("GET","/pattern/list").data.toObject().value("total").toInt();
    for(int number=total;number<235;++number) {
        auto response=std::make_shared<ApiResponse>(); auto done=std::make_shared<bool>(false);
        api->uploadFile("/pattern/upload",source.fileName(),{{"name","分页验证"+QString::number(number)}},
            [response,done](const ApiResponse &value){ *response=value; *done=true; });
        if(!wait([&]{return *done;}) || !response->ok) return 20;
    }
    record(188,"构造235个真实服务端花型记录",request("GET","/pattern/list").data.toObject().value("total").toInt()==235);
    window.navigate("/file/pattern");
    auto *pagination=serverCard->findChild<ClientPagination*>(); auto *size=pagination ? pagination->findChild<QComboBox*>() : nullptr;
    if(!pagination || !size) return 21;
    size->setCurrentIndex(size->findData(10));
    record(188,"235条数据按10条每页显示",wait([&]{return serverTable->rowCount()==10;}));
    auto *jump=pagination->findChild<QLineEdit*>();
    if(jump) { jump->setText("10"); QMetaObject::invokeMethod(jump,"returnPressed",Qt::DirectConnection); }
    record(188,"跳至第10页显示第91条起的数据",jump && wait([&]{return pagination->page()==10 && serverTable->item(0,serverTable->columnForKey("#"))->text()=="91";}));
    size->setCurrentIndex(size->findData(100));
    record(188,"从第10页切100条后回第一页并刷新100条",wait([&]{return pagination->page()==1 && serverTable->rowCount()==100 && serverTable->item(0,serverTable->columnForKey("#"))->text()=="1";}));
    const bool reachable=wait([&] {
        serverTable->doItemsLayout();
        serverTable->verticalScrollBar()->setValue(serverTable->verticalScrollBar()->maximum()); app.processEvents();
        const QRect last=serverTable->visualItemRect(serverTable->item(99,serverTable->columnForKey("name")));
        return serverTable->verticalScrollBar()->maximum()>0 && last.top()>=0 && last.bottom()<=serverTable->viewport()->height();
    });
    if(!reachable) qInfo("PATTERN_SCROLL value=%d max=%d viewport=%d lastBottom=%d",serverTable->verticalScrollBar()->value(),
        serverTable->verticalScrollBar()->maximum(),serverTable->viewport()->height(),serverTable->visualItemRect(serverTable->item(99,serverTable->columnForKey("name"))).bottom());
    record(188,"100条页内可滚动访问最后一行",reachable);
    const auto offline=request("POST","/device",{{"code","queue-offline-many"},{"name",QString(100,QChar(0x8bbe))}}).data.toObject().value("id");
    QJsonArray selected;
    for(const auto &item:request("GET","/pattern/list").data.toObject().value("list").toArray()) {
        if(item.toObject().value("name").toString().size()<=22)selected.append(item.toObject().value("id"));
    }
    if(selected.isEmpty())return 22;
    if(!request("POST","/pattern/batch-download",{{"patternIds",selected},{"deviceIds",QJsonArray{offline}}}).ok)return 23;
    int taskTotal=request("GET","/pattern/queue").data.toObject().value("total").toInt();
    while(taskTotal<235) {if(!request("POST","/pattern/download",{{"patternId",selected.first()},{"deviceIds",QJsonArray{offline}}}).ok)return 24;++taskTotal;}
    window.navigate("/file/queue");app.processEvents();ClientTable *downloadTable=nullptr;ClientPagination *downloadPages=nullptr;
    for(auto *widget:window.findChildren<QTableWidget*>())if(auto *candidate=dynamic_cast<ClientTable*>(widget))if(candidate->isVisible() && candidate->columnForKey("progress")>=0)downloadTable=candidate;
    for(auto *candidate:window.findChildren<ClientPagination*>())if(candidate->isVisible())downloadPages=candidate;
    if(!downloadTable || !downloadPages)return 25;
    auto *downloadSize=downloadPages->findChild<QComboBox*>();downloadSize->setCurrentIndex(downloadSize->findData(10));
    record(266,"235条下发任务按10条每页显示",wait([&]{return downloadTable->rowCount()==10;}));
    auto *downloadJump=downloadPages->findChild<QLineEdit*>();downloadJump->setText("10");QMetaObject::invokeMethod(downloadJump,"returnPressed",Qt::DirectConnection);
    record(266,"下发队列跳第10页首条序号91",wait([&]{return downloadPages->page()==10 && downloadTable->item(0,downloadTable->columnForKey("#"))->text()=="91";}));
    downloadSize->setCurrentIndex(downloadSize->findData(100));
    record(266,"下发队列切100条回第一页并刷新100条",wait([&]{return downloadPages->page()==1 && downloadTable->rowCount()==100;}));
    record(262,"下发队列100字目标设备名可Hover查看全文",downloadTable->item(0,downloadTable->columnForKey("deviceName"))->toolTip()==QString(100,QChar(0x8bbe)));
    QPushButton *clearCompleted=nullptr;for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()=="清除已完成")clearCompleted=button;
    if(clearCompleted) {
        QTimer::singleShot(40,&window,[&] {auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());record(261,"下发队列清除已完成前显示二次确认",dialog && dialog->windowTitle()=="清除已完成");if(dialog)dialog->reject();});
        clearCompleted->click();
    } else record(261,"下发队列清除已完成前显示二次确认",false);
    record(261,"取消清除后235条任务保持",request("GET","/pattern/queue").data.toObject().value("total").toInt()==235);
    UiI18n::instance()->setEnglish(true);app.processEvents();
    bool timestamp=false;for(auto *label:window.findChildren<QLabel*>())if(label->isVisible() && label->text().startsWith("Last"))timestamp=timestamp || QRegularExpression("[0-9]{2}:[0-9]{2}:[0-9]{2}").match(label->text()).hasMatch();
    record(270,"切英文后下发队列最后刷新时间仍保留",timestamp);
    bool messagesEnglish=true;for(int r=0;r<downloadTable->rowCount();++r)messagesEnglish=messagesEnglish && !downloadTable->item(r,downloadTable->columnForKey("message"))->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));
    record(268,"下发队列英文备注列没有残留中文控制消息",messagesEnglish);
    bool liveEnglish=true;for(int r=0;r<downloadTable->rowCount();++r)liveEnglish&=!downloadTable->item(r,downloadTable->columnForKey("status"))->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));record(269,"下发队列刷新后的实际任务状态随英文翻译，业务文件名保留",liveEnglish && messagesEnglish);
    if(clearCompleted){QTimer::singleShot(40,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());bool translated=dialog;if(dialog){for(auto *label:dialog->findChildren<QLabel*>())translated&=!label->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));for(auto *button:dialog->findChildren<QPushButton*>())translated&=!button->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));dialog->reject();}record(267,"英文下发队列实际清理确认提示及按钮无中文残留",translated);});clearCompleted->click();}

    UiI18n::instance()->setEnglish(false);app.processEvents();timestamp=false;
    for(auto *label:window.findChildren<QLabel*>())if(label->isVisible() && label->text().startsWith("最后刷新"))timestamp=QRegularExpression("[0-9]{2}:[0-9]{2}:[0-9]{2}").match(label->text()).hasMatch();
    record(270,"切回中文后最后刷新时间仍保留",timestamp);
    const auto another=request("POST","/device",{{"code","queue-offline-third"},{"name","第三离线设备"}}).data.toObject().value("id").toInteger();
    window.navigate("/file/pattern");app.processEvents();QTreeWidget *targets=nullptr;
    for(auto *candidate:window.findChildren<QTreeWidget*>("groupTree"))if(candidate->isVisible())targets=candidate;
    auto findDevice=[&](qint64 id){if(targets)for(QTreeWidgetItemIterator it(targets);*it;++it)if((*it)->data(0,Qt::UserRole+1)=="device" && (*it)->data(0,Qt::UserRole).toLongLong()==id)return *it;return static_cast<QTreeWidgetItem*>(nullptr);};
    wait([&]{return findDevice(another) && findDevice(offline.toInteger());});
    auto *firstTarget=findDevice(offline.toInteger()),*secondTarget=findDevice(another);
    if(!firstTarget || !secondTarget)return 26;
    firstTarget->setCheckState(0,Qt::Checked);targets->scrollToItem(secondTarget);app.processEvents();
    const auto targetRect=targets->visualItemRect(secondTarget);const QPointF targetPoint(targetRect.left()+70,targetRect.center().y());
    QMouseEvent press(QEvent::MouseButtonPress,targetPoint,targets->viewport()->mapToGlobal(targetPoint.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,targetPoint,targets->viewport()->mapToGlobal(targetPoint.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(targets->viewport(),&press);QApplication::sendEvent(targets->viewport(),&release);app.processEvents();
    record(150,"方框选第一设备再点击第二设备名称，两个勾选均保留",firstTarget->checkState(0)==Qt::Checked && secondTarget->checkState(0)==Qt::Checked);
    record(189,"100字设备名称在树中保留完整Hover文本",firstTarget->toolTip(0)==QString(100,QChar(0x8bbe)));
    record(179,"花型列表名称字段明确显示花型名称",serverTable->horizontalHeaderItem(serverTable->columnForKey("name"))->text()=="花型名称");
    QPushButton *send=nullptr,*clearTargets=nullptr;
    for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible()){if(button->text()=="批量下发")send=button;if(button->text()=="清空")clearTargets=button;}
    if(!send || !clearTargets || !wait([&]{return serverTable->rowCount()==100;}))return 27;
    // A real tree click sets currentItem before itemClicked. Keep the fixture
    // consistent so the live status poll cannot restore a different current row.
    targets->setCurrentItem(firstTarget);targets->itemClicked(firstTarget,0);app.processEvents();QLabel *selectionLabel=nullptr;
    for(auto *label:window.findChildren<QLabel*>())if(label->isVisible() && label->property("selectionSource").isValid())selectionLabel=label;
    record(189,"超长当前设备名称省略显示，Hover保留全文且清空按钮仍在窗口内",wait([&]{return selectionLabel && selectionLabel->toolTip()==QString(100,QChar(0x8bbe)) && selectionLabel->text().endsWith(QChar(0x2026)) && window.rect().contains(QRect(clearTargets->mapTo(&window,QPoint()),clearTargets->size()));}));
    const auto patternOne=serverTable->recordAt(0).value("id").toInteger(),patternTwo=serverTable->recordAt(1).value("id").toInteger();
    serverTable->cellClicked(0,serverTable->columnForKey("name"));serverTable->cellClicked(1,serverTable->columnForKey("name"));
    auto queuedIds=[&]{QSet<qint64> ids;for(const auto &value:request("GET","/pattern/queue").data.toObject().value("list").toArray())ids.insert(value.toObject().value("id").toInteger());return ids;};
    const auto previousIds=queuedIds();bool extraPrompt=false;QTimer closeUnexpected;closeUnexpected.setSingleShot(true);
    QObject::connect(&closeUnexpected,&QTimer::timeout,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(dialog && dialog->windowTitle()=="选择目标设备"){extraPrompt=true;dialog->reject();}});closeUnexpected.start(30);send->click();closeUnexpected.stop();
    record(187,"左侧已选设备时批量下发不再弹重复选择窗口",!extraPrompt);
    if(!extraPrompt) {
        wait([&]{return queuedIds().size()>=previousIds.size()+4;});QSet<QString> pairs;
        for(const auto &value:request("GET","/pattern/queue").data.toObject().value("list").toArray()){const auto row=value.toObject();if(!previousIds.contains(row.value("id").toInteger()))pairs.insert(QString::number(row.value("patternId").toInteger())+"/"+QString::number(row.value("deviceId").toInteger()));}
        record(171,"批量下发准确生成两个花型乘两个所选设备的4个任务",pairs==QSet<QString>{QString::number(patternOne)+"/"+QString::number(offline.toInteger()),QString::number(patternOne)+"/"+QString::number(another),QString::number(patternTwo)+"/"+QString::number(offline.toInteger()),QString::number(patternTwo)+"/"+QString::number(another)});
    }
    clearTargets->click();app.processEvents();bool unchecked=true;for(QTreeWidgetItemIterator it(targets);*it;++it)unchecked=unchecked && (*it)->checkState(0)==Qt::Unchecked;
    record(152,"清空设备选择同时清除所有设备勾选并重置当前设备",unchecked && deviceCombo->currentIndex()==0);
    for(QTreeWidgetItemIterator it(targets);*it;++it)(*it)->setCheckState(0,Qt::Unchecked);
    bool chooseShown=false;QTimer selectTargets;selectTargets.setSingleShot(true);
    QObject::connect(&selectTargets,&QTimer::timeout,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;chooseShown=dialog->windowTitle()=="选择目标设备";auto *choices=dialog->findChild<QTreeWidget*>();if(choices)for(QTreeWidgetItemIterator it(choices);*it;++it)if(QSet<qint64>{offline.toInteger(),another}.contains((*it)->data(0,Qt::UserRole).toLongLong()))(*it)->setCheckState(0,Qt::Checked);for(auto *button:dialog->findChildren<QPushButton*>())if(button->text()=="确定"){button->click();return;}});
    const auto beforeChoose=queuedIds();selectTargets.start(30);send->click();selectTargets.stop();
    record(187,"左侧未选设备时显示目标设备选择窗口",chooseShown);
    record(186,"选择窗口勾选两个设备后实际生成对应4个批量任务",chooseShown && wait([&]{return queuedIds().size()>=beforeChoose.size()+4;}));
    window.navigate("/file/log");app.processEvents();ClientTable *history=nullptr;
    for(auto *widget:window.findChildren<QTableWidget*>())if(auto *candidate=dynamic_cast<ClientTable*>(widget))if(candidate->isVisible() && candidate->columnForKey("message")>=0)history=candidate;
    wait([&]{return history && history->rowCount()>0;});UiI18n::instance()->setEnglish(true);app.processEvents();
    bool historyEnglish=history && history->rowCount()>0;if(history)for(int r=0;r<history->rowCount();++r)historyEnglish=historyEnglish && !history->item(r,history->columnForKey("message"))->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));
    record(272,"下发日志英文备注列不残留中文控制消息",historyEnglish);UiI18n::instance()->setEnglish(false);

    window.navigate("/file/pattern");app.processEvents();
    auto findButton=[&](const QString &source){const auto title=UiI18n::instance()->text(source);for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()==title)return button;return static_cast<QPushButton*>(nullptr);};
    auto translatedDialog=[&](int row,const QString &name) {auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());bool valid=dialog;const QRegularExpression chinese("[\\x{4e00}-\\x{9fff}]");if(dialog){for(auto *label:dialog->findChildren<QLabel*>())if(label->isVisible() && !label->property("i18nSkip").toBool())valid&=!label->text().contains(chinese);for(auto *button:dialog->findChildren<QPushButton*>())if(button->isVisible())valid&=!button->text().contains(chinese);}record(row,name,valid);if(dialog)dialog->reject();};
    for(bool english:{false,true}) {
        UiI18n::instance()->setEnglish(english);app.processEvents();auto *download=findButton("全部下载到本地");if(!download)return 27;
        const QString target=files.filePath(english ? "download-en.zip" : "download-zh.zip");
        QTimer::singleShot(40,&window,[&]{auto *dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("all patterns save missing");const QString expected=english ? "ServerPatterns.zip" : "服务器花型文件.zip";
            record(177,english ? "英文全部下载默认文件包名称英文" : "中文全部下载默认文件包名称中文",QFileInfo(dialog->selectedFiles().value(0)).fileName()==expected);
            record(177,english ? "英文文件保存弹窗按钮使用Save" : "中文文件保存弹窗按钮使用保存",dialog->labelText(QFileDialog::Accept)==(english ? "Save" : "保存"));dialog->selectFile(target);QMetaObject::invokeMethod(dialog,"accept");});download->click();
        QFile archive(target);const bool downloaded=wait([&]{return QFileInfo(target).size()>0;});const bool opened=archive.open(QIODevice::ReadOnly);record(177,english ? "英文界面实际下载ZIP字节有效" : "中文界面实际下载ZIP字节有效",downloaded && opened && archive.read(4)==QByteArray("PK\3\4",4));
        record(177,english ? "英文ZIP下载完成提示翻译正常" : "中文ZIP下载完成提示正常",wait([&]{auto *toast=window.findChild<QLabel*>("clientToast");return toast && toast->text()==UiI18n::instance()->text("文件已下载");}));
    }
    int legends=0;for(auto *label:window.findChildren<QLabel*>("patternStatusLegend"))legends+=label->isVisible();auto *legendDevice=findDevice(offline.toInteger());record(154,"花型页面设备树提供三态颜色图例和设备状态图标",legends==3 && legendDevice && !legendDevice->icon(0).isNull());
    auto *addEnglish=findButton("添加文件");if(!addEnglish)return 28;QTimer::singleShot(40,&window,[&]{translatedDialog(226,"英文添加文件弹窗字段、文件拖放说明和按钮不残留中文");});addEnglish->click();
    auto *actualServer=window.findChild<QWidget*>("clientServerPatterns");auto *actualTable=actualServer ? dynamic_cast<ClientTable*>(actualServer->findChild<QTableWidget*>("clientTable")) : nullptr;
    if(actualTable && actualTable->rowCount()>0)actualTable->cellClicked(0,actualTable->columnForKey("name"));auto *batchEnglish=findButton("批量修改");if(!batchEnglish)return 29;QTimer::singleShot(40,&window,[&]{translatedDialog(227,"英文批量修改弹窗字段、说明和按钮不残留中文");});batchEnglish->click();
    UiI18n::instance()->setEnglish(false);app.processEvents();auto *locate=findButton("定位设备文件模块");if(!locate)return 30;locate->click();app.processEvents();record(178,"定位设备文件模块实际滚动至设备文件区域",deviceTable->isVisible() && window.rect().intersects(QRect(deviceTable->mapTo(&window,QPoint()),deviceTable->size())));
    return failures ? 1 : 0;
}
