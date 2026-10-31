#include "api_server.h"
#include "database.h"
#include "device_server.h"
#include "editor_dialog.h"
#include "external_db.h"
#include "main_window.h"
#include "ui_controls.h"
#include "ui_dialogs.h"
#include "permission_labels.h"
#include "upload_summary.h"
#include "window_compatibility.h"
#include "interaction_compatibility.h"
#include <QProgressBar>
#include <QInputMethodEvent>
#include <QScrollBar>
#include <QCalendarWidget>
#include <QClipboard>
#include <QTextEdit>
#include "ui_i18n.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QProgressDialog>
#include <QStackedWidget>
#include <QRadioButton>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QKeyEvent>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTcpSocket>
#include <QtEndian>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <functional>

namespace {
template<class T> T *field(QObject *root,const char *property,const QString &value) {
    for(auto *widget:root->findChildren<T*>()) if(widget->property(property).toString()==value) return widget;
    qFatal("Missing customer regression field %s",qPrintable(value)); return nullptr;
}
void events(int duration=60) { QElapsedTimer timer; timer.start(); do { QApplication::processEvents(); QThread::msleep(2); } while(timer.elapsed()<duration); }
bool waitUntil(std::function<bool()> condition) { QElapsedTimer timer; timer.start(); while(!condition() && timer.elapsed()<15000) events(20); return condition(); }
}
int runCustomerUiRegression() {
    UiI18n::instance()->setEnglish(false);
    int failures=0;
    auto check=[&](int row,const QString &name,bool passed,const QString &detail=QString()) {
        QJsonObject result{{"row",row},{"name",name},{"result",passed ? "PASS" : "FAIL"},{"detail",detail},
            {"scope","原生Qt服务端实际窗口控件；独立临时SQLite；数据库可写权限另用隔离MySQL实测"}};
        QFile output(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG"));
        if(!output.fileName().isEmpty() && output.open(QIODevice::Append)) { output.write(QJsonDocument(result).toJson(QJsonDocument::Compact)+"\n"); output.flush(); }
        qInfo("row %d %s %s",row,passed ? "PASS" : "FAIL",qPrintable(name)); failures+=!passed;
    };
    QTemporaryDir directory; Database db; QString error;
    if(!directory.isValid() || !db.open(directory.filePath("server.db"),&error)) return 2;
    for(int i=0;i<25;++i) {
        qint64 id;
        if(!db.insert("roles",{{"name",QString("r%1").arg(i)},{"remark",QString(180,'x')}},&id,&error) ||
           !db.insert("users",{{"username",QString("u%1").arg(i)},{"password","123456"},{"nickname",QString("用户%1").arg(i)},{"role","user"}},&id,&error) ||
           !db.insert("devices",{{"code",QString("d%1").arg(i)},{"name",i==24 ? QString("专用搜索") : QString("设备%1").arg(i)},{"groupId",1},{"status",i==0 ? "working" : i==1 ? "idle" : "offline"},{"currentSpeed",i==0 ? 2000 : 0},{"maxSpeedValue",3500},{"productionCurrent",3},{"productionTotal",8}},&id,&error) ||
           !db.insert("login_logs",{{"username",QString("log%1").arg(i)},{"ip","127.0.0.1"},{"status","success"},{"loginTime",QString("2026-10-%1 12:00:00").arg(25-i,2,10,QChar('0'))}},&id,&error)) { qCritical("fixture %s",qPrintable(error)); return 2; }
    }
    qint64 parent=1;for(int level=0;level<10;++level){qint64 id;if(!db.insert("groups",{{"name",QString("第%1层分组").arg(level+1)},{"parentId",parent}},&id,&error))return 2;parent=id;}
    if(qEnvironmentVariableIsSet("BOER_TEST_INTERACTION_COMPAT"))for(int i=0;i<1200;++i)if(!db.insert("devices",{{"code",QString("large-%1").arg(i)},{"name",QString("设备%1").arg(i)+QString(85,QChar(0x8bbe))},{"remark",QString(500,QChar(0x6ce8))},{"groupId",1}},nullptr,&error))return 2;
    ApiServer api(&db); DeviceServer devices(&db); MainWindow window(&db,&api,&devices); window.resize(1300,780); window.show(); events();
    auto *nav=window.findChild<QListWidget*>("navigation");
    if(qEnvironmentVariableIsSet("BOER_TEST_INTERACTION_COMPAT")){
        nav->setCurrentRow(5);events();
        return runInteractionCompatibility(window,"server",[&](const QString &message,bool error){window.showFeedback(message,error);},[&]{
            for(bool english:{false,true}){
                UiI18n::instance()->setEnglish(english);
                QTimer::singleShot(80,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;QLineEdit *code=nullptr,*name=nullptr;for(auto *line:dialog->findChildren<QLineEdit*>()){if(line->property("editorField")=="设备编号")code=line;if(line->property("editorField")=="设备名称")name=line;}if(!code || !name){dialog->done(QDialog::Rejected);return;}code->setText("KEYBOARD001");name->setFocus();QInputMethodEvent commit;commit.setCommitString("键盘设备");QApplication::sendEvent(name,&commit);sendInteractionKey(name,Qt::Key_Return);waitInteraction();if(dialog->isVisible())dialog->done(QDialog::Rejected);});
                const auto saved=showEditorDialog(&db,"devices",{},true,&window);recordInteraction("server",QString(english ? "en" : "zh")+" 真实设备表单Enter保存而非取消",saved && saved->value("name")=="键盘设备" && saved->value("code")=="KEYBOARD001");
            }
            nav->setCurrentRow(5);events();auto *size=field<QComboBox>(&window,"customerPageSize","devices");size->setCurrentIndex(size->findData(100));events();auto *table=field<QTableWidget>(&window,"customerTable","devices");
            auto idAt=[&](int row){return table->item(row,0)->data(Qt::UserRole).toLongLong();};
            const qint64 first=idAt(0);table->cellWidget(0,0)->findChild<QCheckBox*>("rowCheckBox")->setChecked(true);table->verticalScrollBar()->setValue(table->verticalScrollBar()->maximum());events();const auto lastRect=table->visualItemRect(table->item(99,2));
            recordInteraction("server","1225设备长文本按100条分页且末行可滚动访问",db.count("devices")==1225 && table->rowCount()==100 && lastRect.top()>=0 && lastRect.bottom()<=table->viewport()->height());
            auto *next=field<QPushButton>(&window,"customerNext","devices");next->click();events();const qint64 second=idAt(0);table->cellWidget(0,0)->findChild<QCheckBox*>("rowCheckBox")->setChecked(true);UiI18n::instance()->setEnglish(false);events();UiI18n::instance()->setEnglish(true);events();window.resize(1120,700);events();
            recordInteraction("server","大数据分页语言切换及窄窗口保留不同页面逻辑选择",first!=second && window.selectedDevices_.contains(first) && window.selectedDevices_.contains(second));
            auto *previous=field<QPushButton>(&window,"customerPrevious","devices");previous->click();events();recordInteraction("server","返回原页仍回显原设备选择",idAt(0)==first && table->cellWidget(0,0)->findChild<QCheckBox*>("rowCheckBox")->isChecked());
            const int deviceIndex=window.tables_.indexOf(table);qint64 scopeCount=-1;if(auto *note=window.contextNotes_.value(deviceIndex)){const auto found=QRegularExpression("[0-9]+").match(note->text());if(found.hasMatch())scopeCount=found.captured().toLongLong();}
            recordInteraction("server","大数据当前范围总数为1225，不静默截断到1000",scopeCount==1225,QString("displayed=%1 actual=%2").arg(scopeCount).arg(db.count("devices")));
            int treeDevices=0;for(QTreeWidgetItemIterator it(window.trees_.value(deviceIndex));*it;++it)treeDevices+=(*it)->data(0,Qt::UserRole+1)=="device";
            recordInteraction("server","大数据设备树包含全部1225设备",treeDevices==1225,QString("tree=%1").arg(treeDevices));
            int turns=0;while(next->isEnabled() && turns++<20){next->click();events(20);}
            recordInteraction("server","1225设备最后一页25条且包含最早ID1",table->rowCount()==25 && idAt(24)==1,QString("last_page_rows=%1").arg(table->rowCount()));
            auto *keyword=window.filters_.value(deviceIndex);keyword->setText("d0");sendInteractionKey(keyword,Qt::Key_Return);events();
            recordInteraction("server","可搜索到原1000条截断范围之外的早期设备d0",table->rowCount()==1 && idAt(0)==1,QString("found=%1").arg(table->rowCount()));
            keyword->clear();sendInteractionKey(keyword,Qt::Key_Return);events();
            window.grab().save(qEnvironmentVariable("BOER_INTERACTION_ARTIFACT_DIR")+"/server-large-data.png");
        });
    }
    if(qEnvironmentVariableIsSet("BOER_TEST_WINDOW_COMPAT")) {
        QStringList pages;for(int index=0;index<nav->count();++index)pages<<QString::number(index);
        return runWindowCompatibility(window,"server",pages,[&](const QString &page){nav->setCurrentRow(page.toInt());},[&]{
            for(bool english:{false,true}){UiI18n::instance()->setEnglish(english);for(const QString &table:{QString("devices"),QString("users"),QString("roles")}){scheduleWindowDialogCheck(window,"server",table+(english ? "-en" : "-zh"));showEditorDialog(&db,table,{},true,&window);}}
            window.showFeedback(QString("Long notification keeps all details visible after resizing. ").repeated(6),true);checkWindowNotification(window,"server","actionToast");
        });
    }
    for(const auto &entry:QList<QPair<QString,QPair<int,int>>>{{"roles",{3,400}},{"users",{4,414}},{"devices",{5,474}},{"login_logs",{6,478}}}) {
        nav->setCurrentRow(entry.second.first); events();
        auto *table=field<QTableWidget>(&window,"customerTable",entry.first);
        auto *size=field<QComboBox>(&window,"customerPageSize",entry.first);
        auto *next=field<QPushButton>(&window,"customerNext",entry.first);
        auto *previous=field<QPushButton>(&window,"customerPrevious",entry.first);
        const auto first=table->item(0,1) ? table->item(0,1)->text() : QString();
        check(entry.second.second,entry.first+"超过20条显示20条且可下一页",table->isVisible() && table->rowCount()==20 && next->isEnabled() && !previous->isEnabled());
        next->click(); events();
        check(entry.second.second,entry.first+"第二页有剩余数据且返回后首条相同",table->rowCount()>0 && table->rowCount()<20 && !next->isEnabled() && previous->isEnabled());
        previous->click(); events(); check(entry.second.second,entry.first+"上一页恢复首条数据",table->item(0,1) && table->item(0,1)->text()==first);
        size->setCurrentIndex(3); events(); table->doItemsLayout(); table->scrollToBottom(); events();
        auto *last=table->item(table->rowCount()-1,0);
        check(entry.second.second,entry.first+"100条模式全部数据可滚动至最后一行",last && table->rowCount()>=25 && table->visualItemRect(last).intersects(table->viewport()->rect()));
        if(entry.first=="login_logs") {
            int column=-1; for(int c=0;c<table->columnCount();++c) if(table->horizontalHeaderItem(c)->text().contains("时间")) column=c;
            bool descending=column>=0; for(int r=1;r<table->rowCount() && descending;++r) descending=table->item(r-1,column)->text()>=table->item(r,column)->text();
            check(477,"登录日志按登录时间倒序而非插入顺序",descending);
        }
        if(entry.first=="roles") {
            bool complete=false;for(int r=0;r<table->rowCount();++r)if(table->item(r,6)->text().size()==180)complete|=table->item(r,6)->toolTip()==table->item(r,6)->text();
            check(401,"原生角色长备注保留完整Hover文本",complete);
        }
        if(entry.first=="devices") {
            bool working=false,idle=false,offline=false,speed=false;
            for(int r=0;r<table->rowCount();++r) {
                for(auto *label:table->cellWidget(r,11)->findChildren<QLabel*>()) {
                    if(label->text()=="缝纫")working=label->objectName()=="statusPillDanger";
                    if(label->text()=="空闲")idle=label->objectName()=="statusPillSuccess";
                    if(label->text()=="关机")offline=label->objectName()=="statusPillInfo";
                }
                if(table->item(r,6)->text()=="d0")speed=table->item(r,14)->text()=="2000/3500";
            }
            check(466,"服务端设备状态颜色缝纫红、空闲绿、关机灰",working && idle && offline);
            check(467,"服务端速度显示当前2000/最大3500",speed);
            check(468,"服务端产量明确标注当前/总生产量，3/8件顺序正确",table->horizontalHeaderItem(15)->text()=="当前/总生产量(件)" && table->item(0,15)->text()=="3/8");
            QTreeWidget *deviceTree=nullptr;for(auto *candidate:window.findChildren<QTreeWidget*>("groupTree"))if(candidate->isVisible())deviceTree=candidate;
            QTreeWidgetItem *deepest=nullptr;bool stateIcons=false;
            if(deviceTree)for(QTreeWidgetItemIterator it(deviceTree);*it;++it){if((*it)->data(0,Qt::UserRole+1)=="group" && (*it)->data(0,Qt::UserRole).toLongLong()==parent)deepest=*it;if((*it)->data(0,Qt::UserRole+1)=="device")stateIcons|=!(*it)->icon(0).isNull();}
            int legend=0;for(auto *label:window.findChildren<QLabel*>("nativeDeviceStatusLegend"))legend+=label->isVisible();
            check(439,"服务端设备树显示状态图标和三色说明",stateIcons && legend==3);
            if(deepest){deviceTree->scrollToItem(deepest);deviceTree->horizontalScrollBar()->setValue(deviceTree->horizontalScrollBar()->maximum());events();}
            check(461,"服务端10级分组可横向滚动并显示完整名称",deepest && deviceTree->horizontalScrollBar()->maximum()>0 && deviceTree->visualItemRect(deepest).width()>=deviceTree->fontMetrics().horizontalAdvance(deepest->text(0))+24);
            auto *search=field<QLineEdit>(&window,"customerFilter","devices");search->setText("专用搜索搜索");QMetaObject::invokeMethod(search,"returnPressed");events();
            check(96,"原生服务端设备搜索也采用双向包含",table->rowCount()==1 && table->item(0,2)->text()=="专用搜索");
            search->clear();QMetaObject::invokeMethod(search,"returnPressed");events();
        }
    }
    nav->setCurrentRow(5);events();QTreeWidget *nativeGroups=nullptr;
    for(auto *tree:window.findChildren<QTreeWidget*>("groupTree"))if(tree->isVisible())nativeGroups=tree;
    QTreeWidgetItem *ungrouped=nullptr;
    if(nativeGroups)for(QTreeWidgetItemIterator it(nativeGroups);*it;++it)if((*it)->data(0,Qt::UserRole+1)=="ungrouped")ungrouped=*it;
    if(!nativeGroups || !ungrouped)qFatal("native ungrouped context missing");
    nativeGroups->scrollToItem(ungrouped);events();
    QTimer::singleShot(20,[&] {
        auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)qFatal("native root group menu missing");QAction *root=nullptr;bool noVirtualChild=true;
        for(auto *action:menu->actions()){if(action->text()=="新增分组")root=action;noVirtualChild&=action->text()!="新增子分组";}
        check(441,"虚拟未分组节点提供新增顶级分组，避免作为实体父级",root && noVirtualChild);
        if(!root)qFatal("native root action missing");
        QTimer::singleShot(20,[&] {
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("native root editor missing");
            auto *parent=field<QComboBox>(dialog,"editorField","parentId");
            check(443,"未分组右键新增默认选顶级分组而非虚拟节点",parent->currentIndex()==0 && !parent->currentData().isValid());
            check(442,"未分组新增只提供顶级父级，不错误列出全部实体分组",parent->count()==1);
            check(444,"原生新增分组采用紧凑窗口且输入控件不越界",dialog->height()<=400 && dialog->rect().contains(QRect(parent->mapTo(dialog,QPoint()),parent->size())));
            QEvent leave(QEvent::Leave);QApplication::sendEvent(dialog,&leave);
            check(446,"新增分组弹窗鼠标离开仍保持打开",dialog->isVisible());
            field<QLineEdit>(dialog,"editorField","分组名称")->setText("原生顶级组");dialog->findChild<QPushButton*>("primaryButton")->click();
        });
        menu->setActiveAction(root);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);
    });
    nativeGroups->customContextMenuRequested(nativeGroups->visualItemRect(ungrouped).center());events();
    const auto peerRows=db.rows("groups","name=?",{"原生顶级组"});
    check(440,"原生右键创建顶级分组与总分组同级保存",peerRows.size()==1 && peerRows.first().toObject().value("parentId").isNull());
    for(int row:{453,458})check(row,"显式顶级分组实际保存空父级，与实体总分组同级",peerRows.size()==1 && peerRows.first().toObject().value("parentId").isNull());
    if(peerRows.isEmpty())return 2;
    const auto peer=peerRows.first().toObject().value("id");qint64 addedDevice=0;
    db.insert("devices",{{"code","peer-scope"},{"name","其他顶级设备"},{"groupId",peer}},&addedDevice,&error);
    db.insert("devices",{{"code","free-scope"},{"name","范围未分组设备"}},&addedDevice,&error);
    nav->setCurrentRow(0);nav->setCurrentRow(5);events();
    auto *deviceTable=field<QTableWidget>(&window,"customerTable","devices");
    auto *scopeLabel=field<QLabel>(&window,"customerScope","devices");
    check(436,"全部设备范围同时包含实体分组与未分组设备，标签准确",scopeLabel->text().contains("全部分组") && deviceTable->rowCount()==27);
    QTreeWidgetItem *defaultGroup=nullptr,*peerGroup=nullptr;
    for(QTreeWidgetItemIterator it(nativeGroups);*it;++it){const auto type=(*it)->data(0,Qt::UserRole+1);if(type=="ungrouped")ungrouped=*it;if(type=="group" && (*it)->data(0,Qt::UserRole).toLongLong()==1)defaultGroup=*it;if(type=="group" && (*it)->data(0,Qt::UserRole).toLongLong()==peer.toInteger())peerGroup=*it;}
    if(!defaultGroup || !peerGroup || !ungrouped)return 2;
    nativeGroups->setCurrentItem(defaultGroup);nativeGroups->itemClicked(defaultGroup,0);events();
    check(436,"实体总分组仅包含本组及子组，不混入其他顶级组或未分组",deviceTable->rowCount()==25 && scopeLabel->text().contains("总分组"));
    nativeGroups->setCurrentItem(peerGroup);nativeGroups->itemClicked(peerGroup,0);events();
    check(436,"其他顶级分组仅显示自身设备",deviceTable->rowCount()==1 && deviceTable->item(0,6)->text()=="peer-scope");
    auto moveOnNativeTable=[&](QPoint at){QMouseEvent event(QEvent::MouseMove,QPointF(at),QPointF(deviceTable->viewport()->mapToGlobal(at)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(deviceTable->viewport(),&event);events(5);};const QPoint nativeBlank(10,deviceTable->viewport()->height()-10);moveOnNativeTable(nativeBlank);const auto nativeBefore=deviceTable->grab().toImage();moveOnNativeTable(deviceTable->visualItemRect(deviceTable->item(0,2)).center());moveOnNativeTable(nativeBlank);check(465,"服务端鼠标移过设备行再移至空白，表格恢复原样且内容未乱绘",!deviceTable->indexAt(nativeBlank).isValid() && nativeBefore==deviceTable->grab().toImage() && deviceTable->item(0,2)->text()=="其他顶级设备");
    nativeGroups->setCurrentItem(ungrouped);nativeGroups->itemClicked(ungrouped,0);events();
    check(437,"选择未分组时仅显示未分组设备",deviceTable->rowCount()==1 && scopeLabel->text().contains("未分组设备"));
    field<QPushButton>(&window,"customerClearScope","devices")->click();events();
    check(437,"清空范围恢复全部27条且标签同步全部设备",deviceTable->rowCount()==27 && scopeLabel->text().contains("全部分组"));
    nav->setCurrentRow(2); events();
    if(qEnvironmentVariableIsSet("BOER_TEST_NATIVE_DEVICE")) {
        QString listenError; if(!devices.listen(0,&listenError)) qFatal("DT listener failed");
        auto frame=[](quint16 kind,quint16 number,const QByteArray &payload=QByteArray()) {
            QByteArray raw("DT"); raw+=QByteArray(11,0);
            auto word=[&](quint16 n) {raw+=char(n>>8);raw+=char(n);}; word(kind);word(number);raw+=char(1);raw+=char(1);
            const auto size=qToBigEndian(quint32(payload.size()+2)); raw.append(reinterpret_cast<const char*>(&size),4);raw+=payload;
            quint16 crc=0xffff; for(uchar byte:raw) {crc^=byte;for(int i=0;i<8;++i)crc=(crc>>1)^((crc&1) ? 0xa001 : 0);}
            word(crc); return raw;
        };
        auto identify=[&](QTcpSocket &socket) {
            socket.connectToHost("127.0.0.1",devices.port());
            if(!waitUntil([&] {return socket.bytesAvailable()>=25;})) qFatal("DT first probe timeout"); socket.readAll();
            socket.write(frame(0x0b2a,2));socket.flush();
            if(!waitUntil([&] {return socket.bytesAvailable()>=25;})) qFatal("DT second probe timeout");socket.readAll();
            QByteArray payload; for(quint32 value:{1u,7654321u}) {const auto bytes=qToBigEndian(value);payload.append(reinterpret_cast<const char*>(&bytes),4);} payload+=QString("中文原生删除设备").toUtf8();
            socket.write(frame(0x1302,0x10fa,payload));socket.flush();
            return waitUntil([&] {return db.count("devices","code=?",{"7654321"})==1;});
        };
        QTcpSocket live; if(!identify(live)) qFatal("DT identity failed");
        const auto liveId=db.rows("devices","code=?",{"7654321"}).first().toObject().value("id").toInteger();
        db.update("devices",liveId,{{"groupId",1}},nullptr);
        nav->setCurrentRow(5);events(); auto *table=field<QTableWidget>(&window,"customerTable","devices");
        field<QComboBox>(&window,"customerPageSize","devices")->setCurrentIndex(3);events();
        QPushButton *remove=nullptr;
        for(int r=0;r<table->rowCount();++r) if(table->item(r,0)->data(Qt::UserRole).toLongLong()==liveId) {
            for(auto *button:table->cellWidget(r,table->columnCount()-1)->findChildren<QPushButton*>()) if(button->text()=="删除") remove=button;
        }
        if(!remove) qFatal("native delete action missing");
        QTimer::singleShot(20,[&] {
            auto *confirm=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(449,"服务端删除设备先显示二次确认",confirm && confirm->windowTitle()=="确认删除");
            if(confirm) confirm->findChild<QPushButton*>("primaryButton")->click();
        });
        remove->click(); events();
        check(472,"原生服务端删除后共享设备查询不再返回旧设备",db.row("devices",liveId).isEmpty());
        check(473,"原生服务端删除主动关闭设备旧连接",waitUntil([&] {return live.state()==QAbstractSocket::UnconnectedState;}));
        live.abort(); QTcpSocket again; const bool registered=identify(again);
        check(473,"原生删除后相同设备不重启服务端即可重新连接",registered && db.rows("devices","code=?",{"7654321"}).first().toObject().value("status")=="idle");
        again.abort(); events(); nav->setCurrentRow(2);events();
    }
    auto external=[&](const QString &name) { return field<QLineEdit>(&window,"externalField",name); };
    auto action=[&](const QString &name) { return field<QPushButton>(&window,"externalAction",name); };
    auto *enabled=field<QCheckBox>(&window,"externalField","enabled");
    auto *type=field<QComboBox>(&window,"externalField","dbType");
    auto *port=field<PortStepper>(&window,"externalField","port");
    auto *interval=field<PortStepper>(&window,"externalField","interval");
    check(375,"外部连接关闭时测试连接按钮禁用",!enabled->isChecked() && !action("test")->isEnabled());
    check(376,"外部连接关闭时立即同步按钮禁用",!action("sync")->isEnabled());
    enabled->setChecked(true); check(384,"开启但未通过可写测试时同步按钮置灰",!action("sync")->isEnabled());
    external("host")->setText("127.0.0.1"); external("username")->setText("sample"); external("database")->setText("db"); port->setValue(18306); interval->setValue(10);
    type->setCurrentIndex(1); events();
    check(378,"切换数据库类型清空旧地址账号密码库名并重置间隔",external("host")->text().isEmpty() && external("username")->text().isEmpty() && external("password")->text().isEmpty() && external("database")->text().isEmpty() && interval->value()==30);
    check(380,"切换MSSQL端口1433，切回MySQL端口3306",port->value()==1433); type->setCurrentIndex(0); check(380,"切回MySQL自动填充3306",port->value()==3306);
    auto *password=external("password"); auto *visibility=password->actions().last(); const auto key=visibility->icon().cacheKey(); visibility->trigger();
    check(381,"数据库密码明文和图标同时变化",password->echoMode()==QLineEdit::Normal && visibility->icon().cacheKey()!=key); visibility->trigger();
    external("host")->setFocus(); external("host")->setText("999.1.1.1"); QMetaObject::invokeMethod(external("host"),"textEdited",Q_ARG(QString,external("host")->text())); events();
    check(382,"错误IPv4在地址输入处即时红字提示",field<QLabel>(&window,"externalField","hostError")->isVisible() && external("host")->property("invalid").toBool());
    check(386,"修改字段显示未保存并阻止同步",field<QLabel>(&window,"externalField","dirty")->isVisible() && !action("sync")->isEnabled());
    if(qEnvironmentVariableIsSet("BOER_TEST_EXTERNAL_MYSQL")) {
        external("host")->setText("127.0.0.1"); port->setValue(18306); external("database")->setText("boer_sync");
        external("username")->setText("boer_qt_read"); password->setText("QtRead_TestOnly_2026"); action("save")->click(); action("test")->click();
        const bool finished=waitUntil([&] {return action("test")->isEnabled();});
        check(385,"真实只读账号连接后原生同步按钮仍禁用",finished && !action("sync")->isEnabled());
        external("username")->setText("boer_qt_sync"); password->setText("QtSync_TestOnly_2026"); action("save")->click(); action("test")->click();
        check(384,"真实可写账号保存并测试成功才允许同步",waitUntil([&] {return action("test")->isEnabled();}) && action("sync")->isEnabled());
        password->setText("incorrect"); check(383,"已通过后改成错误密码立即阻止沿用旧配置同步",!action("sync")->isEnabled());
        check(386,"已通过后修改配置提示未保存",field<QLabel>(&window,"externalField","dirty")->isVisible());
        action("test")->click();const bool rejected=waitUntil([&]{return action("test")->isEnabled();});auto *feedback=window.findChild<QLabel*>("actionToast");check(387,"原生服务端错误密码实际反馈账号密码或权限原因",rejected && feedback && feedback->text().contains("账号或密码") && !action("sync")->isEnabled());UiI18n::instance()->setEnglish(true);events();check(377,"原生数据库连接错误反馈随英文切换且不残留中文",feedback && !feedback->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")));UiI18n::instance()->setEnglish(false);events();

    }
    nav->setCurrentRow(1); events();
    auto *folder=field<QLineEdit>(&window,"toolField","sharedFolder");
    auto *saveSettings=field<QPushButton>(&window,"toolAction","saveSettings");
    auto *folderError=field<QLabel>(&window,"toolField","folderError");
    const QString originalFolder=db.config("shared_folder");
    check(368,"共享目录界面明确预留用途，不声称已开启系统共享或自动导入",window.findChild<QLabel*>("sharedFolderUsageHint") && window.findChild<QLabel*>("sharedFolderUsageHint")->text().contains("预留配置"));

    for(const auto &path:QStringList{"./share","share"}) {
        folder->setText(path); saveSettings->click();
        check(363,"共享目录拒绝相对路径"+path,folderError->isVisible() && folderError->text().contains("绝对路径") && db.config("shared_folder")==originalFolder);
    }
    folder->setText("D:\\sh<are"); saveSettings->click();
    check(364,"共享目录非法字符禁止保存且提示具体原因",folderError->isVisible() && folderError->text().contains("非法字符") && db.config("shared_folder")==originalFolder);
    folder->setText("/"+QString(1100,'a')); saveSettings->click();
    check(366,"超过当前操作系统路径长度限制时禁止保存",folderError->isVisible() && folderError->text().contains("路径过长") && db.config("shared_folder")==originalFolder);
    folder->clear(); saveSettings->click();
    check(367,"共享目录留空可保存，关闭旧错误提示",!folderError->isVisible() && db.config("shared_folder").isEmpty());
    folder->setText(directory.path()); saveSettings->click();
    check(363,"合法绝对目录可以保存",!folderError->isVisible() && db.config("shared_folder")==directory.path());
    nav->setCurrentRow(0);events();QPushButton *clearMainLogs=nullptr;for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()=="清空日志")clearMainLogs=button;
    QTimer::singleShot(20,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("main log confirmation missing");dialog->findChild<QPushButton*>("primaryButton")->click();});if(clearMainLogs)clearMainLogs->click();else qFatal("main clear logs missing");check(357,"真实清空主界面日志后数据库日志为空",db.count("debug_logs")==0);nav->setCurrentRow(1);events();
    auto *diagnosticPort=field<PortStepper>(&window,"toolField","diagnosticPort");const int savedPort=diagnosticPort->value();diagnosticPort->setValue(0);check(360,"端口组件拒绝0并限制最低1",diagnosticPort->value()==1);diagnosticPort->setValue(65536);auto *portText=diagnosticPort->findChild<QLineEdit*>();check(360,"端口最大65535完整显示且步进按钮在控件内",diagnosticPort->value()==65535 && portText->text()=="65535" && portText->width()>=portText->fontMetrics().horizontalAdvance(portText->text())+6 && diagnosticPort->rect().contains(diagnosticPort->findChild<QPushButton*>("stepButtonUp")->geometry()));diagnosticPort->setValue(savedPort);
    for(auto *stepper:QList<PortStepper*>{port,interval}){auto *input=stepper->findChild<QLineEdit*>();check(379,"数据库端口/同步间隔数字和步进按钮完整显示",input && input->width()>=input->fontMetrics().horizontalAdvance(input->text())+4 && stepper->height()>=input->height());}
    QPushButton *showIp=nullptr,*showHost=nullptr;
    for(auto *button:window.findChildren<QPushButton*>()) {
        if(button->text()=="查看本机 IP 信息") showIp=button;
        if(button->text()=="查看主机名") showHost=button;
    }
    UiI18n::instance()->setEnglish(true); events();
    for(const auto &entry:QList<QPair<QPushButton*,QString>>{{showHost,"Host Name"},{showIp,"Local IP Information"},{showHost,"Host Name"}}) {
        if(!entry.first) qFatal("network tool missing");
        QTimer::singleShot(20,[&] {
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(371,"英文工具结果标题独立："+entry.second,dialog && dialog->windowTitle()==entry.second,dialog ? dialog->windowTitle() : "missing");
            for(int row:{358,372})check(row,"连续主机名/IP/主机名调用标题不互相污染："+entry.second,dialog && dialog->windowTitle()==entry.second);
            auto *text=dialog ? dialog->findChild<QTextEdit*>() : nullptr;if(entry.first==showIp)check(357,"清空日志后本机IP弹窗正常显示实际回环地址",text && text->toPlainText().contains("127.0.0.1"));
            bool translated=dialog;if(dialog)for(auto *label:dialog->findChildren<QLabel*>())if(label->isVisible())translated&=!label->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));check(369,"英文诊断结果标题和按钮翻译，系统原始信息保留",translated);check(370,"诊断结果正文在弹窗内换行且不撑出窗口",text && dialog->rect().contains(QRect(text->mapTo(dialog,QPoint()),text->size())) && text->lineWrapMode()==QTextEdit::WidgetWidth);

            if(dialog) dialog->reject();
        });
        entry.first->click();
    }
    UiI18n::instance()->setEnglish(false); events();
    // Exercise the actual modal form through its save button, including validation that must keep it open.
    QTimer::singleShot(0,[&] {
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); if(!dialog) qFatal("missing account dialog");
        auto input=[&](const QString &label) { return field<QLineEdit>(dialog,"editorField",label); };
        auto *save=dialog->findChild<QPushButton*>("primaryButton");
        input("账号")->setText("regression"); save->click();
        check(416,"新建账号空密码保留表单并显示红字",dialog->isVisible() && dialog->findChild<QLabel*>("formValidationError")->isVisible());
        input("密码")->setText("123456"); input("确认密码")->setText("123457"); save->click();
        check(418,"确认密码不一致时禁止提交",dialog->isVisible() && dialog->findChild<QLabel*>("formValidationError")->text().contains("不一致"));
        auto *pwd=input("密码"); const auto icon=pwd->actions().last()->icon().cacheKey(); pwd->actions().last()->trigger();
        check(417,"账号密码明暗文与图标同时变化",pwd->echoMode()==QLineEdit::Normal && pwd->actions().last()->icon().cacheKey()!=icon);
        input("确认密码")->setText("123456"); input("账号姓名")->setText(QString(70,'x'));
        check(419,"账号姓名限制50字符",input("账号姓名")->text().size()==50);
        input("邮箱")->setText("testexample.com"); save->click();
        check(420,"非法邮箱在真实新建表单禁止保存",dialog->isVisible() && dialog->findChild<QLabel*>("formValidationError")->text().contains("邮箱"));
        input("邮箱")->setText(QString(130,'x')); check(421,"邮箱限制100字符",input("邮箱")->text().size()==100);
        input("邮箱")->setText("test@example.com");input("手机号")->clear();save->click();check(424,"原生新建账号手机号为空时保留表单并红字提示",dialog->isVisible() && dialog->findChild<QLabel*>("formValidationError")->text().contains("手机号不能为空"));check(422,"邮箱提示可选，手机号提示必填并给出明确空值原因",input("邮箱")->placeholderText()=="可选" && input("手机号")->placeholderText()=="请输入手机号");input("手机号")->setText("138 0013 8000"); save->click();
    });
    auto account=showEditorDialog(&db,"users",{},true,&window);
    check(423,"原生账号表单接受含空格手机号并规范化",account && account->value("phone").toString()=="13800138000");
    check(425,"账号启用开关开启时保存disabled=false",account && !account->value("disabled").toBool());
    QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto *name=field<QLineEdit>(dialog,"editorField","账号姓名");name->clear();dialog->findChild<QPushButton*>("primaryButton")->click();check(427,"编辑账号同样拒绝空姓名并保留表单",dialog->isVisible() && dialog->findChild<QLabel*>("formValidationError")->isVisible());name->setText("修改姓名");
        auto *password=field<QLineEdit>(dialog,"editorField","新密码(可选)");password->setText("123");dialog->findChild<QPushButton*>("primaryButton")->click();check(428,"编辑账号新密码不足6位时禁止提交",dialog->isVisible() && dialog->findChild<QLabel*>("formValidationError")->text().contains("密码"));password->clear();
        for(auto *button:dialog->findChildren<QAbstractButton*>())if(auto *toggle=dynamic_cast<UiSwitch*>(button))toggle->setChecked(false);
        dialog->findChild<QPushButton*>("primaryButton")->click();});
    const auto disabledAccount=showEditorDialog(&db,"users",account.value_or(QJsonObject{}),false,&window);
    check(425,"账号启用开关关闭时保存disabled=true，与显示语义一致",disabledAccount && disabledAccount->value("disabled").toBool());
    check(428,"编辑账号留空新密码不覆盖原密码",disabledAccount && !disabledAccount->contains("password"));

    QTimer::singleShot(0,[&] {
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto *code=field<QLineEdit>(dialog,"editorField","工号"); auto *name=field<QLineEdit>(dialog,"editorField","姓名"); auto *phone=field<QLineEdit>(dialog,"editorField","手机号");
        code->setText("E100"); dialog->findChild<QPushButton*>("primaryButton")->click();
        check(311,"原生员工表单拒绝非数字工号",dialog->isVisible() && dialog->findChild<QLabel*>("formValidationError")->isVisible());
        code->setText("0001001"); name->setText(QString(10,QChar(0x540d))); check(320,"原生员工姓名限制8字",name->text().size()==8);
        phone->setText("138 0013 8000"); dialog->findChild<QPushButton*>("primaryButton")->click();
    });
    auto employee=showEditorDialog(&db,"employees",{},true,&window);
    check(312,"原生员工表单空格手机号规范化且工号保留零",employee && employee->value("phone")=="13800138000" && employee->value("code")=="0001001");
    qint64 duplicateEmployee=0;
    check(313,"原生服务端保存员工经过共享手机号校验",employee && db.insert("employees",*employee,&duplicateEmployee,&error));
    QTimer::singleShot(0,[&] {
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        field<QLineEdit>(dialog,"editorField","工号")->setText("1002");field<QLineEdit>(dialog,"editorField","姓名")->setText("重复手机");
        auto *phone=field<QLineEdit>(dialog,"editorField","手机号");phone->setText("138 0013 8000");dialog->findChild<QPushButton*>("primaryButton")->click();
        check(313,"原生员工新增重复手机号原地红字拦截",dialog->isVisible() && dialog->findChild<QLabel*>("formValidationError")->text().contains("手机号已被"));
        phone->setText("13900138000");dialog->findChild<QPushButton*>("primaryButton")->click();
    });
    check(313,"原生员工修正重复号码后可以提交",showEditorDialog(&db,"employees",{},true,&window).has_value());
    for(const auto &selection:QList<QPair<QString,int>>{{"deviceInfo",403},{"patternFiles",404},{"salaryStatistics",404}}) {
        QTimer::singleShot(0,[&] {
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            field<QLineEdit>(dialog,"editorField","角色名称")->setText("单权限");
            auto *tree=dialog->findChild<QTreeWidget*>();
            for(auto *button:dialog->findChildren<QPushButton*>()) if(button->text()=="清空") button->click();
            for(auto *box:dialog->findChildren<QCheckBox*>()) if(box->text()=="展开细分权限") box->setChecked(true);
            QTreeWidgetItem *selected=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it) if((*it)->data(0,Qt::UserRole).toString()==selection.first) selected=*it;
            if(!selected) qFatal("permission fixture missing");
            selected->setCheckState(0,Qt::Checked); tree->scrollToItem(selected); events();
            check(402,"展开细分权限可滚动到"+selection.first,tree->visualItemRect(selected).intersects(tree->viewport()->rect()));
            dialog->findChild<QPushButton*>("primaryButton")->click();
        });
        const auto role=showEditorDialog(&db,"roles",{},true,&window);
        const auto map=role ? QJsonDocument::fromJson(role->value("permissions").toString().toUtf8()).object() : QJsonObject{};
        int checked=0; for(auto it=map.begin();it!=map.end();++it) checked+=it.value().toBool();
        check(selection.second,"父子联动只勾选"+selection.first+"保留选中子项及父项，不扩散到同级",checked==2 && map.value(selection.first).toBool());
    }
    for(bool creating:{true,false}) {
        QTimer::singleShot(0,[&] {
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            field<QLineEdit>(dialog,"editorField","角色名称")->setText("修改待放弃");
            QTimer::singleShot(20,[&] {
                auto *confirm=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                check(creating ? 405 : 407,"角色退出有改动时显示放弃确认",confirm && confirm!=dialog && confirm->windowTitle()=="放弃编辑");
                if(confirm && confirm!=dialog) confirm->reject();
            });
            dialog->reject();
            check(creating ? 405 : 407,"取消放弃后保留角色表单与已填写内容",dialog->isVisible() && field<QLineEdit>(dialog,"editorField","角色名称")->text()=="修改待放弃");
            QTimer::singleShot(20,[] {
                auto *confirm=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if(confirm) confirm->findChild<QPushButton*>("primaryButton")->click();
            });
            dialog->reject();
        });
        const auto result=showEditorDialog(&db,"roles",creating ? QJsonObject{} : db.rows("roles").first().toObject(),creating,&window);
        check(creating ? 405 : 407,"确认放弃关闭角色表单且不返回保存数据",!result);
    }
    QTimer::singleShot(0,[&] {
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("device optional editor missing");auto *name=field<QLineEdit>(dialog,"editorField","设备名称");const QString saved=name->text();name->clear();dialog->findChild<QPushButton*>("primaryButton")->click();check(470,"服务端编辑设备同样拒绝空名称并保留表单",dialog->isVisible() && dialog->findChild<QLabel*>("formValidationError")->isVisible());name->setText(QString(101,QChar(0x8bbe)));name->selectAll();name->copy();check(470,"服务端设备名100字限制与复制可用，和客户端一致",name->text().size()==100 && QApplication::clipboard()->text()==name->text());QEvent leave(QEvent::Leave);QApplication::sendEvent(dialog,&leave);check(470,"服务端设备编辑鼠标离开不消失",dialog->isVisible());name->setText(saved);dialog->findChild<QPushButton*>("primaryButton")->click();
    });
    const auto editedDevice=showEditorDialog(&db,"devices",db.rows("devices","code=?",{"d0"}).first().toObject(),false,&window);
    check(469,"原生服务端设备类型未选择时仍能保存，与客户端和接口一致",editedDevice && editedDevice->value("type").toString().isEmpty());

    qint64 nativePattern=0;check(163,"服务端共享写入拒绝非法订单号，防止原生界面绕过HTTP校验",!db.insert("patterns",{{"name","非法订单"},{"fileName","invalid.NTP"},{"orderNo","A-B"}},&nativePattern,&error));
    check(163,"服务端共享写入接受字母数字订单号并保留前导零",db.insert("patterns",{{"name","合法订单"},{"fileName","valid.NTP"},{"orderNo","Ab0001"}},&nativePattern,&error));
    check(169,"服务端直接修改订单号也拒绝首尾空格且不改原值",!db.update("patterns",nativePattern,{{"orderNo"," A1 "}},&error) && db.row("patterns",nativePattern).value("orderNo")=="Ab0001");
    qint64 specialLog=0;const QString specialUser="登录+&?#%_";db.insert("login_logs",{{"username",specialUser},{"ip","127.0.0.1"},{"status","failed"},{"loginTime",QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")}},&specialLog,&error);
    nav->setCurrentRow(6);events();auto *logFilter=field<QLineEdit>(&window,"customerFilter","login_logs");logFilter->setText(specialUser);QMetaObject::invokeMethod(logFilter,"returnPressed");events();auto *logTable=field<QTableWidget>(&window,"customerTable","login_logs");check(475,"服务端登录日志搜索按字面匹配特殊字符，不当作正则",logTable->rowCount()==1 && logTable->item(0,1)->text()==specialUser);
    window.resize(1120,700);for(const auto &entry:QList<QPair<int,int>>{{3,399},{4,412},{5,464},{6,476}}) {
        nav->setCurrentRow(entry.first);events();DateRangeWidget *range=nullptr;for(auto *frame:window.findChildren<QFrame*>())if(auto *widget=dynamic_cast<DateRangeWidget*>(frame))if(widget->isVisible())range=widget;
        if(!range){check(entry.second,"当前筛选区域提供可见日期范围控件",false);continue;}range->setRange(QDate(2026,1,1),QDate(2026,12,31));events();bool fits=window.rect().contains(QRect(range->mapTo(&window,QPoint()),range->size()));for(auto *button:range->findChildren<QPushButton*>("datePart"))fits&=button->width()>=button->fontMetrics().horizontalAdvance(button->text())+4;
        check(entry.second,"1120小窗实际筛选日期完整显示，不越出页面或遮挡日历按钮",fits);
    }
    // Explicit child permissions must appear in both native lists and account previews.
    const QString detailed=QString::fromUtf8(QJsonDocument(QJsonObject{{"fileManagement",true},{"patternFiles",true},{"devicePatternFiles",false},{"downloadLog",false},{"statistics",true},{"salaryStatistics",true},{"statusStatistics",false}}).toJson(QJsonDocument::Compact));
    qint64 detailedRole=0;db.insert("roles",{{"name","detailed"},{"permissions",detailed}},&detailedRole,&error);
    nav->setCurrentRow(0);nav->setCurrentRow(3);events();
    auto *rolesTable=field<QTableWidget>(&window,"customerTable","roles");QStringList displayed;
    for(int row=0;row<rolesTable->rowCount();++row)if(rolesTable->item(row,1)->text()=="detailed")displayed=rolesTable->item(row,4)->data(Qt::UserRole+1).toStringList();
    check(406,"服务端角色列表显示实际子权限，不将未授权的下发日志或状态统计混入",displayed.contains("花型文件") && displayed.contains("工资统计") && !displayed.contains("下发日志") && !displayed.contains("状态统计"));
    QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QStringList tags;for(auto *label:dialog->findChildren<QLabel*>("permissionTag"))tags<<label->text();
        check(432,"账号角色权限预览与实际客户端子菜单一致",tags.contains("花型文件") && tags.contains("工资统计") && !tags.contains("下发日志") && !tags.contains("状态统计"));
        check(435,"编辑账号使用编辑标题并锁定原账号",dialog->windowTitle()=="编辑账号" && field<QLineEdit>(dialog,"editorField","账号")->isReadOnly());
        check(429,"账号操作弹窗明确使用编辑账号文案，账号只读不冒充新建",dialog->windowTitle()=="编辑账号" && field<QLineEdit>(dialog,"editorField","账号")->text()=="existing");
        dialog->reject();});
    showEditorDialog(&db,"users",{{"id",999},{"username","existing"},{"nickname","原账号"},{"role","detailed"},{"permissions",detailed}},false,&window);
    QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto *username=field<QLineEdit>(dialog,"editorField","账号");
        check(426,"编辑后再新建账号不残留旧账号字段且可输入",username->text().isEmpty() && !username->isReadOnly() && dialog->windowTitle()=="新建账号");
        field<QLineEdit>(dialog,"editorField","密码")->setText("123456");field<QLineEdit>(dialog,"editorField","确认密码")->setText("123456");field<QLineEdit>(dialog,"editorField","账号姓名")->setText("随机测试");field<QLineEdit>(dialog,"editorField","手机号")->setText("13800138000");
        dialog->findChild<QPushButton*>("primaryButton")->click();QPushButton *generate=nullptr;for(auto *button:dialog->findChildren<QPushButton*>())if(button->text()=="随机生成")generate=button;
        if(generate)generate->click();check(415,"空账号报错后随机生成账号立即清除原账号红字",generate && !username->text().isEmpty() && !dialog->findChild<QLabel*>("formValidationError")->isVisible());dialog->reject();});
    showEditorDialog(&db,"users",{},true,&window);
    DateRangeWidget dates(&window);dates.move(300,200);dates.show();
    for(bool english:{false,true}) {
        if(UiI18n::instance())UiI18n::instance()->setEnglish(english);events();
        dates.findChildren<QPushButton*>().first()->click();events();auto *popup=dates.findChild<QMenu*>("datePopup");auto *calendar=popup ? popup->findChild<QCalendarWidget*>() : nullptr;
        check(479,english ? "英文日期日历使用英文月份、导航与星期" : "中文日期日历使用中文月份与星期",calendar && calendar->locale().language()==(english ? QLocale::English : QLocale::Chinese) && popup->isVisible());
        if(popup)popup->close();events();
    }
    if(UiI18n::instance())UiI18n::instance()->setEnglish(false);
    QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto *text=dialog->findChild<QTextEdit*>();
        if(text){text->selectAll();text->copy();}check(480,"执行结果文本只读但允许全选复制中文内容",text && text->isReadOnly() && QApplication::clipboard()->text()=="中文诊断结果 127.0.0.1");
        QEvent leave(QEvent::Leave);QApplication::sendEvent(dialog,&leave);check(481,"执行结果弹窗鼠标离开不消失",dialog->isVisible());dialog->reject();});
    UiMessageBox::output(&window,"本机诊断","中文诊断结果 127.0.0.1");

    nav->setCurrentRow(5);events();qint64 crossA=0,crossB=0;
    db.insert("devices",{{"code","cross-a"},{"name","跨组A"},{"groupId",1}},&crossA,&error);db.insert("devices",{{"code","cross-b"},{"name","跨组B"}},&crossB,&error);
    nav->setCurrentRow(0);nav->setCurrentRow(5);events();auto *crossTable=field<QTableWidget>(&window,"customerTable","devices");field<QComboBox>(&window,"customerPageSize","devices")->setCurrentIndex(3);events();
    auto node=[&](const QString &type,qint64 id=0){for(QTreeWidgetItemIterator it(nativeGroups);*it;++it)if((*it)->data(0,Qt::UserRole+1)==type && (id==0 || (*it)->data(0,Qt::UserRole).toLongLong()==id))return *it;return static_cast<QTreeWidgetItem*>(nullptr);};
    auto selectDevice=[&](qint64 id){for(int r=0;r<crossTable->rowCount();++r)if(crossTable->item(r,0)->data(Qt::UserRole).toLongLong()==id){crossTable->cellWidget(r,0)->findChild<QCheckBox*>()->setChecked(true);return true;}return false;};
    nativeGroups->setCurrentItem(node("group",1));nativeGroups->itemClicked(node("group",1),0);events();const bool choseA=selectDevice(crossA);
    nativeGroups->setCurrentItem(node("ungrouped"));nativeGroups->itemClicked(node("ungrouped"),0);events();const bool choseB=selectDevice(crossB);
    nativeGroups->scrollToItem(node("group",peer.toInteger()));events();QTimer::singleShot(20,[&]{auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());QAction *move=nullptr;bool root=false;if(menu)for(auto *action:menu->actions()){if(action->text()=="移动已选设备到当前组")move=action;root|=action->text()=="新增分组";}
        check(460,"服务端实体分组右键不再提供新增顶级父组",menu && !root);check(462,"跨分组选择后提供移动到当前组菜单",choseA && choseB && move);if(!move){if(menu)menu->close();return;}menu->setActiveAction(move);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);});
    nativeGroups->customContextMenuRequested(nativeGroups->visualItemRect(node("group",peer.toInteger())).center());events();
    check(462,"实际跨两个分组勾选并一次移动两台设备",db.row("devices",crossA).value("groupId")==peer && db.row("devices",crossB).value("groupId")==peer);
    check(462,"移动后清理已选项，并提供手动取消选择按钮",window.findChild<QPushButton*>("clearDeviceSelection")!=nullptr);


    bool hasAll=false,hasUngrouped=false;for(QTreeWidgetItemIterator it(nativeGroups);*it;++it){hasAll|=(*it)->data(0,Qt::UserRole+1)=="all" && (*it)->text(0)=="全部分组";hasUngrouped|=(*it)->data(0,Qt::UserRole+1)=="ungrouped" && (*it)->text(0)=="未分组设备";}check(438,"服务端与客户端统一虚拟全部分组和未分组设备命名",hasAll && hasUngrouped);
    for(const auto &entry:QList<QPair<qint64,int>>{{1,454},{peer.toInteger(),459}}) {
        auto *parentNode=node("group",entry.first);nativeGroups->scrollToItem(parentNode);events();const QString childName=QString("上下文子组%1").arg(entry.first);
        QTimer::singleShot(20,[&]{auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());QAction *add=nullptr;if(menu)for(auto *action:menu->actions())if(action->text()=="新增子分组")add=action;if(!add)qFatal("native child menu missing");
            QTimer::singleShot(20,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto *input=dialog ? dialog->findChild<QLineEdit*>("promptInput") : nullptr;if(!input)qFatal("native child prompt missing");
                QEvent leave(QEvent::Leave);QApplication::sendEvent(dialog,&leave);check(457,"原生新增子分组弹窗鼠标离开仍保留",dialog->isVisible());check(455,"原生新增子组采用紧凑输入弹窗且字段完整在窗口内",dialog->height()<=300 && dialog->rect().contains(QRect(input->mapTo(dialog,QPoint()),input->size())));input->setText(childName);dialog->findChild<QPushButton*>("primaryButton")->click();});menu->setActiveAction(add);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);});
        nativeGroups->customContextMenuRequested(nativeGroups->visualItemRect(parentNode).center());events();auto child=db.rows("groups","name=?",{childName});check(entry.second,"右键新增子分组固定为当前实体父级，不能跳到其他上级",child.size()==1 && child.first().toObject().value("parentId").toInteger()==entry.first);
    }
    qint64 removableGroup=0,removableDevice=0;db.insert("groups",{{"name","删除分组专项"},{"parentId",QJsonValue(QJsonValue::Null)}},&removableGroup,&error);db.insert("devices",{{"code","group-delete-kept"},{"name","分组删除保留设备"},{"groupId",removableGroup}},&removableDevice,&error);nav->setCurrentRow(0);nav->setCurrentRow(5);events();window.resize(1120,700);events();
    auto *removeGroupNode=node("group",removableGroup);nativeGroups->scrollToItem(removeGroupNode);events();
    QTimer::singleShot(20,[&]{auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());QAction *remove=nullptr;if(menu)for(auto *action:menu->actions())if(action->text()=="删除分组")remove=action;check(445,"小窗口分组右键提供删除入口且菜单保持在窗口范围",menu && remove && window.rect().contains(QRect(menu->mapTo(&window,QPoint()),menu->size())));if(!remove)qFatal("native remove group missing");
        QTimer::singleShot(20,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());bool warns=false;if(dialog)for(auto *label:dialog->findChildren<QLabel*>())warns|=label->text().contains("组内设备将移至未分组设备");check(463,"删除分组确认明确提示组内设备移至未分组",warns);if(dialog)dialog->findChild<QPushButton*>("primaryButton")->click();});menu->setActiveAction(remove);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);});
    nativeGroups->customContextMenuRequested(nativeGroups->visualItemRect(removeGroupNode).center());events();check(456,"实际删除非空分组后设备保留且移至未分组",db.row("groups",removableGroup).isEmpty() && !db.row("devices",removableDevice).isEmpty() && db.row("devices",removableDevice).value("groupId").isNull());
    qint64 contextDelete=0;db.insert("devices",{{"code","native-context-delete"},{"name","原生未分组删除"}},&contextDelete,&error);nav->setCurrentRow(0);nav->setCurrentRow(5);events();
    auto *victim=node("device",contextDelete);if(!victim)qFatal("native context delete missing");nativeGroups->scrollToItem(victim);events();
    QTimer::singleShot(20,[&]{auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());QAction *rename=nullptr;if(menu)for(auto *action:menu->actions())if(action->text()=="重命名")rename=action;if(!rename)qFatal("native rename missing");QTimer::singleShot(20,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto *input=dialog ? dialog->findChild<QLineEdit*>("promptInput") : nullptr;if(!input)qFatal("native rename prompt missing");QEvent leave(QEvent::Leave);QApplication::sendEvent(dialog,&leave);check(452,"原生设备重命名鼠标离开仍显示并保留原名",dialog->isVisible() && input->text()=="原生未分组删除");input->setText("原生右键改名");dialog->findChild<QPushButton*>("primaryButton")->click();});menu->setActiveAction(rename);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);});nativeGroups->customContextMenuRequested(nativeGroups->visualItemRect(victim).center());events();check(452,"设备树右键重命名实际写入设备名称",db.row("devices",contextDelete).value("name")=="原生右键改名");victim=node("device",contextDelete);nativeGroups->scrollToItem(victim);events();

    QTimer::singleShot(20,[&]{auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());QAction *remove=nullptr;bool move=false;if(menu)for(auto *action:menu->actions()){if(action->text()=="删除设备")remove=action;move|=action->text()=="移出分组";}
        check(448,"服务端未分组设备右键提供删除并隐藏无意义移出",remove && !move);if(!remove)qFatal("native delete menu missing");
        QTimer::singleShot(20,[&]{auto *confirm=qobject_cast<QDialog*>(QApplication::activeModalWidget());check(449,"服务端设备树删除也必须二次确认",confirm!=nullptr);if(confirm)confirm->findChild<QPushButton*>("primaryButton")->click();});menu->setActiveAction(remove);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);});
    nativeGroups->customContextMenuRequested(nativeGroups->visualItemRect(victim).center());events();check(448,"服务端右键确认后设备记录实际删除",db.row("devices",contextDelete).isEmpty());
    if(qEnvironmentVariableIsSet("BOER_TEST_NATIVE_DEVICE")) {
        nav->setCurrentRow(1);events();QPushButton *currentPorts=nullptr;for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()=="查看当前服务端口进程")currentPorts=button;
        QTimer poll;bool completed=false;poll.setInterval(10);QObject::connect(&poll,&QTimer::timeout,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto *text=dialog ? dialog->findChild<QTextEdit*>() : nullptr;if(!text)return;check(362,"当前服务端口进程实际查询设备TCP监听端口",devices.port()>0 && text->toPlainText().contains(QString::number(devices.port())));completed=true;poll.stop();dialog->reject();});poll.start();if(currentPorts)currentPorts->click();
        check(362,"端口查询异步完成且结果弹窗正常打开",waitUntil([&]{return completed;}));poll.stop();
    }

    qint64 groupedAccount=0;db.insert("users",{{"username","groupedtest"},{"nickname","已分组账号"},{"password","test-only"},{"role","user"},{"groupId",1}},&groupedAccount,&error);nav->setCurrentRow(0);nav->setCurrentRow(4);events();auto *accountTable=field<QTableWidget>(&window,"customerTable","users");field<QComboBox>(&window,"customerPageSize","users")->setCurrentIndex(3);events();QPushButton *allAccounts=nullptr,*ungroupedAccounts=nullptr;for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible()){if(button->text()=="全部账号")allAccounts=button;if(button->text()=="未分组账号")ungroupedAccounts=button;}
    if(!allAccounts || !ungroupedAccounts)qFatal("account scope buttons missing");allAccounts->click();events();check(413,"全部账号按钮显示分组与未分组全量账号",accountTable->rowCount()==db.count("users"));ungroupedAccounts->click();events();check(413,"未分组账号按钮只显示无组织分组账号且标签同步",accountTable->rowCount()==db.count("users","COALESCE(group_id,0)=0") && field<QLabel>(&window,"customerScope","users")->text().contains("未分组账号"));
    QTreeWidget *accountGroups=nullptr;for(auto *tree:window.findChildren<QTreeWidget*>("groupTree"))if(tree->isVisible())accountGroups=tree;QTreeWidgetItem *allGroupNode=nullptr;if(accountGroups)for(QTreeWidgetItemIterator it(accountGroups);*it;++it)if((*it)->data(0,Qt::UserRole+1)=="all")allGroupNode=*it;if(!allGroupNode)qFatal("account all groups missing");accountGroups->scrollToItem(allGroupNode);events();
    QTimer::singleShot(20,[&]{auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());QAction *add=nullptr;if(menu)for(auto *action:menu->actions())if(action->text()=="新增分组")add=action;if(!add)qFatal("account root context missing");QTimer::singleShot(20,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("account group editor missing");field<QLineEdit>(dialog,"editorField","分组名称")->setText("账号顶级组");dialog->findChild<QPushButton*>("primaryButton")->click();});menu->setActiveAction(add);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);});accountGroups->customContextMenuRequested(accountGroups->visualItemRect(allGroupNode).center());events();auto accountRoot=db.rows("groups","name=?",{"账号顶级组"});check(411,"账号分组树提供虚拟全部入口，可真正新建顶级分组",accountRoot.size()==1 && accountRoot.first().toObject().value("parentId").isNull());
    UiI18n::instance()->setEnglish(true);window.resize(1120,700);events();
    for(const auto &entry:QList<QPair<int,QList<int>>>{{1,{373}},{2,{374,396,397,398}},{3,{409,410}},{4,{433,434}}}) {
        nav->setCurrentRow(entry.first);events();QStringList remaining;const QRegularExpression chinese("[\\x{4e00}-\\x{9fff}]");
        for(auto *label:window.findChildren<QLabel*>())if(label->isVisible() && !label->property("i18nSkip").toBool() && !label->property("i18nDataName").isValid() && label->text().contains(chinese))remaining<<label->text();
        for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && !button->property("i18nSkip").toBool() && button->text().contains(chinese))remaining<<button->text();
        for(int row:entry.second)check(row,"1120英文实际页面静态标题、标签与按钮无残留中文，业务名称保留",remaining.isEmpty(),remaining.join(" | "));
        const QString artifact=QFileInfo(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG")).absoluteDir().filePath(QString("native-current-en-small-%1.png").arg(entry.first));window.grab().save(artifact);
    }
    UiI18n::instance()->setEnglish(false);events();

    if(qEnvironmentVariableIsSet("BOER_TEST_NATIVE_FILE_IO")) {
        QFile portFile(qEnvironmentVariable("BOER_TEST_NATIVE_FILE_PORT"));if(!portFile.open(QIODevice::WriteOnly))return 5;portFile.write(QByteArray::number(devices.port()));portFile.close();if(!waitUntil([&]{return db.count("devices","code IN ('777001','777002')")==2;}))qFatal("native file peers missing");
        const auto deleteId=db.rows("devices","code=?",{"777001"}).first().toObject().value("id").toInteger(),manyId=db.rows("devices","code=?",{"777002"}).first().toObject().value("id").toInteger();QFile patternFile(directory.filePath("native-file-fixture.NTP"));if(!patternFile.open(QIODevice::WriteOnly))return 5;patternFile.write("NATIVE-TEST");patternFile.close();qint64 existing=0;db.insert("patterns",{{"name","设备花型A"},{"fileName","native-file-fixture.NTP"},{"filePath",patternFile.fileName()},{"fileSize",11},{"unitPrice",2.5}},&existing,&error);db.insert("patterns",{{"name","设备花型A(1)"},{"fileName","existing.NTP"}},nullptr,&error);
        UiI18n::instance()->setEnglish(true);nav->setCurrentRow(7);events();auto *select=window.findChild<QComboBox*>("patternDeviceSelect");auto *files=window.findChild<QTableWidget*>("patternDeviceTable");auto *live=window.findChild<QPushButton*>("nativeLiveReadDeviceFiles");auto *bulk=window.findChild<QPushButton*>("nativeDeleteDeviceFiles");auto *upload=window.findChild<QPushButton*>("nativeUploadDeviceFiles");if(!select || !files || !live || !bulk || !upload)qFatal("native file controls missing");select->setCurrentIndex(select->findData(deleteId));events();live->click();events();check(224,"服务端真实读取6个设备文件并提供批量删除",files->rowCount()==6 && bulk->isEnabled());
        auto chooseFiles=[&](const QStringList &names){for(int row=0;row<files->rowCount();++row)if(auto *box=files->cellWidget(row,0)->findChild<QCheckBox*>())box->setChecked(names.contains(files->item(row,3)->text()));};auto hasFile=[&](const QString &name){for(int row=0;row<files->rowCount();++row)if(files->item(row,3)->text()==name)return true;return false;};
        bool cancelCurrent=false,sawLoading=false,sawError=false;QString lastFileModal;QTimer fileDialogs;fileDialogs.setInterval(10);QObject::connect(&fileDialogs,&QTimer::timeout,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;if(auto *progress=qobject_cast<QProgressDialog*>(dialog)){if(!sawLoading){sawLoading=true;auto *label=progress->findChild<QLabel*>();check(230,"服务端删除进度框显示英文进度，业务文件名保留",label && label->text().startsWith("Deleting device files"));}if(cancelCurrent){cancelCurrent=false;for(auto *button:progress->findChildren<QPushButton*>())if(button->text()=="Stop Remaining Deletions"){button->click();break;}}return;}QString message;for(auto *label:dialog->findChildren<QLabel*>())message+=label->text();if(message!=lastFileModal){qInfo("Native file modal: %s",qPrintable(message));lastFileModal=message;}if(message.contains("Deleted ")){sawError=true;dialog->findChild<QPushButton*>("primaryButton")->click();}else if(message.contains("Delete the "))dialog->findChild<QPushButton*>("primaryButton")->click();});
        chooseFiles({"设备花型A.NTP","设备花型B.NTP"});QString first;for(int row=0;row<files->rowCount();++row)if(files->cellWidget(row,0)->findChild<QCheckBox*>()->isChecked()){first=files->item(row,3)->text();break;}const QString kept=first=="设备花型A.NTP" ? "设备花型B.NTP" : "设备花型A.NTP";cancelCurrent=true;fileDialogs.start();bulk->click();fileDialogs.stop();check(224,"服务端批量删除停止后只完成当前项，后续文件保留",sawLoading && files->rowCount()==5 && !hasFile(first) && hasFile(kept));chooseFiles({kept,"设备花型D.NTP"});fileDialogs.start();bulk->click();fileDialogs.stop();check(224,"服务端真实一次删除两个选中文件，保留其余3项",files->rowCount()==3 && !hasFile(kept) && !hasFile("设备花型D.NTP"));chooseFiles({"设备花型C.NTP"});fileDialogs.start();bulk->click();fileDialogs.stop();check(224,"服务端设备拒绝删除时不删除缓存且出现失败结果",sawError && files->rowCount()==3 && hasFile("设备花型C.NTP"));
        int same=-1;for(int row=0;row<files->rowCount();++row)if(files->item(row,3)->text()=="同名.NTP")same=row;if(same<0)qFatal("native duplicate files missing");auto *remove=files->cellWidget(same,10)->findChild<QPushButton*>();fileDialogs.start();remove->click();fileDialogs.stop();check(203,"服务端同名不同编号删除也拒绝不明确的物理目标",files->rowCount()==3 && db.count("device_pattern_files","device_id=? AND file_name=?",{deleteId,"同名.NTP"})==2);
        select->setCurrentIndex(select->findData(manyId));events();live->click();events();auto *size=window.findChild<QComboBox*>("patternDevicePageSize");size->setCurrentIndex(size->findData(100));events();if(files->rowCount()!=40)qFatal("native forty file fixture missing");for(int row=0;row<40;++row)files->cellWidget(row,0)->findChild<QCheckBox*>()->setChecked(true);
        QTimer::singleShot(20,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("native conflict dialog missing");auto *pages=dialog->findChild<QStackedWidget*>("nativeConflictPages");QRadioButton *rename=nullptr;for(auto *radio:dialog->findChildren<QRadioButton*>())if(radio->text()==UiI18n::instance()->text("重命名后新增"))rename=radio;QList<QLineEdit*> names;for(auto *line:dialog->findChildren<QLineEdit*>())if(line->property("nativeConflictName").isValid())names<<line;check(200,"服务端同名冲突默认重命名保留原花型",rename && rename->isChecked());check(202,"服务端40个同名文件实际分成4页，每页10项",pages && pages->count()==4 && names.size()==40);check(202,"服务端多冲突弹窗高度不超过700，分页按钮可访问",dialog->height()<=700);dialog->grab().save(QFileInfo(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG")).absoluteDir().filePath("native-many-conflicts-en.png"));if(!pages || names.size()!=40)qFatal("native conflict pages missing");
            QLineEdit *first=nullptr,*second=nullptr;for(auto *line:names)if(line->isVisible()){first=line;break;}if(!first)qFatal("native conflict first field missing");first->setText("原生跨页A");dialog->findChild<QPushButton*>("nativeConflictNext")->click();events();for(auto *line:names)if(line->isVisible()){second=line;break;}if(!second)qFatal("native conflict second field missing");second->setText("原生跨页B");pages->setCurrentIndex(3);first->clear();dialog->findChild<QPushButton*>("primaryButton")->click();events();check(202,"服务端隐藏页空名称校验返回原页且不丢跨页输入",dialog->isVisible() && pages->currentIndex()==0 && second->text()=="原生跨页B" && dialog->findChild<QLabel*>("promptError")->isVisible());first->setText("原生跨页A");QString reason;check(196,"服务端同名弹窗打开后真实刷新缓存ID",devices.refreshPatternList(manyId,&reason));dialog->findChild<QPushButton*>("primaryButton")->click();});upload->click();
        const auto uploads=db.rows("upload_tasks","device_id=?",{manyId},"id",100);QSet<QString> targets;bool snapshots=true;for(const auto &value:uploads){const auto task=value.toObject();targets.insert(task.value("targetName").toString());snapshots&=!task.value("fileSnapshot").toString().isEmpty();}check(202,"服务端跨页输入与旧ID快照完整提交40项且新名称唯一",uploads.size()==40 && targets.size()==40 && targets.contains("原生跨页A") && targets.contains("原生跨页B") && snapshots);
        QPushButton *queueButton=nullptr;for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()==UiI18n::instance()->text("上传队列"))queueButton=button;if(!queueButton)qFatal("native upload queue button missing");QTimer::singleShot(20,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("native upload queue missing");auto *refresh=dialog->findChild<QPushButton*>("nativeTaskRefresh");auto *table=dialog->findChild<QTableWidget*>("nativeTaskTable");auto *overall=dialog->findChild<QProgressBar*>("nativeUploadOverallProgress");auto *counts=dialog->findChild<QLabel*>("nativeUploadOverallCounts");if(!refresh || !table || !overall || !counts)qFatal("native summary controls missing");refresh->click();const auto expected=uploadTaskSummary(db.rows("upload_tasks",{}, {},"id",10000));check(212,"服务端上传队列对全部40项任务计算总进度并显示完成失败数量",overall->value()==expected.value("overallProgress").toInt() && counts->text().contains("Total 40"));
            const auto task=uploads.first().toObject();db.update("upload_tasks",task.value("id").toInteger(),{{"status","uploading"},{"progress",50}},nullptr);refresh->click();for(int row=0;row<table->rowCount();++row)if(table->item(row,0)->data(Qt::UserRole)==task.value("id").toVariant())table->setCurrentCell(row,0);check(215,"服务端上传进行中的暂停继续取消按钮均禁用",!dialog->findChild<QPushButton*>("nativeTaskPause")->isEnabled() && !dialog->findChild<QPushButton*>("nativeTaskResume")->isEnabled() && !dialog->findChild<QPushButton*>("nativeTaskCancel")->isEnabled());dialog->reject();});queueButton->click();
        QFile done(qEnvironmentVariable("BOER_TEST_NATIVE_FILE_DONE"));if(!done.open(QIODevice::WriteOnly))return 5;done.write("done");done.close();
    }
    window.close(); return failures ? 1 : 0;
}
