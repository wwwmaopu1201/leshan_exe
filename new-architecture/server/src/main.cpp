#include "online_update.h"
#include "ui_settings.h"
#include "api_server.h"
#include "database.h"
#include "device_server.h"
#include "editor_dialog.h"
#include "main_window.h"
#include "ui_controls.h"
#include "ui_i18n.h"
#include "ui_dialogs.h"
#include "window_compatibility.h"
#include <QScreen>
#include "trial_state.h"
int runCustomerUiRegression();

#include <QApplication>
#include <QAction>
#include <QAbstractItemView>
#include <QCalendarWidget>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDialog>
#include <QCheckBox>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QPalette>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QScrollArea>
#include <QScrollBar>
#include <QSaveFile>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <memory>
#include <cstdio>

namespace {
QFile *runtimeLog=nullptr;
QMutex runtimeLogMutex;
bool mirrorLogToStdout=false;
bool envEnabled(const QString &name,bool fallback) {
    const QString value=qEnvironmentVariable(name.toUtf8().constData()).trimmed().toLower();
    if(value.isEmpty()) return fallback;
    if(QStringList{"1","true","yes","on"}.contains(value)) return true;
    if(QStringList{"0","false","no","off"}.contains(value)) return false;
    return fallback;
}
void fileMessageHandler(QtMsgType type,const QMessageLogContext &,const QString &message) {
    const QByteArray level=type==QtDebugMsg ? "DEBUG" : type==QtWarningMsg ? "WARN" :
        type==QtCriticalMsg ? "ERROR" : type==QtFatalMsg ? "FATAL" : "INFO";
    const QByteArray line=QDateTime::currentDateTime().toString("yyyy/MM/dd HH:mm:ss.zzz ").toUtf8()+
        level+" "+message.toUtf8()+"\n";
    QMutexLocker lock(&runtimeLogMutex);
    if(runtimeLog && runtimeLog->isOpen()) { runtimeLog->write(line); runtimeLog->flush(); }
    if(mirrorLogToStdout) { std::fwrite(line.constData(),1,size_t(line.size()),stdout); std::fflush(stdout); }
}
void setupRuntimeLog(const QString &databasePath) {
    const QString configured=qEnvironmentVariable("LOG_DIR").trimmed();
    const QString directory=configured.isEmpty() ?
        QDir(QFileInfo(databasePath).absolutePath()).filePath("logs") : configured;
    if(!QDir().mkpath(directory)) return;
    auto *file=new QFile(QDir(directory).filePath("server_"+QDate::currentDate().toString("yyyy-MM-dd")+".log"));
    if(!file->open(QIODevice::WriteOnly|QIODevice::Append)) { delete file; return; }
    runtimeLog=file;
    mirrorLogToStdout=envEnabled("LOG_TO_STDOUT",!envEnabled("QUIET_MODE",true));
    qInstallMessageHandler(fileMessageHandler);
    qInfo("Qt server log initialized");
}
QHash<QString,QString> readConfig(const QString &path,QString *error) {
    QHash<QString,QString> values;
    QFile file(path);
    if(!file.exists()) { if(error) *error="配置文件不存在："+path; return values; }
    if(!file.open(QIODevice::ReadOnly|QIODevice::Text)) {
        if(error) *error="无法读取配置文件："+path; return values;
    }
    QString section;
    for(QString line:QString::fromUtf8(file.readAll()).split('\n')) {
        bool quoted=false; QChar quote;
        for(int i=0;i<line.size();++i) {
            if((line[i]=='\'' || line[i]=='"') && (i==0 || line[i-1]!='\\')) {
                if(!quoted) { quoted=true; quote=line[i]; }
                else if(line[i]==quote) quoted=false;
            } else if(line[i]=='#' && !quoted) { line=line.left(i); break; }
        }
        const bool nested=!line.isEmpty() && line[0].isSpace();
        line=line.trimmed();
        if(line.isEmpty()) continue;
        const int colon=line.indexOf(':');
        if(colon<1) continue;
        const QString key=line.left(colon).trimmed();
        QString value=line.mid(colon+1).trimmed();
        if(!nested && value.isEmpty()) { section=key; continue; }
        if(value.size()>=2 && ((value.startsWith('"') && value.endsWith('"')) ||
                               (value.startsWith('\'') && value.endsWith('\'')))) value=value.mid(1,value.size()-2);
        values[(section.isEmpty() ? QString() : section+".")+key]=value;
    }
    return values;
}
}

