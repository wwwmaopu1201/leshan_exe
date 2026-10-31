#include "online_update.h"
#include "ui_settings.h"
#include "client_window.h"
#include "login_regression.h"
#include "client_file_dialog.h"
#include "excel_export.h"
#include "form_dialog.h"
#include "page_widgets.h"
#include "ui_controls.h"
#include "ui_dialogs.h"
#include "ui_i18n.h"
#include "window_compatibility.h"
int runClientInteractionRegression(ClientWindow &window);
int runNetworkResilienceRegression();
int runDirectPatternRegression();
int runConcurrentClientRegression(ClientWindow &window,const QString &host,int port);
int runClientLargeDeviceCheck(ClientWindow &window,const QString &host,int port);

#include <QApplication>
#include <QClipboard>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFont>
#include <QIcon>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QMouseEvent>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QUuid>
#include <QJsonObject>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QStringListModel>
#include <QStyleFactory>
#include <QTimer>
#include <functional>
#include <memory>

namespace {
void selectTestFile(QFileDialog *chooser,const QString &path) {
    chooser->selectFile(path);
    // A visible Cocoa dialog may preserve its focused filename edit instead of
    // replacing it in selectFile(). Fill that edit as a user typing a path would.
    if(auto *filename=chooser->findChild<QLineEdit*>("fileNameEdit")) filename->setText(path);
}
void runClientUiPatternDownloadSmoke(ClientWindow *window,std::function<void(const QString&)> finished) {
    auto directory=std::make_shared<QTemporaryDir>();
    if(!directory->isValid()) { finished("pattern download test directory failed"); return; }
    const QString source=directory->filePath("ui-download.dst");
    const QString target=directory->filePath("downloaded.dst");
    QFile input(source);
    if(!input.open(QIODevice::WriteOnly) || input.write("QT-CLIENT-UI-DOWNLOAD")!=21) {
        finished("pattern download fixture failed"); return;
    }
    input.close();
    window->api()->uploadFile("/pattern/upload",source,QJsonObject{{"name","UI下载联调"}},
        [window,directory,target,finished](const ApiResponse &uploaded) {
            if(!uploaded.ok) { finished("pattern download setup failed: "+uploaded.error); return; }
            const qint64 id=uploaded.data.toObject().value("id").toInteger();
            window->navigate("/file/pattern");
            QTimer::singleShot(300,window,[window,directory,target,id,finished] {
                QPushButton *download=nullptr;
                for(auto *button:window->findChildren<QPushButton*>())
                    if(button->isVisible() && button->text()=="下载") { download=button; break; }
                if(!download) { finished("pattern row download button missing"); return; }
                QTimer::singleShot(50,window,[target] {
                    auto *chooser=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                    if(!chooser) qFatal("pattern download save dialog did not open");
                    selectTestFile(chooser,target);
                    QMetaObject::invokeMethod(chooser,"accept",Qt::DirectConnection);
                });
                download->click();
                auto *poll=new QTimer(window); poll->setInterval(100);
                QObject::connect(poll,&QTimer::timeout,window,
                    [window,directory,target,id,poll,finished,attempts=0]() mutable {
                        QFile output(target);
                        if(output.open(QIODevice::ReadOnly) && output.readAll()=="QT-CLIENT-UI-DOWNLOAD") {
                            poll->stop(); poll->deleteLater();
                            window->api()->remove("/pattern/"+QString::number(id),{},
                                [finished](const ApiResponse &removed) {
                                    finished(removed.ok ? QString() : "pattern download cleanup failed");
                                });
                            return;
                        }
                        if(++attempts>=60) {
                            poll->stop(); poll->deleteLater(); finished("pattern row download did not save the file");
                        }
                    });
                poll->start();
            });
        });
}
void runClientUiEmployeeSmoke(ClientWindow *window,std::function<void(const QString&)> finished) {
    auto directory=std::make_shared<QTemporaryDir>();
    if(!directory->isValid()) { finished("employee file test directory failed"); return; }
    const QString code="9"+QString::number(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8).toUInt(nullptr,16)).rightJustified(10,'0');
    const QString source=directory->filePath("employees.csv");
    const QString target=directory->filePath("employees-export.csv");
    QFile input(source);
    if(!input.open(QIODevice::WriteOnly) ||
       input.write((QString("员工工号,员工姓名,手机号,备注\n%1,测试员,13800000000,联调\n").arg(code)).toUtf8())<0) {
        finished("employee import fixture failed"); return;
    }
    input.close();
    window->navigate("/employee/list");
    QTimer::singleShot(300,window,[window,directory,source,target,code,finished] {
        QPushButton *exportButton=nullptr,*importButton=nullptr;
        for(auto *button:window->findChildren<QPushButton*>()) if(button->isVisible()) {
            if(button->text()=="导出") exportButton=button;
            if(button->text()=="批量导入") importButton=button;
        }
        if(!exportButton || !importButton) { finished("employee import/export buttons missing"); return; }
        QTimer::singleShot(50,window,[target] {
            auto *chooser=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if(!chooser) qFatal("employee export save dialog did not open");
            selectTestFile(chooser,target);
            QMetaObject::invokeMethod(chooser,"accept",Qt::DirectConnection);
        });
        exportButton->click();
        auto *poll=new QTimer(window); poll->setInterval(100);
        QObject::connect(poll,&QTimer::timeout,window,
            [window,directory,source,target,code,importButton,finished,poll,attempts=0]() mutable {
                QFile output(target);
                if(output.open(QIODevice::ReadOnly) && output.readAll().contains("员工工号")) {
                    poll->stop(); poll->deleteLater();
                    auto connection=std::make_shared<QMetaObject::Connection>();
                    *connection=QObject::connect(window->api(),&ApiClient::requestObserved,window,
                        [window,code,connection,finished](const QString &path,int,bool ok) {
                            if(path!="/api/employee/import") return;
                            QObject::disconnect(*connection);
                            if(!ok) { finished("employee import API rejected the form"); return; }
                            window->api()->get("/employee/list?page=1&pageSize=200",
                                [code,finished](const ApiResponse &result) {
                                    if(!result.ok) { finished("employee import verification failed"); return; }
                                    for(const QJsonValue &value:apiRows(result.data))
                                        if(value.toObject().value("code").toString()==code) { finished({}); return; }
                                    finished("employee imported record is missing");
                                });
                        });
                    QTimer::singleShot(50,window,[window,source] {
                        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                        auto *file=dialog ? dialog->findChild<QLineEdit*>("filePathInput") : nullptr;
                        auto *group=dialog ? dialog->findChild<QComboBox*>() : nullptr;
                        if(!dialog || !file || !group || group->count()<2)
                            qFatal("employee import form or group choices missing");
                        group->setCurrentIndex(1);
                        file->setText(source);
                        for(auto *button:dialog->findChildren<QPushButton*>())
                            if(button->text()=="确定") { button->click(); return; }
                        qFatal("employee import confirm button missing");
                    });
                    importButton->click();
                    return;
                }
                if(++attempts>=60) {
                    poll->stop(); poll->deleteLater(); finished("employee export did not save CSV");
                }
            });
        poll->start();
    });
}
void runClientUiExportSmoke(ClientWindow *window,std::function<void(const QString&)> finished) {
    auto directory=std::make_shared<QTemporaryDir>();
    if(!directory->isValid()) { finished("UI export temporary directory failed"); return; }
    const QString target=directory->filePath("process.xlsx");
    window->navigate("/statistics/process");
    QTimer::singleShot(250,window,[window,directory,target,finished] {
        QPushButton *exportButton=nullptr;
        for(auto *button:window->findChildren<QPushButton*>())
            if(button->isVisible() && button->text()=="导出 Excel") { exportButton=button; break; }
        if(!exportButton) { finished("UI statistics export button missing"); return; }
        QTimer::singleShot(50,window,[target] {
            auto *chooser=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if(!chooser) qFatal("UI statistics export did not open a save dialog");
            selectTestFile(chooser,target);
            QMetaObject::invokeMethod(chooser,"accept",Qt::DirectConnection);
        });
        exportButton->click();
        auto *poll=new QTimer(window);
        poll->setInterval(100);
        QObject::connect(poll,&QTimer::timeout,window,[poll,directory,target,finished,attempts=0]() mutable {
            QFile file(target);
            if(file.open(QIODevice::ReadOnly) && file.read(4)=="PK\x03\x04") {
                poll->stop(); poll->deleteLater(); finished({}); return;
            }
            if(++attempts>=60) {
                poll->stop(); poll->deleteLater(); finished("UI statistics export did not save XLSX");
            }
        });
        poll->start();
    });
}
void runClientExportSmoke(ApiClient *api,std::function<void(const QString&)> finished) {
    auto directory=std::make_shared<QTemporaryDir>();
    if(!directory->isValid()) { finished("Excel temporary directory failed"); return; }
    const QString csv=directory->filePath("process.csv");
    const QString xlsx=directory->filePath("process.xlsx");
    api->downloadToFile("/statistics/export/process",csv,
        [directory,csv,xlsx,finished](const ApiResponse &result) {
            if(!result.ok) { finished("statistics export: "+result.error); return; }
            QString error;
            if(!csvToXlsx(csv,xlsx,&error)) { finished("statistics Excel conversion: "+error); return; }
            QFile output(xlsx);
            if(!output.open(QIODevice::ReadOnly) || output.read(4)!="PK\x03\x04") {
                finished("statistics Excel file is invalid"); return;
            }
            finished({});
        });
}
void runClientWriteSmoke(ApiClient *api,std::function<void(const QString&)> finished) {
    const QString suffix=QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    const QString oldType="QtSmoke-"+suffix, newType="QtSmokeRenamed-"+suffix;
    api->post("/pattern/type-summary",QJsonObject{{"value",oldType}},
        [api,finished,oldType,newType,suffix](const ApiResponse &created) {
        if(!created.ok) { finished("catalog create: "+created.error); return; }
        api->put("/pattern/type-summary",QJsonObject{{"oldValue",oldType},{"newValue",newType}},
            [api,finished,newType,suffix](const ApiResponse &renamed) {
            if(!renamed.ok) { finished("catalog rename: "+renamed.error); return; }
            api->remove("/pattern/type-summary",QJsonObject{{"value",newType}},
                [api,finished,suffix](const ApiResponse &removed) {
                if(!removed.ok) { finished("catalog delete: "+removed.error); return; }
                auto directory=std::make_shared<QTemporaryDir>();
                if(!directory->isValid()) { finished("temporary upload directory failed"); return; }
                const QString filePath=directory->filePath("smoke.dst");
                QFile source(filePath);
                if(!source.open(QIODevice::WriteOnly) || source.write("QT-CLIENT-SMOKE")!=15) {
                    finished("temporary upload file failed"); return;
                }
                source.close();
                api->uploadFile("/pattern/upload",filePath,QJsonObject{{"name","Qt客户端联调"+suffix}},
                    [api,finished,directory](const ApiResponse &uploaded) {
                    if(!uploaded.ok) { finished("pattern upload: "+uploaded.error); return; }
                    const qint64 id=uploaded.data.toObject().value("id").toInteger();
                    const QString output=directory->filePath("download.dst");
                    api->downloadToFile("/pattern/"+QString::number(id)+"/file",output,
                        [api,finished,directory,id,output](const ApiResponse &downloaded) {
                        if(!downloaded.ok) { finished("pattern download: "+downloaded.error); return; }
                        QFile restored(output);
                        if(!restored.open(QIODevice::ReadOnly) || restored.readAll()!="QT-CLIENT-SMOKE") {
                            finished("pattern download content mismatch"); return;
                        }
                        api->remove("/pattern/"+QString::number(id),{},
                            [finished](const ApiResponse &deleted) {
                            finished(deleted.ok ? QString() : "pattern delete: "+deleted.error);
                        });
                    });
                });
            });
        });
    });
}
}

