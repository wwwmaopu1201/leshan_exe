#include "online_update.h"
#include "update_progress_dialog.h"
#include "ui_i18n.h"
#include "ui_dialogs.h"
#include <QPointer>
#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextBrowser>
#include <QTimer>
#include <QVersionNumber>
#include <QVBoxLayout>
#include <memory>
#include <functional>

namespace {
void retireUpdateStep(QDialog *dialog,QObject *context,const std::function<void()> &next) {
    QObject::connect(dialog,&QObject::destroyed,context,[context,next]{QTimer::singleShot(0,context,next);});
    dialog->setEnabled(false);dialog->hide();dialog->deleteLater();
}

void bindUpdateLanguage(QDialog *dialog,const std::function<void()> &translate) {
    dialog->setProperty("i18nSkip",true);
    for(auto *child:dialog->findChildren<QWidget*>())child->setProperty("i18nSkip",true);
    translate();
    if(auto *i18n=UiI18n::instance())QObject::connect(i18n,&UiI18n::languageChanged,dialog,translate);
}


bool validUpdateUrl(const QUrl &url) {
    if(!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()) return false;
    if(url.scheme()=="https") return true;
    // HTTP is allowed only for the explicitly configured update server/directory.
    const QString path=QDir::cleanPath(url.path());
    return url.scheme()=="http" && url.host()=="47.92.226.92" && url.port()==99 && path.startsWith("/update/");
}
bool version(const QString &text) { return QRegularExpression("^[0-9]+(\\.[0-9]+){1,3}$").match(text).hasMatch(); }
QString platform() {
#ifdef Q_OS_WIN
    return "windows-"+QSysInfo::currentCpuArchitecture();
#elif defined(Q_OS_MACOS)
    return "macos-"+QSysInfo::currentCpuArchitecture();
#else
    return "linux-"+QSysInfo::currentCpuArchitecture();
#endif
}
QNetworkRequest request(const QUrl &url) {
    QNetworkRequest req(url);
    req.setTransferTimeout(30000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}
}
OnlineUpdate::OnlineUpdate(const QString &product,QObject *parent,QNetworkAccessManager *network,const QString &downloadDirectory)
    : QObject(parent),downloadDirectory_(downloadDirectory),product_(product),network_(network ? network : new QNetworkAccessManager(this)) {
    QFile config(QDir(QCoreApplication::applicationDirPath()).filePath("update-config.json"));
    QJsonObject settings;
    if(config.open(QIODevice::ReadOnly)) settings=QJsonDocument::fromJson(config.readAll()).object();
    QString address=qEnvironmentVariable(product=="client" ? "BOER_CLIENT_UPDATE_URL" : "BOER_SERVER_UPDATE_URL");
    if(address.isEmpty()) address=settings.value(product+"ManifestUrl").toString();
    if(address.isEmpty()) address=defaultManifestUrl(product).toString();
    manifest_=QUrl(address);
}
QUrl OnlineUpdate::defaultManifestUrl(const QString &product) {
    if(product!="client" && product!="server") return {};
    return QUrl("http://47.92.226.92:99/update/"+product+".json");
}
bool OnlineUpdate::isNewer(const QString &candidate,const QString &current) {
    return version(candidate) && version(current) && QVersionNumber::compare(QVersionNumber::fromString(candidate),QVersionNumber::fromString(current))>0;
}
bool OnlineUpdate::validRelease(const QJsonObject &r) {
    return version(r.value("version").toString()) && validUpdateUrl(QUrl(r.value("url").toString())) &&
        QRegularExpression("^[a-fA-F0-9]{64}$").match(r.value("sha256").toString()).hasMatch() &&
        !r.value("notes").toString().trimmed().isEmpty();
}
void OnlineUpdate::check() {
    if(checking_ || updating_ || !validUpdateUrl(manifest_)) return;
    checking_=true;
    auto *reply=network_->get(request(manifest_));
    // Metadata is deliberately bounded, even if the remote server streams endlessly.
    connect(reply,&QNetworkReply::readyRead,reply,[reply] { if(reply->bytesAvailable()>1024*1024) reply->abort(); });
    QTimer::singleShot(15000,reply,[reply] { if(!reply->isFinished()) reply->abort(); });
    connect(reply,&QNetworkReply::finished,this,[this,reply] {
        checking_=false;reply->deleteLater();
        if(reply->error()!=QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200) return;
        const auto root=QJsonDocument::fromJson(reply->readAll()).object();
        if(root.value("product").toString()!=product_) return;
        auto release=root.value("releases").toObject().value(platform()).toObject();
        if(!validRelease(release) || !isNewer(release.value("version").toString(),QCoreApplication::applicationVersion())) return;
        if(QSettings().value("updates/"+product_+"/skippedVersion").toString()==release.value("version").toString()) return;
        offer(release);
    });
}
void OnlineUpdate::offer(const QJsonObject &release) {
    if(updating_)return;
    updating_=true;
    auto *dialog=new QDialog(QApplication::activeWindow());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setProperty("updateStep","offer");
    connect(dialog,&QDialog::finished,this,[this]{updating_=false;});
    dialog->setWindowTitle("发现新版本"); dialog->resize(640,440);
    auto *layout=new QVBoxLayout(dialog);
    auto *title=new QLabel(QString("%1\n当前版本：%2    新版本：%3").arg(product_=="client" ? "客户端更新" : "服务端更新",QCoreApplication::applicationVersion(),release.value("version").toString()),dialog);
    layout->addWidget(title);
    auto *notes=new QTextBrowser(dialog); notes->setPlainText(release.value("notes").toString()); layout->addWidget(notes);
    auto *buttons=new QDialogButtonBox(dialog);
    auto *later=buttons->addButton("稍后再说",QDialogButtonBox::RejectRole);
    auto *skip=buttons->addButton("跳过此版本",QDialogButtonBox::ActionRole);
    auto *get=buttons->addButton("下载更新",QDialogButtonBox::AcceptRole);get->setObjectName("updateDownload"); get->setDefault(true);
    layout->addWidget(buttons);
    bindUpdateLanguage(dialog,[this,dialog,title,later,skip,get,release] {
        dialog->setWindowTitle(updateText("发现新版本","New version available"));
        title->setText(updateText("%1\n当前版本：%2    新版本：%3","%1\nCurrent version: %2    New version: %3")
            .arg(product_=="client" ? updateText("客户端更新","Client update") : updateText("服务端更新","Server update"),QCoreApplication::applicationVersion(),release.value("version").toString()));
        later->setText(updateText("稍后再说","Remind me later"));skip->setText(updateText("跳过此版本","Skip this version"));get->setText(updateText("下载更新","Download update"));
    });
    connect(later,&QPushButton::clicked,dialog,&QDialog::reject);
    connect(skip,&QPushButton::clicked,dialog,[this,dialog,release] {
        QSettings().setValue("updates/"+product_+"/skippedVersion",release.value("version").toString()); dialog->reject();
    });
    const QPointer<QWidget> owner=dialog->parentWidget();
    connect(get,&QPushButton::clicked,dialog,[this,dialog,release,owner] {
        retireUpdateStep(dialog,this,[this,release,owner]{download(release,owner);});
    });
    configureUpdateDialog(dialog);
    dialog->show();
}
void OnlineUpdate::download(const QJsonObject &release,QWidget *parent) {
    const QUrl url(release.value("url").toString());
    QString suffix=QFileInfo(url.path()).suffix().toLower();
#ifdef Q_OS_WIN
    const bool supported=suffix=="exe" || suffix=="msi";
#elif defined(Q_OS_MACOS)
    const bool supported=suffix=="dmg" || suffix=="pkg";
#else
    const bool supported=suffix=="deb" || suffix=="rpm";
#endif
    if(!supported) { showError(updateText("更新包格式不受支持，请联系管理员。","Unsupported installer format. Please contact your administrator."),parent); return; }
    const QString directory=downloadDirectory_.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/updates" : downloadDirectory_;
    if(!QDir().mkpath(directory)) { showError(updateText("无法创建更新下载目录。","Unable to create the update download directory."),parent); return; }
    const QString path=directory+"/bohr-"+product_+"-"+release.value("version").toString()+"."+suffix;
    auto file=std::make_shared<QSaveFile>(path);
    if(!file->open(QIODevice::WriteOnly)) { showError(file->errorString(),parent); return; }
    auto hash=std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto failed=std::make_shared<bool>(false);
    auto *progress=new UpdateProgressDialog(product_,release.value("version").toString(),parent);
    progress->setProperty("updateStep","download");
    progress->show();
    auto *reply=network_->get(request(url));
    connect(progress,&QDialog::rejected,reply,&QNetworkReply::abort);
    connect(reply,&QNetworkReply::readyRead,this,[reply,file,hash,failed] {
        const auto bytes=reply->readAll(); hash->addData(bytes);
        if(file->write(bytes)!=bytes.size()) { *failed=true; reply->abort(); }
    });
    connect(reply,&QNetworkReply::downloadProgress,progress,&UpdateProgressDialog::setDownloadProgress);
    const QPointer<QWidget> owner=progress->parentWidget();
    connect(reply,&QNetworkReply::finished,this,[this,reply,file,hash,failed,progress,path,release,owner] {
        const bool canceled=progress->wasCanceled();reply->deleteLater();
        QString error;
        if(canceled)file->cancelWriting();
        else if(*failed || reply->error()!=QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200 ||
                hash->result().toHex()!=release.value("sha256").toString().toLower().toLatin1()) {
            file->cancelWriting();error=updateText("下载失败或文件校验失败，请稍后重启软件重试。","Download or verification failed. Please restart the application and try again.");
        } else if(!file->commit())error=updateText("更新包保存失败，请检查磁盘空间。","Unable to save the installer. Please check available disk space.");
        retireUpdateStep(progress,this,[this,canceled,error,path,owner] {
            if(canceled){updating_=false;return;}
            if(!error.isEmpty())showError(error,owner);else showReady(path,owner);
        });
    });
}
void OnlineUpdate::showError(const QString &message,QWidget *owner) {
    {
        QMessageBox box(QMessageBox::Warning,updateText("更新失败","Update failed"),message,QMessageBox::NoButton,owner);
        box.setProperty("updateStep","error");
        box.addButton(updateText("确定","OK"),QMessageBox::AcceptRole);configureUpdateDialog(&box);box.exec();
    }
    updating_=false;
}
void OnlineUpdate::showReady(const QString &path,QWidget *owner) {
    bool open=false;
    {
        QMessageBox box(QMessageBox::Information,"","",QMessageBox::NoButton,owner);
        box.setProperty("updateStep","complete");
        auto *install=box.addButton("",QMessageBox::AcceptRole);install->setObjectName("updateInstall");
        auto *later=box.addButton("",QMessageBox::RejectRole);later->setObjectName("updateInstallLater");
        bindUpdateLanguage(&box,[&box,install,later] {
            box.setWindowTitle(updateText("下载完成","Download complete"));
            box.setText(updateText("更新包已下载并通过校验。点击安装后软件将自动退出，请先保存工作；服务端请先备份数据。","The installer has been downloaded and verified. Save your work before installing: the application will exit automatically. Back up the server database first."));
            install->setText(updateText("打开安装包","Open installer"));later->setText(updateText("稍后安装","Install later"));
        });
        configureUpdateDialog(&box);box.exec();open=box.clickedButton()==install;
    }
    // The completion window is destroyed before the OS installer is launched.
    if(!open){updating_=false;return;}
    const QPointer<QWidget> parent=owner;
    QTimer::singleShot(0,this,[this,path,parent] {
        if(!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))showError(updateText("无法打开安装包，请手动打开：","Unable to open the installer. Please open manually: ")+path,parent);
#ifdef Q_OS_WIN
        else QTimer::singleShot(0,qApp,&QCoreApplication::quit);
#endif
        updating_=false;
    });
}

void startOnlineUpdate(const QString &product) {
    for(const auto &arg:QCoreApplication::arguments())
        if(arg.contains("test") || arg.contains("regression") || arg=="--headless") return;
    auto *updater=new OnlineUpdate(product,qApp);
    QTimer::singleShot(1200,updater,[updater] { updater->check(); });
}