int main(int argc,char **argv) {
    bool consoleMode=false;
    for(int i=1;i<argc;++i) if(QString::fromLocal8Bit(argv[i])=="--headless" ||
                               QString::fromLocal8Bit(argv[i])=="--self-test" ||
                               QString::fromLocal8Bit(argv[i])=="--trial-self-test") consoleMode=true;
    std::unique_ptr<QCoreApplication> app(consoleMode ? static_cast<QCoreApplication*>(new QCoreApplication(argc,argv)) : static_cast<QCoreApplication*>(new QApplication(argc,argv)));
    // Keep the storage identity stable for existing installations.
    QCoreApplication::setApplicationName("Boer Qt Server");
    QGuiApplication::setApplicationDisplayName("Bohr Qt Server");
    QCoreApplication::setApplicationVersion(BOER_APP_VERSION);
    std::unique_ptr<QTemporaryDir> isolatedCliSettings;
    bool testInvocation=false;for(const auto &argument:QCoreApplication::arguments())testInvocation|=argument=="--ui-self-test" || argument=="--customer-ui-regression";
    if(testInvocation && qEnvironmentVariableIsEmpty("BOER_TEST_SETTINGS_DIR")){isolatedCliSettings=std::make_unique<QTemporaryDir>();QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,isolatedCliSettings->path());}

    const QString isolatedSettings=qEnvironmentVariable("BOER_TEST_SETTINGS_DIR");
    if(!isolatedSettings.isEmpty()){QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,isolatedSettings);}

    if(!consoleMode) {
        auto *gui=static_cast<QApplication*>(app.get());
        gui->setStyle(new UiCheckboxStyle(QStyleFactory::create("Fusion")));
        gui->setFont(QFont("PingFang SC",12));
        QPalette palette=gui->palette(); palette.setColor(QPalette::PlaceholderText,QColor("#b6bec9")); gui->setPalette(palette);
        gui->setWindowIcon(QIcon(":/app-icon.png"));
        gui->installEventFilter(new ButtonFeedbackFilter(gui));
        QFile theme(":/theme.qss");
        if(theme.open(QIODevice::ReadOnly)) gui->setStyleSheet(QString::fromUtf8(theme.readAll()));
        auto *i18n=new UiI18n(gui);
        auto languageSettings=uiSettings("QtServer");
        i18n->setEnglish(languageSettings.value("loginLanguage","zh-CN").toString()=="en-US");
    }
    QCommandLineParser parser; parser.addHelpOption();
    QCommandLineOption dbOption("db","SQLite 数据库路径","path");
    QCommandLineOption configOption("config","服务端 YAML 配置文件","path");
    QCommandLineOption dbTypeOption("db-type","主数据库类型 sqlite/mysql","type");
    QCommandLineOption dataDirOption("data-dir","数据和上传文件目录","path");
    QCommandLineOption portOption("port","HTTP API 端口","port");
    QCommandLineOption tcpPortOption("tcp-port","设备 TCP 端口","port","38400");
    QCommandLineOption headlessOption("headless","只运行 API 服务，不显示窗口");
    QCommandLineOption selfTestOption("self-test","运行数据库和认证自检");
    QCommandLineOption uiSelfTestOption("ui-self-test","检查主窗口及编辑表单构建");
    QCommandLineOption trialSelfTestOption("trial-self-test","检查设备绑定和时间回拨逻辑");
    QCommandLineOption customerUiOption("customer-ui-regression","在临时库逐条检查客户服务端界面问题");
    parser.addOptions({dbOption,configOption,dbTypeOption,dataDirOption,portOption,tcpPortOption,
                       headlessOption,selfTestOption,uiSelfTestOption,trialSelfTestOption,customerUiOption}); parser.process(*app);
    if(parser.isSet(customerUiOption)) return runCustomerUiRegression();
    const QString configuredPath=parser.isSet(configOption) ? parser.value(configOption) :
        qEnvironmentVariable("CONFIG_PATH").trimmed();
    const QString configPath=configuredPath.isEmpty() ? "configs/config.yaml" : configuredPath;
    QString configError;
    const QHash<QString,QString> config=QFileInfo::exists(configPath) ? readConfig(configPath,&configError) :
        configuredPath.isEmpty() ? QHash<QString,QString>{} : readConfig(configPath,&configError);
    if(!configError.isEmpty()) { qCritical("%s",qPrintable(configError)); return 2; }
    auto setting=[&config](const QString &environment,const QString &key,const QString &fallback=QString()) {
        const QString value=qEnvironmentVariable(environment.toUtf8().constData()).trimmed();
        if(!value.isEmpty()) return value;
        const QString configured=config.value(key).trimmed();
        return configured.isEmpty() ? fallback : configured;
    };
    const QString databaseType=(parser.isSet(dbTypeOption) ? parser.value(dbTypeOption) :
        setting("DB_TYPE","database.type","sqlite")).trimmed().toLower();
    if(databaseType!="sqlite" && databaseType!="mysql") { qCritical("Unsupported database type"); return 2; }
    const QString defaultDir=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString dataDir=parser.isSet(dataDirOption) ? parser.value(dataDirOption) :
        setting("BOERLAN_DATA_DIR","database.dataDir",qEnvironmentVariable("DATA_DIR",defaultDir));
    const QString path=parser.isSet(dbOption) ? parser.value(dbOption) :
        setting("DB_PATH","database.path",QDir(dataDir).filePath("boer-lan-qt.db"));
    const QString trialPath=databaseType=="mysql" ? QDir(dataDir).filePath("mysql-primary.db") : path;
    if(parser.isSet(trialSelfTestOption)) {
        QTemporaryDir directory;
        if(!directory.isValid()) return 2;
        const QString statePath=directory.filePath("trial.json");
        const QDateTime now=QDateTime::fromSecsSinceEpoch(1760000000,QTimeZone::utc());
        QString trialError;
        if(!TrialState::ensureAt(statePath,"machine-a",now,"qt-0.1.0",&trialError) ||
           !TrialState::ensureAt(statePath,"machine-a",now.addSecs(60),"qt-0.1.0",&trialError))
            return 2;
        QFile stateFile(statePath);
        if(!stateFile.open(QIODevice::ReadOnly) ||
           QJsonDocument::fromJson(stateFile.readAll()).object().value("launchCount").toInteger()!=2)
            return 2;
        stateFile.close();
        // Repeated launches must replace the existing state and preserve its origin.
        for(int launch=3;launch<=5;++launch) {
            if(!TrialState::ensureAt(statePath,"machine-a",now.addSecs(launch*60),"qt-0.1.0",&trialError)) {
                qCritical("Repeated trial launch failed: %s",qPrintable(trialError)); return 2;
            }
            if(!stateFile.open(QIODevice::ReadOnly)) return 2;
            const QJsonObject state=QJsonDocument::fromJson(stateFile.readAll()).object();
            stateFile.close();
            if(state.value("launchCount").toInteger()!=launch ||
               state.value("firstSeenAt").toInteger()!=now.toSecsSinceEpoch() ||
               state.value("lastSeenAt").toInteger()!=now.addSecs(launch*60).toSecsSinceEpoch()) return 2;
        }
        if(TrialState::ensureAt(statePath,"machine-b",now.addSecs(120),"qt-0.1.0",&trialError) ||
           TrialState::ensureAt(statePath,"machine-a",now.addSecs(-700),"qt-0.1.0",&trialError))
            return 2;
        const QString migrationPath=directory.filePath("legacy-trial.json");
        if(!TrialState::ensureAt(migrationPath,"old-host-fingerprint",now,"qt-0.1.0",&trialError) ||
           !TrialState::ensureAt(migrationPath,"stable-machine-fingerprint",now.addSecs(60),
                                 "qt-0.1.0",&trialError,{"old-host-fingerprint"})) return 2;
        QFile migrated(migrationPath);
        if(!migrated.open(QIODevice::ReadOnly)) return 2;
        const QJsonObject migratedState=QJsonDocument::fromJson(migrated.readAll()).object();
        migrated.close();
        if(migratedState.value("machineHash").toString()!="stable-machine-fingerprint" ||
           migratedState.value("firstSeenAt").toInteger()!=now.toSecsSinceEpoch() ||
           migratedState.value("launchCount").toInteger()!=2 ||
           TrialState::ensureAt(migrationPath,"different-machine",now.addSecs(120),
                                "qt-0.1.0",&trialError)) return 2;
        return 0;
    }
    if(!parser.isSet(selfTestOption) && !parser.isSet(uiSelfTestOption)) {
        setupRuntimeLog(trialPath);
        QString trialError;
        if(!TrialState::ensure(trialPath,&trialError)) {
            qCritical("Trial validation failed: %s",qPrintable(trialError));
            if(!consoleMode) UiMessageBox::warning(nullptr,"服务端启动失败",trialError);
            return 5;
        }
    }
    Database database; QString error;
    const bool databaseOpened=databaseType=="mysql" ?
        database.openMySql(setting("DB_HOST","database.host","127.0.0.1"),
            setting("DB_PORT","database.port","3306").toInt(),setting("DB_USER","database.username"),
            setting("DB_PASSWORD","database.password"),setting("DB_NAME","database.database"),
            setting("DB_CHARSET","database.charset","utf8mb4"),dataDir,&error) :
        database.open(path,&error);
    if(!databaseOpened) { qCritical("Database error: %s",qPrintable(error)); return 1; }
    if(parser.isSet(selfTestOption)) {
        QSqlQuery query(database.connection()); query.exec("SELECT password FROM users WHERE username='admin'");
        const bool ok=query.next() && Database::checkPassword("admin123",query.value(0).toString()) && database.count("groups")>=1
            && database.connection().record("production_records").contains("source_key")
            && database.connection().record("upload_tasks").contains("file_snapshot")
            && database.connection().record("salary_records").contains("source_production_id")
            && database.connection().record("device_runtime_sessions").contains("duration_seconds");
        return ok ? 0 : 2;
    }
    if(parser.isSet(uiSelfTestOption)) {
        UiI18n::instance()->setEnglish(false);
        for(const auto primitive:{QStyle::PE_IndicatorCheckBox,QStyle::PE_IndicatorItemViewItemCheck}) {
            auto paintIndicator=[&](QStyle::State state) {
                QImage image(24,24,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
                QStyleOption option; option.rect=QRect(4,4,16,16); option.state=QStyle::State_Enabled|state;
                QPainter painter(&image); static_cast<QApplication*>(app.get())->style()->drawPrimitive(primitive,&option,&painter);
                painter.end(); return image;
            };
            const QImage unchecked=paintIndicator(QStyle::State_Off), checked=paintIndicator(QStyle::State_On);
            int outlinePixels=0, bluePixels=0;
            for(int y=0;y<24;++y) for(int x=0;x<24;++x) {
                const QColor outline=unchecked.pixelColor(x,y), fill=checked.pixelColor(x,y);
                if(outline.alpha()>70 && outline.red()<230) ++outlinePixels;
                if(fill.alpha()>150 && fill.blue()>140 && fill.red()<120) ++bluePixels;
            }
            if(outlinePixels<8 || bluePixels<25) qFatal("checkbox indicator paint failed");
        }
        QSqlDatabase testConnection=database.connection();
        if(!testConnection.transaction()) qFatal("UI test transaction failed");
        QSqlQuery statusFixture(testConnection);
        statusFixture.prepare("INSERT INTO login_logs(username,ip,status,login_time) VALUES(?,?,?,?)");
        statusFixture.addBindValue("__ui_status_smoke__"); statusFixture.addBindValue("127.0.0.1");
        statusFixture.addBindValue("失败"); statusFixture.addBindValue("2026-09-26 12:00:00");
        if(!statusFixture.exec()) qFatal("UI status fixture failed");
        if(!database.insert("debug_logs",{{"level","warn"},{"source","tcp"},
            {"message","设备日志原文"}},nullptr,&error))
            qFatal("UI debug log fixture failed");
        qint64 childGroupId=0, grandchildGroupId=0;
        if(!database.insert("groups",{{"name","测试车间"},{"parentId",1}},&childGroupId,&error) ||
           !database.insert("groups",{{"name","测试班组"},{"parentId",childGroupId}},&grandchildGroupId,&error))
            qFatal("UI nested group fixture failed");
        QTemporaryDir patternFixtureDir;
        if(!patternFixtureDir.isValid()) qFatal("UI pattern temporary directory failed");
        QFile patternFixture(patternFixtureDir.filePath("pattern.dst"));
        if(!patternFixture.open(QIODevice::WriteOnly) || patternFixture.write("DST")!=3)
            qFatal("UI pattern temporary file failed");
        patternFixture.close();
        qint64 uiDeviceId=0,uiPatternId=0;
        if(!database.insert("devices",{{"code","UI-PATTERN-DEVICE"},{"name","花型测试设备"},
            {"groupId",grandchildGroupId}},&uiDeviceId,&error) ||
           !database.insert("patterns",{{"name","花型测试数据"},{"fileName","pattern.dst"},
            {"filePath",patternFixture.fileName()},{"fileSize",3},{"stitches",32}},&uiPatternId,&error) ||
           !database.insert("production_records",{{"deviceId",uiDeviceId},{"patternId",uiPatternId},
            {"patternNo",5},{"patternName","花型测试数据"},{"pieces",1},
            {"runningTime",0.5},{"recordDate",QDate::currentDate().toString("yyyy-MM-dd")+" 12:00:00"}},nullptr,&error) ||
           !database.insert("device_pattern_files",{{"deviceId",uiDeviceId},{"patternNo",5},
            {"fileName","花型测试数据.dst"},{"fileSize",3}},nullptr,&error))
            qFatal("UI pattern fixture failed");
        for(int patternNumber=0;patternNumber<21;++patternNumber)
            if(!database.insert("patterns",{{"name",QString("分页花型%1").arg(patternNumber)},
                {"fileName","pattern.dst"},{"filePath",patternFixture.fileName()},
                {"fileSize",3}},nullptr,&error))
                qFatal("UI pattern pagination fixture failed");
        DeviceServer previewDevices(&database); ApiServer previewApi(&database);
        MainWindow window(&database,&previewApi,&previewDevices); window.show();
        QTimer::singleShot(0,&window,[&] {
            const QString snapshotDir=qEnvironmentVariable("BOER_QT_UI_SNAPSHOT_DIR");
            if(!snapshotDir.isEmpty()) QDir().mkpath(snapshotDir);
            // This smoke starts with a wide layout; automatic startup screen-fit
            // is exercised separately by the window compatibility regression.
            window.resize(1600,1000);app->processEvents();
            if(auto *nav=window.findChild<QListWidget*>("navigation")) for(int i=0;i<nav->count();++i) {
                nav->setCurrentRow(i); app->processEvents();
                if(i==7) {
                    auto *table=window.findChild<QTableWidget*>("patternServerTable");
                    if(!table || table->rowCount()<1) qFatal("pattern file page missing fixture");
                    auto *pageSize=window.findChild<QComboBox*>("patternServerPageSize");
                    if(!pageSize) qFatal("pattern page size control missing");
                    pageSize->setCurrentIndex(0);
                    if(table->rowCount()!=10) qFatal("pattern page size did not apply");
                    pageSize->setCurrentIndex(1);
                    auto *deviceSelect=window.findChild<QComboBox*>("patternDeviceSelect");
                    auto *deviceFiles=window.findChild<QTableWidget*>("patternDeviceTable");
                    if(!deviceSelect || !deviceFiles) qFatal("pattern device file controls missing");
                    deviceSelect->setCurrentIndex(deviceSelect->findData(uiDeviceId));
                    if(deviceFiles->rowCount()!=1 || deviceFiles->item(0,3)->text()!="花型测试数据.dst")qFatal("offline device file cache should remain visible as read-only");
                    if(window.findChild<QPushButton*>("nativeUploadDeviceFiles")->isEnabled() || window.findChild<QPushButton*>("nativeDeleteDeviceFiles")->isEnabled())qFatal("offline cached files must not permit device operations");
                    QTimer::singleShot(50,[snapshotDir] {
                        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                        if(!dialog || !dialog->findChild<QLabel*>("dialogTitle") ||
                           dialog->findChild<QLabel*>("dialogTitle")->text()!="上传花型")
                            qFatal("pattern upload dialog missing or unstyled");
                        NumericStepper *stitches=nullptr, *price=nullptr;
                        for(auto *stepper:dialog->findChildren<NumericStepper*>()) {
                            if(stepper->decimals()==0) stitches=stepper;
                            if(stepper->decimals()==3) price=stepper;
                        }
                        if(!stitches || !price)
                            qFatal("pattern numeric controls missing");
                        stitches->setValue(12); price->setValue(0.125);
                        if(stitches->value()!=12 || price->value()!=0.125)
                            qFatal("pattern numeric controls did not update");
                        if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("dialog-pattern-upload.png"));
                        dialog->reject();
                    });
                    bool clickedUpload=false;
                    for(auto *button:window.findChildren<QPushButton*>()) if(button->isVisible() && button->text()=="上传花型") {
                        button->click(); clickedUpload=true; break;
                    }
                    if(!clickedUpload) qFatal("pattern upload action missing");
                    QTimer::singleShot(50,[] {
                        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                        if(!dialog || !dialog->findChild<QListWidget*>() ||
                           dialog->findChild<QListWidget*>()->count()<1)
                            qFatal("pattern download target dialog missing");
                        dialog->reject();
                    });
                    bool clickedDownload=false;
                    if(auto *actions=table->cellWidget(0,9)) for(auto *button:actions->findChildren<QPushButton*>())
                        if(button->text()=="下发") { button->click(); clickedDownload=true; break; }
                    if(!clickedDownload) qFatal("pattern download action missing");
                }
                if(i==1) {
                    bool verified=false;
                    for(auto *step:window.findChildren<PortStepper*>()) if(step->isVisible()) {
                    step->setValue(8088);
                    auto *input=step->findChild<QLineEdit*>("portStepperInput");
                    auto *up=step->findChild<QPushButton*>("stepButtonUp");
                    auto *down=step->findChild<QPushButton*>("stepButtonDown");
                    if(!input || input->text()!="8088" || !up || !down)
                        qFatal("port input display failed: value=%d text=%s input=%d up=%d down=%d",
                            step->value(),input ? qPrintable(input->text()) : "<missing>",input!=nullptr,up!=nullptr,down!=nullptr);
                    up->click(); down->click(); app->processEvents();
                    if(step->value()!=8088 || input->text()!="8088") qFatal("port stepper controls failed");
                    verified=true;
                    break;
                    }
                    if(!verified) qFatal("visible port input missing");
                }
                if(!snapshotDir.isEmpty()) window.grab().save(QDir(snapshotDir).filePath(QString("page-%1.png").arg(i)));
            }
            if(auto *nav=window.findChild<QListWidget*>("navigation")) nav->setCurrentRow(0);
            app->processEvents();
            if(auto *logs=window.findChild<QListWidget*>("logView")) {
                auto *level=window.findChild<QComboBox*>("debugLevelFilter");
                if(!level || logs->count()<1 || !logs->itemWidget(logs->item(0)))
                    qFatal("persisted debug log card missing");
                level->setCurrentIndex(1); app->processEvents();
                if(logs->count()!=1 || logs->itemWidget(logs->item(0)))
                    qFatal("debug log level filter did not hide warning record");
                level->setCurrentIndex(2); app->processEvents();
                if(logs->count()!=1 || !logs->itemWidget(logs->item(0)))
                    qFatal("debug log level filter did not restore warning record");
                level->setCurrentIndex(0); app->processEvents();
            } else qFatal("persisted debug log list missing");
            for(auto *button:window.findChildren<QPushButton*>()) if(button->isVisible() && button->text()=="刷新") {
                const QPointF center=button->rect().center();
                const QPointF global=button->mapToGlobal(center.toPoint());
                QMouseEvent press(QEvent::MouseButtonPress,center,center,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease,center,center,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(button,&press); QApplication::sendEvent(button,&release); app->processEvents();
                auto *toast=window.findChild<QLabel*>("actionToast");
                if(!toast || !toast->isVisible() || !button->property("clickFlash").toBool())
                    qFatal("button feedback missing");
                if(!snapshotDir.isEmpty()) window.grab().save(QDir(snapshotDir).filePath("button-feedback.png"));
                break;
            }
            auto verifyFilterFlow=[&window](bool compact) {
                QWidget *keyword=nullptr, *date=nullptr;
                for(auto *widget:window.findChildren<QWidget*>("filterKeywordGroup")) if(widget->isVisible()) { keyword=widget; break; }
                for(auto *widget:window.findChildren<QWidget*>("filterDateGroup")) if(widget->isVisible()) { date=widget; break; }
                if(!keyword || !date || keyword->parentWidget()!=date->parentWidget() || (date->y()>keyword->y())!=compact)
                    qFatal("responsive filter rows did not match available width");
            };
            if(auto *nav=window.findChild<QListWidget*>("navigation")) for(int page:{4,5}) {
                nav->setCurrentRow(page); app->processEvents(); verifyFilterFlow(false);
            }
            window.resize(1120,700); app->processEvents();
            if(auto *nav=window.findChild<QListWidget*>("navigation")) for(int page:{4,5}) {
                nav->setCurrentRow(page); app->processEvents(); verifyFilterFlow(true);
            }
            window.resize(1600,1000); app->processEvents();
            if(auto *nav=window.findChild<QListWidget*>("navigation")) for(int page:{4,5}) {
                nav->setCurrentRow(page); app->processEvents(); verifyFilterFlow(false);
            }
            if(!snapshotDir.isEmpty()) {
                window.resize(1120,700); app->processEvents();
                if(auto *nav=window.findChild<QListWidget*>("navigation")) for(int i:{3,4}) {
                    nav->setCurrentRow(i); app->processEvents();
                    window.grab().save(QDir(snapshotDir).filePath(QString("page-%1-small.png").arg(i)));
                }
                window.resize(1600,1000); app->processEvents();
                if(auto *nav=window.findChild<QListWidget*>("navigation")) nav->setCurrentRow(3);
                app->processEvents();
                for(auto *widget:window.findChildren<QWidget*>()) if(auto *range=dynamic_cast<DateRangeWidget*>(widget); range && range->isVisible()) {
                    auto buttons=range->findChildren<QPushButton*>();
                    if(!buttons.isEmpty()) { buttons.first()->click(); app->processEvents();
                        if(auto *popup=QApplication::activePopupWidget()) {
                            popup->grab().save(QDir(snapshotDir).filePath("date-popup.png"));
                            if(auto *calendar=popup->findChild<QCalendarWidget*>()) {
                                const QDate chosen(2026,9,15);
                                if(!QMetaObject::invokeMethod(calendar,"clicked",Qt::DirectConnection,Q_ARG(QDate,chosen)) || range->startDate()!=chosen)
                                    qFatal("date range selection failed");
                            } else popup->close();
                        }
                    }
                    break;
                }
                if(auto *nav=window.findChild<QListWidget*>("navigation")) nav->setCurrentRow(4);
                app->processEvents();
                for(auto *widget:window.findChildren<QWidget*>()) if(auto *combo=dynamic_cast<UiComboBox*>(widget); combo && combo->isVisible() && combo->parentWidget() && combo->parentWidget()->objectName()=="filterPanel") {
                    static_cast<QComboBox*>(combo)->showPopup(); app->processEvents();
                    if(auto *popup=combo->view()->window()) {
                        if(combo->view()->sizeHintForRow(0)<combo->height() ||
                           (combo->count()>1 && combo->view()->visualRect(combo->model()->index(1,0)).top()!=combo->view()->visualRect(combo->model()->index(0,0)).bottom()+1))
                            qFatal("filter combo option height or layout failed");
                        popup->grab().save(QDir(snapshotDir).filePath("combo-popup.png")); popup->hide();
                    }
                    break;
                }
            }
            if(auto *nav=window.findChild<QListWidget*>("navigation")) nav->setCurrentRow(4);
            app->processEvents();
            for(auto *table:window.findChildren<QTableWidget*>()) if(table->isVisible() && table->rowCount()>0) {
                auto *holder=table->cellWidget(0,0);
                auto *check=holder ? holder->findChild<QCheckBox*>() : nullptr;
                if(check) {
                    for(int i=0;i<12;++i) {
                        check->click(); app->processEvents();
                        if(check->isChecked()!=(i%2==0)) qFatal("table checkbox toggle failed");
                    }
                    table->setCurrentCell(0,2); app->processEvents();
                    if(table->selectionModel()->selectedRows().size()!=1) qFatal("table row selection failed");
                    if(!snapshotDir.isEmpty()) window.grab().save(QDir(snapshotDir).filePath("table-row-selected.png"));
                    break;
                }
            }
            for(const QString &table:{"roles","users","devices"}) {
                QTimer::singleShot(50,[snapshotDir,table] {
                    if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                        if(table=="users" || table=="devices") for(auto *widget:dialog->findChildren<QWidget*>()) if(auto *combo=dynamic_cast<UiComboBox*>(widget); combo && combo->isVisible() && combo->count()>0) {
                            static_cast<QComboBox*>(combo)->showPopup(); QApplication::processEvents();
                            if(combo->view()->sizeHintForRow(0)<combo->height()) qFatal("editor combo option too short");
                            if(!snapshotDir.isEmpty()) combo->view()->window()->grab().save(QDir(snapshotDir).filePath("dialog-"+table+"-combo-popup.png"));
                            combo->hidePopup(); break;
                        }
                        if(table=="roles") {
                            auto *outer=dialog->findChild<QScrollArea*>("dialogScroll");
                            auto *permissions=dialog->findChild<QTreeWidget*>();
                            QCheckBox *detail=nullptr;
                            for(auto *check:dialog->findChildren<QCheckBox*>()) if(check->text()=="展开细分权限") { detail=check; break; }
                            if(!detail || !permissions) qFatal("role permission controls missing");
                            const int checkboxFontHeight=detail->fontMetrics().height();
                            const int treeFontHeight=permissions->fontMetrics().height();
                            const QPoint checkboxPosition=detail->mapTo(dialog,QPoint(0,0));
                            const QPoint treePosition=permissions->mapTo(dialog,QPoint(0,0));
                            if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("dialog-roles-before.png"));
                            detail->click();
                            QApplication::processEvents();
                            if(outer || permissions->height()!=190 || detail->fontMetrics().height()!=checkboxFontHeight ||
                               permissions->fontMetrics().height()!=treeFontHeight ||
                               detail->mapTo(dialog,QPoint(0,0))!=checkboxPosition ||
                               permissions->mapTo(dialog,QPoint(0,0))!=treePosition)
                                qFatal("role permission expansion layout failed");
                            if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("dialog-roles-expanded.png"));
                        }
                        if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("dialog-"+table+".png"));
                        dialog->reject();
                    }
                });
                showEditorDialog(&database,table,{},true,&window);
            }
            const QJsonArray roleRows=database.rows("roles");
            if(!roleRows.isEmpty()) {
                QTimer::singleShot(50,[snapshotDir] {
                    if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                        QCheckBox *detail=nullptr;
                        for(auto *check:dialog->findChildren<QCheckBox*>()) if(check->text()=="展开细分权限") { detail=check; break; }
                        auto *permissions=dialog->findChild<QTreeWidget*>();
                        if(!detail || !permissions) qFatal("edit role permission controls missing");
                        const QFont checkboxFont=detail->font(), treeFont=permissions->font();
                        const QPoint checkboxPosition=detail->mapTo(dialog,QPoint(0,0));
                        const QPoint treePosition=permissions->mapTo(dialog,QPoint(0,0));
                        auto indicatorSize=[detail] {
                            QStyleOptionButton option; option.initFrom(detail);
                            option.state &= ~(QStyle::State_On|QStyle::State_Off);
                            option.state |= detail->isChecked() ? QStyle::State_On : QStyle::State_Off;
                            return detail->style()->subElementRect(QStyle::SE_CheckBoxIndicator,&option,detail).size();
                        };
                        const QSize checkboxSize=indicatorSize();
                        if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("dialog-role-edit-before.png"));
                        detail->click(); QApplication::processEvents();
                        if(detail->font()!=checkboxFont || permissions->font()!=treeFont || indicatorSize()!=checkboxSize ||
                           detail->mapTo(dialog,QPoint(0,0))!=checkboxPosition ||
                           permissions->mapTo(dialog,QPoint(0,0))!=treePosition)
                            qFatal("edit role font or layout changed on expansion");
                        if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("dialog-role-edit-expanded.png"));
                        auto *home=permissions->topLevelItem(0);
                        const QRect row=permissions->visualItemRect(home);
                        const QPointF local(row.left()+64,row.center().y());
                        if(permissions->itemAt(local.toPoint())!=home) qFatal("permission name hit target missing");
                        const QPointF global=permissions->viewport()->mapToGlobal(local.toPoint());
                        auto clickPermissionName=[&] {
                            QMouseEvent press(QEvent::MouseButtonPress,local,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                            QMouseEvent release(QEvent::MouseButtonRelease,local,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                            QApplication::sendEvent(permissions->viewport(),&press);
                            QApplication::sendEvent(permissions->viewport(),&release);
                            QApplication::processEvents();
                        };
                        const Qt::CheckState initial=home->checkState(0);
                        clickPermissionName();
                        if(home->checkState(0)==initial || !permissions->selectedItems().isEmpty() || permissions->hasFocus())
                            qFatal("permission name did not toggle cleanly");
                        if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("dialog-role-edit-name-click.png"));
                        clickPermissionName();
                        if(home->checkState(0)!=initial) qFatal("permission name second click did not restore state");
                        dialog->reject();
                    }
                });
                showEditorDialog(&database,"roles",roleRows.first().toObject(),false,&window);
            }
            const QJsonArray adminRows=database.rows("users","role=?",{"admin"});
            if(!adminRows.isEmpty()) {
                QTimer::singleShot(50,[snapshotDir] {
                    if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                        auto *groups=dialog->findChild<QTreeWidget*>("visibleGroups");
                        if(!groups || !groups->isVisible() || groups->topLevelItemCount()<1) qFatal("visible group tree missing");
                        auto *root=groups->topLevelItem(0);
                        if(root->childCount()<1 || root->child(0)->childCount()<1) qFatal("visible group hierarchy missing");
                        auto *group=root->child(0)->child(0);
                        const QRect row=groups->visualItemRect(group);
                        const QPointF local(row.left()+105,row.center().y());
                        if(groups->itemAt(local.toPoint())!=group) qFatal("visible group name hit target missing");
                        const QPointF global=groups->viewport()->mapToGlobal(local.toPoint());
                        auto clickGroupName=[&] {
                            QMouseEvent press(QEvent::MouseButtonPress,local,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                            QMouseEvent release(QEvent::MouseButtonRelease,local,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                            QApplication::sendEvent(groups->viewport(),&press);
                            QApplication::sendEvent(groups->viewport(),&release);
                            QApplication::processEvents();
                        };
                        const Qt::CheckState initial=group->checkState(0);
                        if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("dialog-user-groups-before.png"));
                        clickGroupName();
                        if(group->checkState(0)==initial || !groups->selectedItems().isEmpty() || groups->hasFocus())
                            qFatal("visible group name did not toggle cleanly");
                        if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("dialog-user-groups-after.png"));
                        clickGroupName();
                        if(group->checkState(0)!=initial) qFatal("visible group second click did not restore state");
                        dialog->reject();
                    }
                });
                showEditorDialog(&database,"users",adminRows.first().toObject(),false,&window);
            }
            if(database.count("devices")>0) {
                const QJsonObject device=database.rows("devices").first().toObject();
                QTimer::singleShot(50,[snapshotDir] {
                    if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                        if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("dialog-device-edit.png"));
                        dialog->reject();
                    }
                });
                showEditorDialog(&database,"devices",device,false,&window);
            }
            if(!snapshotDir.isEmpty()) {
                QMenu menu(&window); menu.setObjectName("groupContextMenu"); menu.setMinimumWidth(170);
                menu.addAction("新增子分组"); menu.addAction("重命名"); menu.addAction("删除分组");
                menu.popup(window.mapToGlobal(QPoint(340,220))); app->processEvents();
                menu.grab().save(QDir(snapshotDir).filePath("group-context-menu.png")); menu.close();
            }
            QTimer::singleShot(50,[snapshotDir] {
                auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if(!dialog || dialog->objectName()!="editorDialog") qFatal("styled group prompt missing");
                auto *input=dialog->findChild<QLineEdit*>("promptInput");
                auto *invalid=dialog->findChild<QLabel*>("promptError");
                auto *confirm=dialog->findChild<QPushButton*>("primaryButton");
                if(!input || !invalid || !confirm) qFatal("styled group prompt fields missing");
                if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("group-prompt.png"));
                confirm->click(); QApplication::processEvents();
                if(!invalid->isVisible()) qFatal("empty group name validation missing");
                input->setText("测试子分组"); confirm->click();
            });
            const auto prompted=UiMessageBox::prompt(&window,"新增子分组","请输入分组名称",{},50);
            if(!prompted || *prompted!="测试子分组") qFatal("styled group prompt result failed");
            QTimer::singleShot(50,[snapshotDir] {
                if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                    if(!(dialog->windowFlags() & Qt::FramelessWindowHint) || !dialog->findChild<QTextEdit*>("dialogOutput"))
                        qFatal("diagnostic output dialog uses native default chrome");
                    if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("diagnostic-output-dialog.png"));
                    dialog->reject();
                }
            });
            UiMessageBox::output(&window,"诊断结果","Sample diagnostic output\nSecond line");
            auto testLanguageSettings=uiSettings("QtServer");
            const QVariant savedLanguage=testLanguageSettings.value("loginLanguage");
            auto *englishButton=window.findChild<QPushButton*>("mainLanguageOption");
            for(auto *button:window.findChildren<QPushButton*>("mainLanguageOption")) if(button->text()=="EN") englishButton=button;
            if(!englishButton) qFatal("main language switch missing");
            englishButton->click(); app->processEvents();
            if(!UiI18n::instance()->english() || testLanguageSettings.value("loginLanguage").toString()!="en-US") qFatal("main language switch failed");
            QSet<QString> untranslated;
            const QRegularExpression chinese("[\\x{4e00}-\\x{9fff}]");
            auto checkText=[&](const QString &value){ if(value.contains(chinese)) untranslated.insert(value); };
            if(auto *nav=window.findChild<QListWidget*>("navigation")) for(int page=0;page<nav->count();++page) {
                nav->setCurrentRow(page); app->processEvents();
                if(page==7) {
                    auto *table=window.findChild<QTableWidget*>("patternServerTable");
                    auto *next=window.findChild<QPushButton*>("patternServerNext");
                    auto *previous=window.findChild<QPushButton*>("patternServerPrevious");
                    if(!table || table->rowCount()!=20 || !next || !next->isEnabled() || !previous)
                        qFatal("native pattern file pagination first page failed");
                    next->click(); app->processEvents();
                    if(table->rowCount()!=2 || next->isEnabled()) qFatal("native pattern file pagination second page failed");
                    previous->click(); app->processEvents();
                }
                if(page==8) {
                    auto *summary=window.findChild<QTableWidget*>("patternStatsSummary");
                    auto *details=window.findChild<QTableWidget*>("patternStatsDetails");
                    if(!summary || !details || summary->rowCount()<1 || details->rowCount()<1)
                        qFatal("native pattern statistics page did not render both tables");
                }
                if(page==4 || page==5) verifyFilterFlow(false);
                if(page==3) for(auto *table:window.findChildren<QTableWidget*>()) if(table->isVisible() && table->rowCount()>0 && table->columnCount()==7) {
                    if(table->item(0,6)->text()!="普通用户") qFatal("role remark was translated as interface text");
                    break;
                }
                if(page==4) for(auto *table:window.findChildren<QTableWidget*>()) if(table->isVisible() && table->rowCount()>0 && table->columnCount()==11) {
                    if(table->item(0,3)->text()!="管理员") qFatal("user-entered account name was translated");
                    break;
                }
                if(page==6) {
                    bool verified=false;
                    for(auto *pill:window.findChildren<QLabel*>("statusPillDanger")) if(pill->isVisible()) {
                        const int needed=pill->fontMetrics().horizontalAdvance(pill->text())+20;
                        if(pill->width()<needed) qFatal("translated login status pill clipped");
                        verified=true; break;
                    }
                    if(!verified) qFatal("login failure status pill missing");
                }
                if(!snapshotDir.isEmpty()) window.grab().save(QDir(snapshotDir).filePath(QString("page-%1-en.png").arg(page)));
                for(auto *widget:window.findChildren<QWidget*>()) if(widget->isVisibleTo(&window) && !widget->property("i18nSkip").toBool()) {
                    if(auto *label=qobject_cast<QLabel*>(widget)) checkText(label->text());
                    if(auto *button=qobject_cast<QPushButton*>(widget)) checkText(button->text());
                    if(auto *line=qobject_cast<QLineEdit*>(widget)) checkText(line->placeholderText());
                    if(auto *combo=qobject_cast<QComboBox*>(widget)) for(int i=0;i<combo->count();++i) if(!combo->itemData(i,UiI18n::PreserveDataRole).toBool()) checkText(combo->itemText(i));
                    if(auto *list=qobject_cast<QListWidget*>(widget)) for(int i=0;i<list->count();++i) if(!list->item(i)->data(UiI18n::PreserveDataRole).toBool()) checkText(list->item(i)->text());
                    if(auto *tree=qobject_cast<QTreeWidget*>(widget)) for(QTreeWidgetItemIterator it(tree);*it;++it) if(!(*it)->data(0,UiI18n::PreserveDataRole).toBool()) checkText((*it)->text(0));
                    if(auto *table=qobject_cast<QTableWidget*>(widget)) {
                        for(int col=0;col<table->columnCount();++col) {
                            if(auto *header=table->horizontalHeaderItem(col)) checkText(header->text());
                            for(int row=0;row<table->rowCount();++row) if(auto *item=table->item(row,col); item && !item->data(UiI18n::PreserveDataRole).toBool()) checkText(item->text());
                        }
                    }
                }
            }
            for(const QString &table:{"roles","users","devices"}) {
                QTimer::singleShot(50,[&,table] {
                    if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                        if(table=="roles") for(auto *check:dialog->findChildren<QCheckBox*>()) if(check->text()=="Expand Detailed Permissions") {
                            check->click(); QApplication::processEvents();
                            if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("dialog-roles-expanded-en.png"));
                            break;
                        }
                        if(table=="devices") for(auto *widget:dialog->findChildren<QWidget*>()) if(auto *combo=dynamic_cast<UiComboBox*>(widget)) {
                            const int at=combo->findText("模板机");
                            if(at>=0) { combo->setCurrentIndex(at); if(UiI18n::instance()->sourceText(combo)!="模板机") qFatal("catalog value changed in English mode"); break; }
                        }
                        if(table=="users") {
                            auto tags=dialog->findChildren<QLabel*>("permissionTag");
                            if(tags.size()>1) {
                                const QPoint first=tags[0]->mapTo(dialog,QPoint(0,0));
                                const QPoint second=tags[1]->mapTo(dialog,QPoint(0,0));
                                if(first.y()==second.y() && second.x()!=first.x()+tags[0]->width()+6)
                                    qFatal("role permission tags are not packed consecutively");
                            }
                        }
                        for(auto *widget:dialog->findChildren<QWidget*>()) if(widget->isVisibleTo(dialog) && !widget->property("i18nSkip").toBool()) {
                            if(auto *label=qobject_cast<QLabel*>(widget)) checkText(label->text());
                            if(auto *button=qobject_cast<QAbstractButton*>(widget)) checkText(button->text());
                            if(auto *line=qobject_cast<QLineEdit*>(widget)) checkText(line->placeholderText());
                            if(auto *combo=qobject_cast<QComboBox*>(widget)) for(int i=0;i<combo->count();++i) if(!combo->itemData(i,UiI18n::PreserveDataRole).toBool()) checkText(combo->itemText(i));
                            if(auto *list=qobject_cast<QListWidget*>(widget)) for(int i=0;i<list->count();++i) if(!list->item(i)->data(UiI18n::PreserveDataRole).toBool()) checkText(list->item(i)->text());
                            if(auto *tree=qobject_cast<QTreeWidget*>(widget)) for(QTreeWidgetItemIterator it(tree);*it;++it) if(!(*it)->data(0,UiI18n::PreserveDataRole).toBool()) checkText((*it)->text(0));
                        }
                        if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("dialog-"+table+"-en.png"));
                        dialog->reject();
                    }
                });
                showEditorDialog(&database,table,{},true,&window);
            }
            QTimer::singleShot(50,[snapshotDir] {
                if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                    auto *title=dialog->findChild<QLabel*>("dialogTitle");
                    if(!title || title->text()!="Add Child Group") qFatal("English group prompt title untranslated");
                    if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("group-prompt-en.png"));
                    dialog->reject();
                }
            });
            UiMessageBox::prompt(&window,"新增子分组","请输入分组名称",{},50);
            if(!roleRows.isEmpty()) {
                const QJsonObject role=roleRows.first().toObject();
                QTimer::singleShot(50,[snapshotDir,role,chinese] {
                    if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                        auto *remark=dialog->findChild<QPlainTextEdit*>();
                        if(!remark || remark->toPlainText()!=role.value("remark").toString()) qFatal("role remark changed with language");
                        for(auto *check:dialog->findChildren<QCheckBox*>()) if(check->text()=="Expand Detailed Permissions") { check->click(); break; }
                        QApplication::processEvents();
                        if(auto *tree=dialog->findChild<QTreeWidget*>()) for(QTreeWidgetItemIterator it(tree);*it;++it)
                            if((*it)->text(0).contains(chinese)) qFatal("role submenu permission untranslated");
                        if(!snapshotDir.isEmpty()) dialog->grab().save(QDir(snapshotDir).filePath("dialog-role-edit-expanded-en.png"));
                        dialog->reject();
                    }
                });
                showEditorDialog(&database,"roles",role,false,&window);
            }
            for(const QString &value:untranslated) qInfo("untranslated Qt UI: %s",qPrintable(value));
            if(!untranslated.isEmpty()) qFatal("visible Qt UI contains untranslated Chinese labels");
            if(!snapshotDir.isEmpty()) {
                window.resize(1120,700); app->processEvents();
                if(auto *nav=window.findChild<QListWidget*>("navigation")) for(int page:{1,3,4,5}) {
                    nav->setCurrentRow(page); app->processEvents();
                    if(page==4 || page==5) verifyFilterFlow(true);
                    window.grab().save(QDir(snapshotDir).filePath(QString("page-%1-en-small.png").arg(page)));
                }
            }
            UiI18n::instance()->setEnglish(false);
            if(auto *nav=window.findChild<QListWidget*>("navigation")) {
                nav->setCurrentRow(0); app->processEvents();
                if(nav->item(0)->text()!="主界面") qFatal("switching back to Chinese failed");
            }
            if(savedLanguage.isValid()) testLanguageSettings.setValue("loginLanguage",savedLanguage);
            else testLanguageSettings.remove("loginLanguage");
            testConnection.rollback();
            app->quit();
        });
        return app->exec();
    }
    const QString yamlPort=config.value("server.port").trimmed();
    const QString requestedPort=parser.isSet(portOption) ? parser.value(portOption) :
        database.config("server_port",yamlPort.isEmpty() ? "8088" : yamlPort);
    bool portOk=false; const int port=requestedPort.toInt(&portOk);
    if(!portOk || port<1 || port>65535) { qCritical("Invalid port"); return 2; }
    bool tcpPortOk=false; const int tcpPort=parser.value(tcpPortOption).toInt(&tcpPortOk);
    if(!tcpPortOk || tcpPort<0 || tcpPort>65535) { qCritical("Invalid TCP port"); return 2; }
    DeviceServer deviceServer(&database);
    QObject::connect(&deviceServer,&DeviceServer::eventLogged,&deviceServer,[](const QString &line){ qInfo("%s",qPrintable(line)); });
    if(!deviceServer.listen(tcpPort,&error)) { qCritical("Device listen error: %s",qPrintable(error)); return 4; }
    ApiServer server(&database);
    bool jwtHoursValid=false;
    const int jwtHours=setting("JWT_EXPIRE","jwt.expire","24").toInt(&jwtHoursValid);
    if(!jwtHoursValid || jwtHours<1) { qCritical("Invalid JWT expiry"); return 2; }
    server.setJwtSettings(setting("JWT_SECRET","jwt.secret"),jwtHours);
    const bool logHttp=envEnabled("HTTP_ACCESS_LOG",!envEnabled("QUIET_MODE",true));
    QObject::connect(&server,&ApiServer::requestLogged,&server,[logHttp](const QString &line) {
        if(logHttp || line.startsWith("外部数据库")) qInfo("%s",qPrintable(line));
    });
    server.setDeviceServer(&deviceServer);
    QObject::connect(&server,&ApiServer::deviceDeleted,&deviceServer,&DeviceServer::disconnectDevice);
    server.setDevicePort(deviceServer.port());
    if(!server.listen(port,&error)) { qCritical("Listen error: %s",qPrintable(error)); return 3; }
    const QString portFile=qEnvironmentVariable("PORT_FILE").trimmed().isEmpty() ?
        QDir(database.dataDirectory()).filePath("backend-port.txt") : qEnvironmentVariable("PORT_FILE").trimmed();
    if(QDir().mkpath(QFileInfo(portFile).absolutePath())) {
        QSaveFile output(portFile);
        if(output.open(QIODevice::WriteOnly) && output.write(QByteArray::number(server.port()))>0)
            output.commit();
    }
    if(parser.isSet(headlessOption)) { qInfo("Qt server listening on port %d",port); return app->exec(); }
    startOnlineUpdate("server");
    QDialog login; login.setObjectName("loginDialog"); login.setWindowTitle("博尔物联网管理软件服务端"); login.resize(1100,650); login.setMinimumSize(900,540);
    auto loginSettings=uiSettings("QtServer");
    QString currentLanguage=loginSettings.value("loginLanguage","zh-CN").toString();
    if(currentLanguage!="en-US") currentLanguage="zh-CN";
    auto *outer=new QVBoxLayout(&login); outer->setContentsMargins(24,14,44,14); outer->setSpacing(0);
    auto *header=new QHBoxLayout; header->setObjectName("windowChromeLayout"); auto *bohr=new QLabel("Bohr"); bohr->setObjectName("loginBohr"); header->addWidget(bohr);
    auto *brand=new QLabel("博尔物联网管理软件"); brand->setObjectName("loginBrand"); header->addWidget(brand); header->addStretch();
    auto *language=new QPushButton; language->setObjectName("languageSwitch"); language->setCursor(Qt::PointingHandCursor); language->setFixedSize(116,30);
    auto *languageLayout=new QHBoxLayout(language); languageLayout->setContentsMargins(8,0,8,0); languageLayout->setSpacing(6);
    auto *languageText=new QLabel; languageText->setObjectName("languageSwitchText"); languageText->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *languageIcon=new QLabel; languageIcon->setObjectName("languageSwitchIcon"); languageIcon->setPixmap(QIcon(":/language-switch.svg").pixmap(QSize(18,18),2.0));
    languageIcon->setFixedSize(18,18); languageIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
    languageLayout->addWidget(languageText,1); languageLayout->addWidget(languageIcon); header->addWidget(language); outer->addLayout(header);
    outer->addStretch(1);
    auto *stage=new QHBoxLayout; stage->addStretch(1);
    auto *card=new QFrame; card->setObjectName("loginCard"); card->setFixedWidth(320);
    auto *shadow=new QGraphicsDropShadowEffect(card); shadow->setBlurRadius(28); shadow->setOffset(0,10); shadow->setColor(QColor(44,91,154,65)); card->setGraphicsEffect(shadow);
    auto *cardLayout=new QVBoxLayout(card); cardLayout->setContentsMargins(34,22,34,28); cardLayout->setSpacing(16);
    auto *loginTitle=new QLabel("博尔物联网管理软件服务端"); loginTitle->setObjectName("loginTitle"); loginTitle->setAlignment(Qt::AlignCenter); cardLayout->addWidget(loginTitle);
    const QString rememberedUsername=loginSettings.value("rememberedUsername").toString();
    auto *username=new QLineEdit(rememberedUsername.isEmpty() ? "admin" : rememberedUsername);
    username->setPlaceholderText("请输入账号"); username->setClearButtonEnabled(true);
    cardLayout->addWidget(username);
    auto *password=new QLineEdit; password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText("请输入密码"); password->setClearButtonEnabled(true);
    auto *passwordVisibility=password->addAction(passwordVisibilityIcon(false),QLineEdit::TrailingPosition);
    passwordVisibility->setCheckable(true);
    QObject::connect(passwordVisibility,&QAction::toggled,password,[=](bool visible) {
        password->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        passwordVisibility->setIcon(passwordVisibilityIcon(visible));
    });
    cardLayout->addWidget(password);
    auto *remember=new QCheckBox("记住账号"); remember->setChecked(!rememberedUsername.isEmpty()); remember->setStyleSheet("color: #7d8ea2; font-size: 12px;"); cardLayout->addWidget(remember,0,Qt::AlignRight);
    auto *loginButton=new QPushButton("登  录"); loginButton->setObjectName("loginButton"); cardLayout->addWidget(loginButton);
    stage->addWidget(card); outer->addLayout(stage); outer->addStretch(1);
    auto *footer=new QLabel("苏州博尔科技有限公司\nResearch Suzhou Bohr Technology Co., Ltd."); footer->setObjectName("loginFooter"); footer->setAlignment(Qt::AlignCenter); outer->addWidget(footer);
    auto *loginErrorToast=new QLabel(&login); loginErrorToast->setObjectName("loginErrorToast");
    loginErrorToast->setAttribute(Qt::WA_TransparentForMouseEvents); loginErrorToast->setAlignment(Qt::AlignCenter);
    loginErrorToast->setFixedHeight(38); loginErrorToast->hide();
    auto *errorShadow=new QGraphicsDropShadowEffect(loginErrorToast); errorShadow->setBlurRadius(16);
    errorShadow->setOffset(0,3); errorShadow->setColor(QColor(90,50,50,40)); loginErrorToast->setGraphicsEffect(errorShadow);
    auto *errorTimer=new QTimer(&login); errorTimer->setSingleShot(true);
    QObject::connect(errorTimer,&QTimer::timeout,loginErrorToast,&QWidget::hide);
    auto showLoginError=[&] {
        loginErrorToast->setText(currentLanguage=="en-US" ? "Administrator username or password is incorrect" : "管理员账号或密码错误");
        loginErrorToast->show();UiWindowGeometry::fitNotification(loginErrorToast,&login,60);loginErrorToast->raise();errorTimer->start(2500);
    };
    auto applyLanguage=[&] {
        const bool english=currentLanguage=="en-US";
        login.setWindowTitle(english ? "Bohr IoT Management Server" : "博尔物联网管理软件服务端");
        brand->setProperty("i18nSkip",true);
        brand->setText(QString(english ? "Bohr IoT Management Server" : "博尔物联网管理软件服务端")+"  v"+QCoreApplication::applicationVersion());
        loginTitle->setText(english ? "Bohr IoT Management Server" : "博尔物联网管理软件服务端");
        languageText->setText(english ? "English" : "简体中文");
        language->setAccessibleName(english ? "Switch to Chinese" : "切换到英语");
        username->setPlaceholderText(english ? "Enter Username" : "请输入账号");
        password->setPlaceholderText(english ? "Enter Password" : "请输入密码");
        remember->setText(english ? "Remember Username" : "记住账号");
        loginButton->setText(english ? "Login" : "登  录");
        footer->setText(english ? "Bohr IoT Management Server\nResearch Suzhou Bohr Technology Co., Ltd." : "苏州博尔科技有限公司\nResearch Suzhou Bohr Technology Co., Ltd.");
    };
    applyLanguage();
    QObject::connect(language,&QPushButton::clicked,&login,[&] {
        loginErrorToast->hide(); errorTimer->stop();
        currentLanguage=currentLanguage=="zh-CN" ? "en-US" : "zh-CN";
        loginSettings.setValue("loginLanguage",currentLanguage);
        UiI18n::instance()->setEnglish(currentLanguage=="en-US");
        applyLanguage();
    });
    bool loggedIn=false;
    auto attemptLogin=[&] {
        QSqlQuery query(database.connection()); query.prepare("SELECT password,disabled,role FROM users WHERE username=? AND deleted_at IS NULL"); query.addBindValue(username->text().trimmed());
        if(query.exec() && query.next() && !query.value(1).toBool() && query.value(2).toString()=="admin" && Database::checkPassword(password->text(),query.value(0).toString())) {
            loginSettings.setValue("rememberedUsername",remember->isChecked() ? username->text().trimmed() : QString());
            loggedIn=true; password->clear(); errorTimer->stop(); loginErrorToast->hide(); login.accept();
        } else {
            password->clear(); password->setFocus(); showLoginError();
        }
    };
    QObject::connect(loginButton,&QPushButton::clicked,&login,attemptLogin);
    QObject::connect(password,&QLineEdit::returnPressed,&login,attemptLogin);
    QObject::connect(username,&QLineEdit::returnPressed,&login,attemptLogin);
    UiWindowGeometry::attach(&login);
    int loginCompatibilityFailures=0;
    if(qEnvironmentVariableIsSet("BOER_TEST_LOGIN_WINDOW_COMPAT"))QTimer::singleShot(100,&login,[&]{
        auto check=[&](const QString &name,bool passed){recordWindowCompatibility("server-login",name,passed);loginCompatibilityFailures+=!passed;};
        for(bool english:{false,true}){
            if((currentLanguage=="en-US")!=english)language->click();waitWindowCompatibilityLayout();
            check(english ? "en 登录窗位于可用屏幕" : "zh 登录窗位于可用屏幕",login.screen()->availableGeometry().contains(login.frameGeometry()));
            bool accessible=true;QStringList loginIssues;for(QWidget *control:QList<QWidget*>{username,password,loginButton,language,remember}){for(int pass=0;pass<3;++pass){for(auto *scroll:login.findChildren<QScrollArea*>())if(scroll->isAncestorOf(control)){const QPoint center=control->mapTo(scroll->widget(),control->rect().center());scroll->ensureVisible(center.x(),center.y(),control->width()/2+8,control->height()/2+8);}QApplication::processEvents();}const QRect region=control->visibleRegion().boundingRect();const bool fits=region.contains(control->rect());if(!fits)loginIssues<<QString("%1 rect=%2x%3 visible=%4,%5 %6x%7").arg(control->metaObject()->className()).arg(control->width()).arg(control->height()).arg(region.x()).arg(region.y()).arg(region.width()).arg(region.height());accessible&=fits;}
            recordWindowCompatibility("server-login",english ? "en 登录输入与按钮可访问" : "zh 登录输入与按钮可访问",accessible,loginIssues.join("; "));loginCompatibilityFailures+=!accessible;
            QDir().mkpath(qEnvironmentVariable("BOER_WINDOW_ARTIFACT_DIR"));login.grab().save(qEnvironmentVariable("BOER_WINDOW_ARTIFACT_DIR")+(english ? "/server-login-en.png" : "/server-login-zh.png"));
        }
        login.reject();
    });
    const QString reopenStage=qEnvironmentVariable("BOER_TEST_LOGIN_REOPEN");
    auto recordReopen=[&](int row,const QString &name,bool passed){QFile file(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG"));if(!file.open(QIODevice::Append))qFatal("Reopen test log unavailable");file.write(QJsonDocument(QJsonObject{{"row",row},{"name",name},{"result",passed ? "PASS" : "FAIL"},{"scope","实际启动Qt服务端GUI进程，临时数据库与独立QSettings；成功登录后关闭，再用同一设置重新启动"}}).toJson(QJsonDocument::Compact)+"\n");file.flush();if(!passed)qFatal("Server reopen check failed: %s",qPrintable(name));};
    if(!reopenStage.isEmpty())QTimer::singleShot(0,&login,[&]{
        recordReopen(352,"服务端"+reopenStage+"进程启动时显示登录页且不缓存密码",login.isVisible() && password->text().isEmpty() && !loggedIn);
        if(reopenStage=="first") {
            if(currentLanguage!="en-US")language->click();QApplication::processEvents();
            recordReopen(354,"英文服务端登录标题与输入提示完整显示",loginTitle->width()>=loginTitle->fontMetrics().horizontalAdvance(loginTitle->text()) && username->placeholderText()=="Enter Username" && password->placeholderText()=="Enter Password");
            const QString artifactDir=qEnvironmentVariable("BOER_TEST_LOGIN_ARTIFACT_DIR");if(!artifactDir.isEmpty())login.grab().save(QDir(artifactDir).filePath("server-reopen-login-en.png"));
            remember->setChecked(true);username->setText("admin");password->setText("admin123");loginButton->click();recordReopen(352,"服务端首次真实认证成功并清空密码",loggedIn && password->text().isEmpty());
            QTimer::singleShot(150,app.get(),[&]{MainWindow *main=nullptr;for(auto *widget:QApplication::topLevelWidgets())if(auto *candidate=qobject_cast<MainWindow*>(widget))if(candidate->isVisible())main=candidate;recordReopen(352,"登录后实际打开服务端主窗口并关闭",main!=nullptr);if(main)main->close();});
        } else {
            bool noMain=true;for(auto *widget:QApplication::topLevelWidgets())if(auto *main=qobject_cast<MainWindow*>(widget))noMain&=!main->isVisible();recordReopen(352,"同一配置重启只恢复记住的账号，不自动进入主窗口",username->text()=="admin" && password->text().isEmpty() && noMain);login.reject();
        }
    });
    const QString loginSnapshotDir=qEnvironmentVariable("BOER_QT_LOGIN_SNAPSHOT_DIR");
    if(!loginSnapshotDir.isEmpty()) QTimer::singleShot(0,&login,[&] {
        QFile rowLog(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG"));
        if(!rowLog.open(QIODevice::WriteOnly|QIODevice::Append) && !rowLog.fileName().isEmpty()) qFatal("Cannot open row log");
        auto record=[&](int row,const QString &name,bool passed) {
            if(rowLog.isOpen()) {
                rowLog.write(QJsonDocument(QJsonObject{{"row",row},{"name",name},{"result",passed ? "PASS" : "FAIL"},
                    {"scope","Qt服务端登录真实控件；临时数据库"}}).toJson(QJsonDocument::Compact)+'\n'); rowLog.flush();
            }
            if(!passed) qFatal("Server login row %d failed: %s",row,qPrintable(name));
        };
        const auto iconBefore=passwordVisibility->icon().cacheKey(); passwordVisibility->trigger();
        record(351,"密码明文与图标切换",password->echoMode()==QLineEdit::Normal && passwordVisibility->icon().cacheKey()!=iconBefore);
        passwordVisibility->trigger(); record(351,"密码恢复暗文",password->echoMode()==QLineEdit::Password);
        QDir().mkpath(loginSnapshotDir);
        const QVariant savedLanguage=loginSettings.value("loginLanguage");
        if(currentLanguage!="zh-CN") language->click();
        QApplication::processEvents();
        if(languageText->text()!="简体中文" || username->placeholderText()!="请输入账号") qFatal("Chinese login locale failed");
        login.grab().save(QDir(loginSnapshotDir).filePath("login-zh.png"));
        language->click(); QApplication::processEvents();
        if(currentLanguage!="en-US" || languageText->text()!="English" || username->placeholderText()!="Enter Username") qFatal("English login locale failed");
        record(350,"登录页中英文标签和占位文字切换",currentLanguage=="en-US" && username->placeholderText()=="Enter Username");
        login.grab().save(QDir(loginSnapshotDir).filePath("login-en.png"));
        username->setText("admin"); password->setText("wrong-password"); loginButton->click(); QApplication::processEvents();
        if(!login.isVisible() || !loginErrorToast->isVisible() || QApplication::activeModalWidget()!=&login)
            qFatal("login error must remain a non-modal toast");
        login.grab().save(QDir(loginSnapshotDir).filePath("login-error-en.png"));
        language->click(); password->setText("wrong-password"); loginButton->click(); QApplication::processEvents();
        if(!loginErrorToast->isVisible() || loginErrorToast->text()!="管理员账号或密码错误")
            qFatal("Chinese login error toast failed");
        record(350,"中英文登录错误非模态显示，保持表单",login.isVisible() && loginErrorToast->isVisible() && QApplication::activeModalWidget()==&login);
        login.grab().save(QDir(loginSnapshotDir).filePath("login-error-zh.png"));
        if(savedLanguage.isValid()) loginSettings.setValue("loginLanguage",savedLanguage);
        else loginSettings.remove("loginLanguage");
        login.reject();
    });
    auto authenticate=[&]() {
        loggedIn=false; loginErrorToast->hide(); errorTimer->stop();
        return login.exec()==QDialog::Accepted && loggedIn;
    };
    if(!authenticate()) return loginCompatibilityFailures ? 1 : 0;
    MainWindow window(&database,&server,&deviceServer);
    QObject::connect(&window,&MainWindow::logoutRequested,&window,[&] {
        auto *gui=static_cast<QApplication*>(app.get());
        gui->setQuitOnLastWindowClosed(false);
        window.hide(); password->clear();
        currentLanguage=loginSettings.value("loginLanguage","zh-CN").toString();
        applyLanguage();
        if(authenticate()) { window.show(); gui->setQuitOnLastWindowClosed(true); }
        else gui->quit();
    });
    window.show();
    return app->exec();
}