int main(int argc,char **argv) {
    QApplication app(argc,argv);
    std::unique_ptr<QTemporaryDir> isolatedCliSettings;
    bool testInvocation=false;for(const auto &argument:QCoreApplication::arguments())testInvocation|=argument.startsWith("--") && argument.endsWith("test");
    if(testInvocation){isolatedCliSettings=std::make_unique<QTemporaryDir>();QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,isolatedCliSettings->path());}

    // Keep the storage identity stable for existing installations.
    QCoreApplication::setApplicationName("Boer Qt Client");
    QGuiApplication::setApplicationDisplayName("Bohr Qt Client");
    QCoreApplication::setApplicationVersion(BOER_APP_VERSION);
    app.setStyle(new UiCheckboxStyle(QStyleFactory::create("Fusion")));
    app.setFont(QFont("PingFang SC",12));
    app.setWindowIcon(QIcon(":/client-app-icon.png"));
    app.installEventFilter(new ButtonFeedbackFilter(&app));
    QFile shared(":/theme.qss"), client(":/client-theme.qss");
    QString stylesheet;
    if(shared.open(QIODevice::ReadOnly)) stylesheet+=QString::fromUtf8(shared.readAll());
    if(client.open(QIODevice::ReadOnly)) stylesheet+='\n'+QString::fromUtf8(client.readAll());
    app.setStyleSheet(stylesheet);
    UiI18n i18n(&app);
    auto settings=uiSettings("QtClient");
    i18n.setEnglish(settings.value("language","zh-CN").toString()=="en-US");
    QCommandLineParser parser; parser.addHelpOption();
    QCommandLineOption directTest("direct-pattern-regression-test","检查临时直传弹窗和清理");
    QCommandLineOption preview("ui-self-test","检查客户端原生 Qt 页面与语言切换");
    QCommandLineOption integration("api-self-test","使用临时服务端检查客户端 API 与页面");
    QCommandLineOption excelTest("excel-self-test","检查原生 Excel 导出文件");
    QCommandLineOption modalTest("modal-self-test","检查弹窗期间原窗口禁止鼠标操作");
    QCommandLineOption loginTest("login-regression-test","逐条验证客户登录问题");
    QCommandLineOption chartTest("chart-regression-test","逐条验证客户图表边界问题");
    QCommandLineOption catalogTest("catalog-regression-test","逐条验证四种目录的真实页面操作");
    QCommandLineOption patternTest("pattern-regression-test","逐条验证花型回传界面和队列");
    QCommandLineOption statisticsTest("statistics-regression-test","逐条验证有业务数据的统计页面");
    QCommandLineOption hostOption("server-host","联调服务端地址","host","127.0.0.1");
    QCommandLineOption portOption("server-port","联调服务端端口","port","18095");
    parser.addOptions({directTest,preview,integration,excelTest,modalTest,loginTest,chartTest,catalogTest,patternTest,statisticsTest,hostOption,portOption}); parser.process(app);
    if(parser.isSet(directTest)) return runDirectPatternRegression();
    if(parser.isSet(loginTest)) return runLoginRegression(app);
    if(parser.isSet(chartTest)) return runChartRegression(app);
    if(parser.isSet(catalogTest)) return runCatalogRegression(app,parser.value(hostOption),parser.value(portOption).toInt());
    if(parser.isSet(patternTest)) return runPatternRegression(app,parser.value(hostOption),parser.value(portOption).toInt());
    if(parser.isSet(statisticsTest)) return runStatisticsRegression(app,parser.value(hostOption),parser.value(portOption).toInt());
    if(parser.isSet(modalTest)) {
        QMainWindow owner;
        owner.resize(780,560); owner.show();
        QDialog dialog(&owner);
        dialog.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
        UiModalDialog::attach(&dialog,&owner);
        QTimer::singleShot(50,&dialog,[&] {
            if(dialog.windowModality()!=Qt::ApplicationModal || !dialog.isModal() ||
               QApplication::activeModalWidget()!=&dialog)
                qFatal("Qt dialog is not application modal");
            auto *overlay=owner.findChild<QWidget*>("dialogOverlay",Qt::FindDirectChildrenOnly);
            if(!overlay || !overlay->isVisible() || overlay->geometry()!=owner.rect())
                qFatal("Modal overlay does not cover the entire owner window");
            if(QGuiApplication::platformName()=="cocoa" &&
               !UiModalDialog::nativeOwnerBlocked(&owner))
                qFatal("macOS main window is not frozen during a dialog");
            if(QGuiApplication::platformName()=="cocoa" &&
               (UiModalDialog::nativeOwnerClickThrough(&owner) ||
                UiModalDialog::nativeOwnerClickThrough(&dialog)))
                qFatal("macOS dialog handling made a window click-through");
            dialog.accept();
        });
        dialog.exec();
        QApplication::processEvents();
        for(auto *overlay:owner.findChildren<QWidget*>("dialogOverlay",Qt::FindDirectChildrenOnly))
            if(overlay->isVisible()) qFatal("Modal overlay remained after closing the dialog");
        if(QGuiApplication::platformName()=="cocoa" &&
           (UiModalDialog::nativeOwnerBlocked(&owner) ||
            UiModalDialog::nativeOwnerClickThrough(&owner)))
            qFatal("macOS main window did not regain normal interaction after the dialog");
        return 0;
    }
    if(parser.isSet(excelTest)) {
        QTemporaryDir temp;
        if(!temp.isValid()) qFatal("Excel test directory failed");
        const QString input=temp.filePath("source.csv");
        QString output=qEnvironmentVariable("BOER_QT_XLSX_SELFTEST_OUTPUT");
        if(output.isEmpty()) output=temp.filePath("statistics.xlsx");
        QFile csv(input);
        if(!csv.open(QIODevice::WriteOnly) ||
           csv.write("\xEF\xBB\xBF员工姓名,加工件数,工资(元)\n\"张,三\",12,34.50\n\"王\"\"五\",0,0\n")<0)
            qFatal("Excel test source failed");
        csv.close();
        QString error;
        if(!csvToXlsx(input,output,&error)) qFatal("Excel export failed: %s",qPrintable(error));
        QFile workbook(output);
        if(!workbook.open(QIODevice::ReadOnly) || workbook.read(4)!="PK\x03\x04")
            qFatal("Excel export did not produce an XLSX archive");
        return 0;
    }
    QTemporaryDir windowTestSettings;
    if(qEnvironmentVariableIsSet("BOER_TEST_WINDOW_COMPAT")){QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,windowTestSettings.path());}
    if(qEnvironmentVariableIsSet("BOER_TEST_NETWORK_RESILIENCE"))return runNetworkResilienceRegression();
    ClientWindow window;
    window.resize(1440,900);
    window.show();
    if(qEnvironmentVariableIsSet("BOER_TEST_MULTI_CLIENT"))return runConcurrentClientRegression(window,parser.value(hostOption),parser.value(portOption).toInt());
    if(qEnvironmentVariableIsSet("BOER_TEST_LARGE_DEVICE_API"))return runClientLargeDeviceCheck(window,parser.value(hostOption),parser.value(portOption).toInt());
    if(qEnvironmentVariableIsSet("BOER_TEST_INTERACTION_COMPAT")){window.previewSession({{"username","interaction-test"},{"nickname","交互测试"},{"role","admin"}});return runClientInteractionRegression(window);}
    if(qEnvironmentVariableIsSet("BOER_TEST_WINDOW_COMPAT")) {
        int loginFailures=0;
        app.processEvents();for(bool english:{false,true}){
            i18n.setEnglish(english);waitWindowCompatibilityLayout();bool accessible=window.screen()->availableGeometry().contains(window.frameGeometry());QStringList loginIssues;if(!accessible)loginIssues<<"window frame outside screen";
            QList<QWidget*> controls;for(auto *line:window.findChildren<QLineEdit*>("clientLoginInput"))controls<<line;controls<<window.findChild<QPushButton*>("clientLoginSubmit");
            for(auto *control:controls){if(!control){accessible=false;continue;}for(int pass=0;pass<3;++pass){for(auto *scroll:window.findChildren<QScrollArea*>())if(scroll->isAncestorOf(control)){const QPoint center=control->mapTo(scroll->widget(),control->rect().center());scroll->ensureVisible(center.x(),center.y(),control->width()/2+8,control->height()/2+8);}app.processEvents();}const QRect region=control->visibleRegion().boundingRect();const bool fits=region.contains(control->rect());if(!fits)loginIssues<<QString("%1 rect=%2x%3 visible=%4,%5 %6x%7").arg(control->objectName()).arg(control->width()).arg(control->height()).arg(region.x()).arg(region.y()).arg(region.width()).arg(region.height());accessible&=fits;}
            recordWindowCompatibility("client",english ? "en 登录窗口及输入按钮可访问" : "zh 登录窗口及输入按钮可访问",accessible,loginIssues.join("; "));loginFailures+=!accessible;
            QDir().mkpath(qEnvironmentVariable("BOER_WINDOW_ARTIFACT_DIR"));window.grab().save(qEnvironmentVariable("BOER_WINDOW_ARTIFACT_DIR")+(english ? "/client-login-en.png" : "/client-login-zh.png"));
        }
        window.previewSession({{"username","window-test"},{"nickname","窗口兼容测试"},{"role","admin"}});
        const QStringList pages={"/home","/dashboard","/device/list","/device/group","/device/monitor","/device/types","/device/electric-control-types","/file/pattern","/file/pattern-types","/file/order-nos","/file/queue","/file/log","/statistics/salary","/statistics/process","/statistics/duration","/statistics/alarm","/employee/list","/profile/info","/profile/password","/support/contact","/support/manual"};
        const int compatibilityResult=runWindowCompatibility(window,"client",pages,[&](const QString &path){window.navigate(path);},[&]{
            for(bool english:{false,true}){i18n.setEnglish(english);window.navigate("/employee/list");app.processEvents();QPushButton *add=nullptr;for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()==i18n.text("新增员工"))add=button;if(!add){recordWindowCompatibility("client","employee form reachable",false);continue;}scheduleWindowDialogCheck(window,"client",english ? "employee-en" : "employee-zh");add->click();}
            window.notify(QString("Long notification keeps all details visible after resizing. ").repeated(6),true);checkWindowNotification(window,"client","clientToast");
        });
        return compatibilityResult || loginFailures ? 1 : 0;
    }
    if(parser.isSet(preview)) {
        QTimer::singleShot(120000,&window,[] { qFatal("Qt client UI self-test timed out"); });
        QTimer::singleShot(0,&window,[&] {
            auto *language=window.findChild<QPushButton*>("languageSwitch");
            if(!language) qFatal("Client login language switch missing");
            const bool initialEnglish=i18n.english();
            language->click(); app.processEvents();
            if(i18n.english()==initialEnglish) qFatal("Client login language click did not switch");
            language->click(); app.processEvents();
            if(i18n.english()!=initialEnglish) qFatal("Client login language switch did not return");
            const QString snapshotDir=qEnvironmentVariable("BOER_QT_CLIENT_SNAPSHOT_DIR");
            if(!snapshotDir.isEmpty()) {
                QDir().mkpath(snapshotDir);
                window.grab().save(QDir(snapshotDir).filePath("login.png"));
            }
            QList<QLineEdit*> loginHistoryInputs;
            for(auto *input:window.findChildren<QLineEdit*>())
                if(input->objectName()=="clientLoginInput" && input->completer())
                    loginHistoryInputs << input;
            if(loginHistoryInputs.size()!=2) qFatal("Client login history inputs missing");
            QLineEdit *loginPassword=nullptr;
            for(auto *input:window.findChildren<QLineEdit*>("clientLoginInput"))
                if(input->echoMode()==QLineEdit::Password) loginPassword=input;
            if(!loginHistoryInputs[1]->isClearButtonEnabled() || !loginPassword ||
               !loginPassword->isClearButtonEnabled())
                qFatal("Client account or password clear action missing");
            for(auto *input:loginHistoryInputs) {
                if(input->height()!=36) qFatal("Client login input height differs from server login");
                auto *completer=input->completer();
                completer->setModel(new QStringListModel({"admin","administrator"},completer));
                completer->setCompletionPrefix("adm");
                completer->complete(); app.processEvents();
                auto *popup=completer->popup();
                if(!popup || popup->sizeHintForRow(0)<input->height() ||
                   popup->width()<input->width()-2 ||
                   popup->geometry().top()<input->mapToGlobal(QPoint(0,input->height())).y()-3)
                    qFatal("Client login suggestions do not match the input height or position");
                if(!snapshotDir.isEmpty())
                    popup->grab().save(QDir(snapshotDir).filePath(
                        input==loginHistoryInputs.first() ? "server-history-dropdown.png" : "account-history-dropdown.png"));
                popup->hide();
            }
            {
                QTemporaryDir files;
                if(!files.isValid()) qFatal("Client file chooser test directory failed");
                const QString source=files.filePath("import.csv");
                QFile file(source);
                if(!file.open(QIODevice::WriteOnly) || file.write("code,name\n1,test\n")<0)
                    qFatal("Client file chooser test source failed");
                file.close();
                QTimer::singleShot(50,&window,[&] {
                    auto *chooser=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                    if(!chooser || !chooser->testOption(QFileDialog::DontUseNativeDialog))
                        qFatal("Client import chooser did not open as a visible Qt dialog");
                    if(!snapshotDir.isEmpty())
                        chooser->grab().save(QDir(snapshotDir).filePath("import-file-dialog.png"));
                    selectTestFile(chooser,source);
                    QMetaObject::invokeMethod(chooser,"accept",Qt::DirectConnection);
                });
                if(ClientFileDialog::getOpenFileName(&window,"选择导入文件",{},"CSV (*.csv)")!=source)
                    qFatal("Client import chooser returned the wrong file");
                const QString target=files.filePath("export.xlsx");
                QTimer::singleShot(50,&window,[&] {
                    auto *chooser=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                    if(!chooser || !chooser->testOption(QFileDialog::DontUseNativeDialog))
                        qFatal("Client export chooser did not open as a visible Qt dialog");
                    if(!snapshotDir.isEmpty())
                        chooser->grab().save(QDir(snapshotDir).filePath("export-file-dialog.png"));
                    selectTestFile(chooser,target);
                    QMetaObject::invokeMethod(chooser,"accept",Qt::DirectConnection);
                });
                if(ClientFileDialog::getSaveFileName(&window,"导出 Excel","export.xlsx","Excel (*.xlsx)")!=target)
                    qFatal("Client export chooser returned the wrong destination");
                ClientFormDialog import(&window,"批量导入员工",560,350);
                FormField field; field.label="导入文件"; field.key="file";
                field.kind=FormField::File; field.maximum=0; import.addField(field);
                QTimer::singleShot(40,&import,[&] {
                    QPushButton *browse=nullptr;
                    for(auto *button:import.findChildren<QPushButton*>())
                        if(button->text()=="选择文件") { browse=button; break; }
                    if(!browse) qFatal("Client import browse button missing");
                    QTimer::singleShot(40,&import,[&] {
                        auto *chooser=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                        if(!chooser) qFatal("Client nested import chooser did not open");
                        selectTestFile(chooser,source);
                        QMetaObject::invokeMethod(chooser,"accept",Qt::DirectConnection);
                    });
                    browse->click();
                    auto *path=import.findChild<QLineEdit*>("filePathInput");
                    if(!path || path->text()!=source) qFatal("Client nested import did not select the file");
                    import.reject();
                });
                import.execute();
            }
            window.previewSession({{"id",1},{"username","preview"},{"nickname","预览账号"},
                {"role","admin"},{"permissions","{}"}});
            app.processEvents();
            if(window.currentPath().isEmpty()) qFatal("Qt client navigation was not created");
            auto *menu=window.findChild<QTreeWidget*>("clientNavigation");
            if(!menu || menu->topLevelItemCount()!=8) qFatal("Client main menu is incomplete");
            for(int index=0;index<menu->topLevelItemCount();++index)
                if(menu->topLevelItem(index)->icon(0).isNull() ||
                   menu->topLevelItem(index)->icon(0).pixmap(18,18).isNull())
                    qFatal("Client main menu icon missing");
            if(!snapshotDir.isEmpty()) {
                window.grab().save(QDir(snapshotDir).filePath("home.png"));
                window.navigate("/dashboard"); app.processEvents();
                window.grab().save(QDir(snapshotDir).filePath("dashboard.png"));
            }
            const QStringList paths={"/device/list","/device/group","/device/monitor","/device/types",
                "/device/electric-control-types","/file/pattern","/file/pattern-types",
                "/file/order-nos","/file/queue","/file/log","/statistics/salary",
                "/statistics/process","/statistics/duration","/statistics/alarm",
                "/employee/list","/profile/info","/profile/password",
                "/support/contact","/support/manual"};
            for(const QString &path:paths) {
                window.navigate(path); app.processEvents();
                if(window.currentPath()!=path) qFatal("Qt client route did not open");
                qInfo("CUSTOMER_CHECK page opened: %s",qPrintable(path));
                if(path=="/profile/password" && !snapshotDir.isEmpty()) {
                    const QSize originalSize=window.size();const bool originalEnglish=i18n.english();
                    for(bool english:{false,true})for(const QSize size:{QSize(1000,650),QSize(1280,720),QSize(1600,900)}) {
                        i18n.setEnglish(english);window.resize(size);app.processEvents();
                        window.grab().save(QDir(snapshotDir).filePath(QString("password-%1-%2.png").arg(english ? "en" : "zh").arg(size.width())));
                    }
                    i18n.setEnglish(originalEnglish);window.resize(originalSize);app.processEvents();
                }
                if(!snapshotDir.isEmpty() && QStringList{"/device/list","/device/monitor","/file/pattern",
                    "/statistics/salary","/employee/list","/profile/info","/profile/password","/support/contact"}.contains(path))
                    window.grab().save(QDir(snapshotDir).filePath(path.mid(1).replace('/','-')+".png"));
            }
            window.navigate("/file/pattern-types"); app.processEvents();
            QPushButton *createType=nullptr;
            for(auto *button:window.findChildren<QPushButton*>())
                if(button->isVisible() && button->text()=="新增类型") { createType=button; break; }
            if(!createType) qFatal("Client catalog create action missing");
            QTimer::singleShot(40,&window,[&] {
                auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                auto *overlay=window.findChild<QWidget*>("dialogOverlay");
                if(!dialog || !overlay || overlay->parentWidget()!=&window ||
                   overlay->geometry()!=window.rect() ||
                   dialog->windowModality()!=Qt::ApplicationModal)
                    qFatal("Client catalog dialog overlay does not cover the sidebar");
                if(!snapshotDir.isEmpty()) {
                    window.grab().save(QDir(snapshotDir).filePath("catalog-overlay.png"));
                    dialog->grab().save(QDir(snapshotDir).filePath("catalog-modal.png"));
                }
                dialog->reject();
            });
            createType->click(); app.processEvents();
            window.navigate("/employee/list"); app.processEvents();
            QPushButton *createEmployee=nullptr;
            for(auto *button:window.findChildren<QPushButton*>())
                if(button->isVisible() && button->text()=="新增员工") { createEmployee=button; break; }
            if(!createEmployee) qFatal("Client employee create action missing");
            QTimer::singleShot(40,&window,[&] {
                auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                auto *scroll=dialog ? dialog->findChild<QScrollArea*>("dialogScroll") : nullptr;
                auto *content=dialog ? dialog->findChild<QWidget*>("dialogContent") : nullptr;
                if(!dialog || !scroll || !content ||
                   dialog->windowModality()!=Qt::ApplicationModal ||
                   scroll->horizontalScrollBarPolicy()!=Qt::ScrollBarAlwaysOff ||
                   scroll->horizontalScrollBar()->isVisible() ||
                   content->width()>scroll->viewport()->width()+1)
                    qFatal("Client employee form scrolls horizontally");
                const QColor background=content->grab(QRect(3,3,1,1)).toImage().pixelColor(0,0);
                if(background.red()<245 || background.green()<245 || background.blue()<245)
                    qFatal("Client employee form background is not white");
                QEvent leave(QEvent::Leave);
                QApplication::sendEvent(dialog,&leave); app.processEvents();
                if(!dialog->isVisible()) qFatal("Client employee dialog disappeared on mouse leave");
                qInfo("CUSTOMER_PASS rows=344 employee dialog remains open on mouse leave");
                auto *input=dialog->findChild<QLineEdit*>();
                if(!input) qFatal("Client employee form input missing");
                const QString original=input->text();
                input->setText("123456"); input->selectAll(); input->copy();
                if(QApplication::clipboard()->text()!="123456") qFatal("Client form input cannot copy");
                input->clear(); input->paste();
                if(input->text()!="123456") qFatal("Client form input cannot paste");
                input->setText(original);
                qInfo("CUSTOMER_PASS rows=343 employee form input copy and paste");
                if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("employee-create-modal.png"));
                dialog->reject();
            });
            createEmployee->click(); app.processEvents();
            window.navigate("/device/list"); app.processEvents();
            if(auto *combo=window.findChild<QComboBox*>("clientFilterCombo")) {
                auto *filter=combo->parentWidget();
                while(filter && filter->objectName()!="clientFilter") filter=filter->parentWidget();
                auto *search=filter ? filter->findChild<QLineEdit*>() : nullptr;
                if(combo->height()!=28 || !search || search->height()!=28)
                    qFatal("Client filter inputs differ from server filter height");
                combo->showPopup(); app.processEvents();
                QWidget *popup=combo->view()->window();
                if(!popup || combo->view()->sizeHintForRow(0)<combo->height() ||
                   popup->geometry().top()<combo->mapToGlobal(QPoint(0,combo->height())).y()-3)
                    qFatal("Client dropdown row height or popup placement failed");
                if(!snapshotDir.isEmpty()) popup->grab().save(QDir(snapshotDir).filePath("filter-dropdown.png"));
                combo->hidePopup();
            } else qFatal("Client standard filter dropdown missing");
            {
                window.navigate("/file/pattern");
                QTemporaryDir files;
                if(!files.isValid()) qFatal("Client upload test directory failed");
                QStringList paths;
                for(const QString &filename:{"中文花型.slw","second.xdg"}) {
                    const QString path=files.filePath(filename);
                    QFile file(path);
                    if(!file.open(QIODevice::WriteOnly) || file.write("PATTERN")!=7)
                        qFatal("Client upload test file failed");
                    paths << path;
                }
                QPushButton *add=nullptr;
                for(auto *button:window.findChildren<QPushButton*>())
                    if(button->text()=="添加文件") { add=button; break; }
                if(!add) qFatal("Client pattern upload action missing");
                QTimer::singleShot(40,&window,[&] {
                    auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                    auto *drop=dialog ? dialog->findChild<QFrame*>("clientPatternDrop") : nullptr;
                    auto *table=dialog ? dialog->findChild<QTableWidget*>("clientTable") : nullptr;
                    QLineEdit *name=nullptr;
                    if(dialog) for(auto *line:dialog->findChildren<QLineEdit*>())
                        if(line->placeholderText()=="请输入花型名称") name=line;
                    if(!drop || !table || !name) qFatal("Client multi-file upload UI missing");
                    auto *mode=dialog->findChild<QComboBox*>("patternUploadMode");
                    if(!mode || mode->currentData().toBool())qFatal("Original server upload must remain the default");
                    mode->setCurrentIndex(1);app.processEvents();
                    if(!table->isColumnHidden(2) || !table->isColumnHidden(5))qFatal("Direct mode must hide server-only metadata");
                    mode->setCurrentIndex(0);app.processEvents();
                    if(table->isColumnHidden(2) || table->isColumnHidden(5))qFatal("Normal upload fields must be restored");
                    auto *overlay=window.findChild<QWidget*>("dialogOverlay");
                    if(dialog->windowModality()!=Qt::ApplicationModal || !overlay ||
                       !overlay->isVisible() || overlay->geometry()!=window.rect())
                        qFatal("Client pattern upload left the main window interactive");
                    QMimeData mime;
                    QList<QUrl> urls; for(const QString &path:paths) urls << QUrl::fromLocalFile(path);
                    mime.setUrls(urls);
                    QDragEnterEvent enter(QPoint(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(drop,&enter);
                    QDropEvent event(QPointF(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(drop,&event);
                    if(table->rowCount()!=2) qFatal("Client multi-file drop failed");
                    table->setCurrentCell(0,0); name->setText("用户填写的中文花型");
                    table->setCurrentCell(1,0); name->setText("Second Pattern");
                    table->setCurrentCell(0,0);
                    if(name->text()!="用户填写的中文花型" || table->item(1,1)->text()!="Second Pattern")
                        qFatal("Client per-file metadata was not preserved");
                    i18n.setEnglish(true); app.processEvents();
                    if(table->item(0,0)->text()!="中文花型.slw" ||
                       table->item(0,1)->text()!="用户填写的中文花型" ||
                       table->horizontalHeaderItem(0)->text()=="文件名")
                        qFatal("Client upload dialog translated user data or missed labels");
                    i18n.setEnglish(false); app.processEvents();
                    if(!snapshotDir.isEmpty())
                        dialog->grab().save(QDir(snapshotDir).filePath("pattern-upload.png"));
                    dialog->reject();
                });
                add->click();
            }
            {
                ClientTable table;
                table.setColumns({{"名称","name",160},{"状态","status",100}},true,
                    {"修改名称","清空类型"},{});
                table.setRecords(QJsonArray{QJsonObject{{"id",17},{"name","用户输入的设备名"},{"status","idle"}}});
                table.resize(620,180); table.show(); app.processEvents();
                auto *item=table.item(0,1);
                if(!item) qFatal("Client table row missing");
                const QPointF at=table.visualItemRect(item).center();
                auto click=[&] {
                    const QPointF global=table.viewport()->mapToGlobal(at.toPoint());
                    QMouseEvent press(QEvent::MouseButtonPress,at,at,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QMouseEvent release(QEvent::MouseButtonRelease,at,at,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(table.viewport(),&press);
                    QApplication::sendEvent(table.viewport(),&release); app.processEvents();
                };
                click(); if(table.checkedIds()!=QList<qint64>{17}) qFatal("Client row click did not check row");
                click(); if(!table.checkedIds().isEmpty()) qFatal("Client row click did not clear row");
                auto *frozen=table.findChild<QTableWidget*>("clientFrozenActions");
                if(!snapshotDir.isEmpty()) table.grab().save(QDir(snapshotDir).filePath("table-actions.png"));
                if(frozen) qInfo("CUSTOMER_CHECK actions width=%d left=%d right=%d viewport=%d..%d visible=%d",
                    frozen->width(),frozen->geometry().left(),frozen->geometry().right(),
                    table.viewport()->geometry().left(),table.viewport()->geometry().right(),frozen->isVisible());
                if(!frozen || frozen->geometry().left()<table.viewport()->geometry().left() ||
                   frozen->width()<100 || !frozen->isVisible() ||
                   frozen->geometry().right()>table.viewport()->geometry().right()+1)
                    qFatal("Client fixed action column overlaps, is hidden, or is too narrow");
                for(auto *button:frozen->findChildren<QPushButton*>())
                    if(button->isVisible() && button->width()<button->fontMetrics().horizontalAdvance(button->text())+8)
                        qFatal("Client action column clips button text");
                if(!snapshotDir.isEmpty()) table.grab().save(QDir(snapshotDir).filePath("table-actions.png"));
                ClientTable manyActions;
                manyActions.setColumns({{"花型名称","name",180}},true,
                    {"预览","编辑","下载","下发","删除"},{});
                manyActions.setRecords(QJsonArray{QJsonObject{{"id",18},{"name","测试花型"}}});
                manyActions.resize(740,170); manyActions.show(); app.processEvents();
                auto *fixedActions=manyActions.findChild<QTableWidget*>("clientFrozenActions");
                if(!fixedActions || !fixedActions->isVisible()) qFatal("Client five-action column is hidden");
                for(auto *button:fixedActions->findChildren<QPushButton*>())
                    if(button->isVisible() && button->width()<button->fontMetrics().horizontalAdvance(button->text())+8)
                        qFatal("Client action button text is squeezed");
                if(!snapshotDir.isEmpty())
                    manyActions.grab().save(QDir(snapshotDir).filePath("table-five-actions.png"));
            }
            {
                ClientFormDialog checkDialog(&window,"设备选择测试",500,370);
            FormField targets; targets.label="目标设备"; targets.key="deviceIds";
            targets.kind=FormField::MultiChoice;
            targets.choices={{"用户输入的中文设备名",QJsonValue(1)}};
            checkDialog.addField(targets);
            QTimer::singleShot(40,&checkDialog,[&] {
                auto *tree=checkDialog.findChild<QTreeWidget*>();
                if(!tree || tree->topLevelItemCount()!=1) qFatal("Client device choice tree missing");
                auto *item=tree->topLevelItem(0);
                const QRect row=tree->visualItemRect(item);
                const QPointF at(row.left()+95,row.center().y());
                auto click=[tree,at] {
                    const QPointF global=tree->viewport()->mapToGlobal(at.toPoint());
                    QMouseEvent press(QEvent::MouseButtonPress,at,at,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QMouseEvent release(QEvent::MouseButtonRelease,at,at,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(tree->viewport(),&press);
                    QApplication::sendEvent(tree->viewport(),&release);
                    QApplication::processEvents();
                };
                click(); if(item->checkState(0)!=Qt::Checked || tree->hasFocus())
                    qFatal("Client choice name did not toggle cleanly");
                click(); if(item->checkState(0)!=Qt::Unchecked)
                    qFatal("Client choice name second click did not clear");
                checkDialog.reject();
            });
            checkDialog.execute();
            }
            i18n.setEnglish(true); app.processEvents();
            if(!snapshotDir.isEmpty()) {
                for(bool english:{false,true}) {
                    i18n.setEnglish(english);
                    for(const QSize size:{QSize(1600,1000),QSize(1120,700)}) {
                        window.resize(size); app.processEvents();
                        for(const QString &path:paths) {
                            window.navigate(path); app.processEvents();
                            if(window.currentPath()!=path) qFatal("Client responsive route failed");
                            const QString filename=QString("matrix-%1-%2-%3.png")
                                .arg(english ? "en" : "zh").arg(size.width()).arg(path.mid(1).replace('/','-'));
                            if(!window.grab().save(QDir(snapshotDir).filePath(filename))) qFatal("Client matrix snapshot failed");
                        }
                    }
                }
                window.resize(1440,900); app.processEvents();
                qInfo("CUSTOMER_CHECK 76 page/language/window-size snapshots saved; visual review required");
            }
            const QRegularExpression chinese("[\\x{4e00}-\\x{9fff}]");
            QSet<QString> untranslated;
            auto check=[&](const QString &value){ if(value.contains(chinese)) untranslated.insert(value); };
            for(QWidget *widget:QApplication::allWidgets()) if(widget && !widget->property("i18nSkip").toBool()) {
                if(auto *label=qobject_cast<QLabel*>(widget)) check(label->text());
                if(auto *button=qobject_cast<QAbstractButton*>(widget)) check(button->text());
                if(auto *line=qobject_cast<QLineEdit*>(widget)) check(line->placeholderText());
                if(auto *combo=qobject_cast<QComboBox*>(widget)) for(int i=0;i<combo->count();++i)
                    if(!combo->itemData(i,UiI18n::PreserveDataRole).toBool()) check(combo->itemText(i));
                if(auto *tree=qobject_cast<QTreeWidget*>(widget)) for(QTreeWidgetItemIterator it(tree);*it;++it)
                    if(!(*it)->data(0,UiI18n::PreserveDataRole).toBool()) check((*it)->text(0));
                if(auto *table=qobject_cast<QTableWidget*>(widget)) {
                    for(int col=0;col<table->columnCount();++col) {
                        if(auto *header=table->horizontalHeaderItem(col)) check(header->text());
                        for(int row=0;row<table->rowCount();++row)
                            if(auto *item=table->item(row,col); item &&
                               !item->data(UiI18n::PreserveDataRole).toBool()) check(item->text());
                    }
                }
            }
            for(const QString &value:untranslated) qWarning("client untranslated: %s",qPrintable(value));
            if(!untranslated.isEmpty()) qFatal("Qt client contains untranslated interface labels");
            {
                ClientFormDialog translated(&window,"编辑设备",500,340);
                FormField originalName; originalName.label="设备名称"; originalName.key="name";
                translated.addField(originalName,"用户填写的中文设备名");
                FormField group; group.label="所属分组"; group.key="groupId";
                group.kind=FormField::Choice;
                group.choices={{"未分组",QJsonValue(QJsonValue::Null)},
                    {"苏州车间",QJsonValue(2)}};
                translated.addField(group,QJsonValue(2));
                QTimer::singleShot(40,&translated,[&] {
                    auto *title=translated.findChild<QLabel*>("dialogTitle");
                    auto *name=translated.findChild<QLineEdit*>();
                    auto *choice=translated.findChild<QComboBox*>();
                    if(!title || title->text()=="编辑设备" || !name ||
                       name->text()!="用户填写的中文设备名" || !choice ||
                       choice->itemText(0)=="未分组" || choice->itemText(1)!="苏州车间" ||
                       name->height()!=36 || choice->height()!=36)
                        qFatal("Client dialog translation changed business data or missed labels");
                    translated.reject();
                });
                translated.execute();
            }
            i18n.setEnglish(false); app.processEvents();
            {
                ClientWindow limited;
                limited.previewSession({{"id",8},{"username","restricted"},{"role","user"},
                    {"permissions","{\"home\":false,\"dashboard\":false,\"deviceManagement\":true,\"deviceInfo\":true,\"remoteMonitoring\":false,\"fileManagement\":false,\"statistics\":false,\"employeeManagement\":false}"}});
                if(limited.currentPath()!="/device/list" || limited.hasPermission("patternFiles"))
                    qFatal("Client restricted default route failed");
                limited.navigate("/file/pattern");
                if(limited.currentPath()!="/device/list") qFatal("Client permission route guard failed");
            }
            {
                ClientWindow limited;
                limited.previewSession({{"id",9},{"username","device-pattern-user"},{"role","user"},
                    {"permissions","{\"home\":false,\"dashboard\":false,\"deviceManagement\":false,\"fileManagement\":true,\"patternFiles\":false,\"devicePatternFiles\":true,\"downloadLog\":false,\"statistics\":false,\"employeeManagement\":false}"}});
                limited.navigate("/file/pattern");
                auto *server=limited.findChild<QWidget*>("clientServerPatterns");
                auto *device=limited.findChild<QWidget*>("clientDevicePatterns");
                if(limited.currentPath()!="/file/pattern" || !server || !device ||
                   server->isVisible() || device->isHidden())
                    qFatal("Client pattern section permissions failed");
            }
            {
                ClientWindow remoteOnly;
                remoteOnly.previewSession({{"id",10},{"username","remote-user"},{"role","user"},
                    {"permissions","{\"home\":false,\"dashboard\":false,\"deviceManagement\":true,\"deviceInfo\":false,\"remoteMonitoring\":true,\"fileManagement\":false,\"statistics\":false,\"employeeManagement\":false}"}});
                if(remoteOnly.currentPath()!="/device/monitor" ||
                   !remoteOnly.findChild<QWidget*>("clientPage"))
                    qFatal("Client remote data page permission route failed");
            }
            app.quit();
        });
    }
    if(parser.isSet(integration)) {
        auto *api=window.api();
        api->setServer(parser.value(hostOption),parser.value(portOption).toInt());
        auto *failures=new QStringList;
        QObject::connect(api,&ApiClient::requestObserved,&window,[failures](const QString &path,int status,bool ok) {
            if(!ok) failures->append(path+" ("+QString::number(status)+")");
        });
        QTimer::singleShot(0,&window,[&,failures] {
            api->post("/auth/login",QJsonObject{{"username","admin"},{"password","admin123"}},
                [&,failures](const ApiResponse &result) {
                    if(!result.ok) qFatal("Qt client test login failed");
                    const QJsonObject data=result.data.toObject();
                    window.testSession(parser.value(hostOption),parser.value(portOption).toInt(),
                        data.value("user").toObject(),data.value("token").toString());
                    const QStringList routes={"/home","/dashboard","/device/list","/device/group","/device/monitor",
                        "/device/types","/device/electric-control-types","/file/pattern",
                        "/file/pattern-types","/file/order-nos","/file/queue","/file/log",
                        "/statistics/salary","/statistics/process","/statistics/duration",
                        "/statistics/alarm","/employee/list","/profile/info","/profile/password",
                        "/support/contact","/support/manual"};
                    const QString liveSnapshot=qEnvironmentVariable("BOER_QT_CLIENT_LIVE_SNAPSHOT_DIR");
                    if(!liveSnapshot.isEmpty()) QDir().mkpath(liveSnapshot);
                    auto sequence=std::make_shared<std::function<void(int)>>();
                    *sequence=[&,sequence,routes,failures,liveSnapshot](int index) {
                        if(index>=routes.size()) {
                            QTimer::singleShot(1800,&window,[&,failures] {
                                QList<QPair<QTableWidgetItem*,QString>> preserved;
                                for(auto *table:window.findChildren<QTableWidget*>())
                                    for(int row=0;row<table->rowCount();++row)
                                        for(int column=0;column<table->columnCount();++column)
                                            if(auto *item=table->item(row,column);
                                               item && item->data(UiI18n::PreserveDataRole).toBool())
                                                preserved.append({item,item->text()});
                                i18n.setEnglish(true); app.processEvents();
                                for(const auto &entry:preserved)
                                    if(entry.first->text()!=entry.second)
                                        qFatal("Client translated a user-entered data value");
                                auto *nav=window.findChild<QTreeWidget*>("clientNavigation");
                                if(!nav || nav->topLevelItemCount()<1 || nav->topLevelItem(0)->text(0)=="首页")
                                    qFatal("Client main-window language switch failed");
                                i18n.setEnglish(false); app.processEvents();
                                runClientUiEmployeeSmoke(&window,[&,failures](const QString &employeeError) {
                                    if(!employeeError.isEmpty()) failures->append(employeeError);
                                    runClientUiPatternDownloadSmoke(&window,[&,failures](const QString &patternError) {
                                        if(!patternError.isEmpty()) failures->append(patternError);
                                        runClientUiExportSmoke(&window,[&,failures](const QString &uiError) {
                                            if(!uiError.isEmpty()) failures->append(uiError);
                                            runClientExportSmoke(api,[&,failures](const QString &exportError) {
                                                if(!exportError.isEmpty()) failures->append(exportError);
                                                runClientWriteSmoke(api,[&,failures](const QString &error) {
                                                    if(!error.isEmpty()) failures->append(error);
                                                    if(!failures->isEmpty()) {
                                                        for(const QString &item:*failures) qWarning("Client API failed: %s",qPrintable(item));
                                                        qFatal("Qt client API page test failed");
                                                    }
                                                    delete failures; app.quit();
                                                });
                                            });
                                        });
                                    });
                                });
                            });
                            return;
                        }
                        window.navigate(routes[index]);
                        QTimer::singleShot(180,&window,[&,sequence,index,routes,liveSnapshot] {
                            if(!liveSnapshot.isEmpty())
                                window.grab().save(QDir(liveSnapshot).filePath(routes[index].mid(1).replace('/','-')+".png"));
                            (*sequence)(index+1);
                        });
                    };
                    (*sequence)(0);
                });
        });
        QTimer::singleShot(20000,&window,[&] { qFatal("Qt client API self-test timed out"); });
    }
    startOnlineUpdate("client");
    return app.exec();
}
