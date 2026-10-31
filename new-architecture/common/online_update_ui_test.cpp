#include "online_update.h"
#include "ui_i18n.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstring>
#include <cstdio>
class FixtureReply : public QNetworkReply {
public:
    FixtureReply(const QNetworkRequest &request,QObject *parent):QNetworkReply(parent),data_(8192,'U') {
        setRequest(request);setUrl(request.url());setAttribute(QNetworkRequest::HttpStatusCodeAttribute,200);open(QIODevice::ReadOnly);
        QTimer::singleShot(120,this,[this]{if(isFinished())return;emit readyRead();emit downloadProgress(data_.size(),data_.size());setFinished(true);emit finished();});
    }
    void abort() override {if(isFinished())return;setError(OperationCanceledError,"Canceled");setFinished(true);emit finished();}
    qint64 bytesAvailable() const override {return data_.size()-offset_+QNetworkReply::bytesAvailable();}
protected:
    qint64 readData(char *target,qint64 max) override {qint64 count=qMin(max,qint64(data_.size()-offset_));if(!count)return -1;std::memcpy(target,data_.constData()+offset_,count);offset_+=count;return count;}
private: QByteArray data_;qint64 offset_=0;
};
class FixtureNetwork : public QNetworkAccessManager {
protected:
    QNetworkReply *createRequest(Operation,const QNetworkRequest &request,QIODevice *) override{return new FixtureReply(request,this);}
};
class OnlineUpdateUiTest {
public:
    static int run(QApplication &app) {
        QTemporaryDir directory;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
        QWidget owner;owner.resize(900,600);owner.show();owner.activateWindow();app.processEvents();UiI18n i18n(&app);
        int failures=0;auto check=[&](bool ok,const char *name){if(!ok){std::fprintf(stderr,"FAIL: %s\n",name);++failures;}};
        for(bool english:{false,true}) for(int scenario=0;scenario<3;++scenario) {
            i18n.setEnglish(english);FixtureNetwork network;OnlineUpdate update("client",nullptr,&network,directory.path()+"/"+QString::number(english)+"-"+QString::number(scenario));
            QJsonObject release{{"version","0.1.5"},{"notes","更新说明"},{"url","https://example.invalid/update.dmg"},{"sha256",QString::fromLatin1(QCryptographicHash::hash(QByteArray(8192,'U'),QCryptographicHash::Sha256).toHex())}};
#ifdef Q_OS_WIN
            release["url"]="https://example.invalid/update.exe";
#elif !defined(Q_OS_MACOS)
            release["url"]="https://example.invalid/update.deb";
#endif
            if(scenario==2)release["sha256"]=QString(64,'0');
            QPointer<QWidget> offer,progress;bool sawDownload=false,sawComplete=false,sawError=false,clicked=false,overlap=false;
            QTimer poll;poll.setInterval(2);
            QObject::connect(&poll,&QTimer::timeout,[&]{
                int visible=0;QWidget *step=nullptr;
                for(auto *widget:QApplication::topLevelWidgets())if(widget->isVisible() && widget->property("updateStep").isValid()){++visible;step=widget;}
                overlap|=visible>1;
                if(!step)return;
                const QString name=step->property("updateStep").toString();
                if(name=="offer" && !clicked) {
                    clicked=true;offer=step;auto *download=step->findChild<QPushButton*>("updateDownload");check(download && download->text()==(english ? "Download update" : "下载更新"),"offer language");download->click();
                } else if(name=="download" && !sawDownload) {
                    sawDownload=true;progress=step;check(offer.isNull(),"offer destroyed before download appears");
                    if(scenario==1)step->findChild<QPushButton*>("updateProgressCancel")->click();
                } else if(name=="complete" && !sawComplete) {
                    sawComplete=true;check(progress.isNull(),"download destroyed before completion appears");
                    auto *later=step->findChild<QPushButton*>("updateInstallLater");check(later && later->text()==(english ? "Install later" : "稍后安装"),"completion language");later->click();
                } else if(name=="error") {
                    sawError=true;check(scenario==2 && progress.isNull(),"download destroyed before verification error appears");for(auto *button:step->findChildren<QPushButton*>())button->click();
                }
            });
            poll.start();update.offer(release);update.offer(release); // Duplicate offers must not create a second window.
            QElapsedTimer timeout;timeout.start();
            while(update.updating_ && timeout.elapsed()<4000){app.processEvents();QThread::msleep(1);}
            poll.stop();check(!update.updating_ && sawDownload && sawComplete==(scenario==0) && sawError==(scenario==2) && !overlap,"one window at a time for success, cancellation and verification failure");
        }
        return failures?1:0;
    }
};
int main(int argc,char **argv){QApplication app(argc,argv);return OnlineUpdateUiTest::run(app);}
