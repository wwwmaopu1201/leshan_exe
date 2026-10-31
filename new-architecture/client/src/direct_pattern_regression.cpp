#include "direct_pattern_dialog.h"
#include "api_client.h"
#include "ui_i18n.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <memory>
namespace {
bool until(const std::function<bool()> &test,int ms=5000){QElapsedTimer time;time.start();while(!test() && time.elapsed()<ms){QApplication::processEvents();QThread::msleep(2);}QApplication::processEvents();return test();}
}
int runDirectPatternRegression(){
    int failures=0;auto check=[&](bool passed,const char *name){qInfo()<< (passed ? "PASS" : "FAIL") <<name;failures+=!passed;};
    QTcpServer server;if(!server.listen(QHostAddress::LocalHost,0))return 2;
    int uploads=0,deletes=0,polls=0,unexpected=0;QString mode="completed";QHash<QString,int> jobPolls;
    QObject::connect(&server,&QTcpServer::newConnection,&server,[&]{while(auto *peer=server.nextPendingConnection()){
        auto buffer=std::make_shared<QByteArray>();auto handled=std::make_shared<bool>(false);
        QObject::connect(peer,&QTcpSocket::disconnected,peer,&QObject::deleteLater);
        QObject::connect(peer,&QTcpSocket::readyRead,peer,[&,peer,buffer,handled]{
            *buffer+=peer->readAll();int end=buffer->indexOf("\r\n\r\n");if(*handled || end<0)return;
            int length=0;for(auto line:buffer->left(end).split('\n'))if(line.toLower().startsWith("content-length:"))length=line.mid(15).trimmed().toInt();
            if(buffer->size()<end+4+length)return;*handled=true;
            const auto request=buffer->left(buffer->indexOf('\r')).split(' ');const auto method=request.value(0),path=request.value(1);
            QJsonObject data;int code=0,http=200;
            if(method=="POST" && path=="/api/pattern/direct-upload"){
                ++uploads;check(buffer->contains("LOCAL_PATTERN_PAYLOAD"),"upload keeps selected local file bytes");
                if(mode=="unsupported"){code=404;http=404;}else {const QString id=QString::number(uploads);jobPolls[id]=0;data={{"id",id}};}
            }else if(path.startsWith("/api/pattern/direct-transfer/")){
                const QString id=QString::fromUtf8(path.mid(path.lastIndexOf('/')+1));
                if(method=="DELETE")++deletes;
                else if(method=="GET"){++polls;const int count=++jobPolls[id];const QString state=mode=="hold" || count==1 ? "downloading" : mode;data={{"status",state},{"progress",state=="completed" ? 100 : 50},{"message",state=="failed" ? "设备拒绝" : ""}};}
                else ++unexpected;
            }else ++unexpected;
            const auto body=QJsonDocument(QJsonObject{{"code",code},{"data",data},{"message",code ? "unsupported" : ""}}).toJson(QJsonDocument::Compact);
            peer->write("HTTP/1.1 "+QByteArray::number(http)+" Test\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);peer->disconnectFromHost();
        });
    }});
    QTemporaryDir temp;const QString path=temp.filePath("local.ntp");{QFile file(path);if(!file.open(QIODevice::WriteOnly))return 2;file.write("LOCAL_PATTERN_PAYLOAD");}
    ApiClient api;api.setServer("127.0.0.1",server.serverPort());
    QJsonArray devices{QJsonObject{{"id",1},{"displayName","Device A"}},QJsonObject{{"id",2},{"displayName","Device B"}}};
    UiI18n::instance()->setEnglish(false);
    {
        DirectPatternDialog dialog(nullptr,&api,{{path,"local.ntp"}},devices);dialog.show();
        auto *close=dialog.findChild<QPushButton*>("directTransferClose");auto *table=dialog.findChild<QTableWidget*>();
        check(dialog.property("fixedModalPosition").toBool() && dialog.isModal(),"progress dialog is fixed and modal");
        check(until([&]{return close->text()=="关闭";}),"two device transfers finish sequentially");
        check(uploads==2 && until([&]{return deletes==2;}) && polls>=4,"all transient jobs deleted after completion");
        check(table->item(0,2)->text()=="下发完成" && table->item(1,2)->text()=="下发完成" && dialog.findChild<QProgressBar*>()->value()==100,"table and progress show machine completion");
        UiI18n::instance()->setEnglish(true);check(close->text()=="Close" && table->item(0,2)->text()=="Completed","live English language switch");dialog.reject();
    }
    mode="failed";
    {
        DirectPatternDialog dialog(nullptr,&api,{{path,"local.ntp"}},{devices[0]});dialog.show();auto *close=dialog.findChild<QPushButton*>("directTransferClose");
        check(until([&]{return close->text()=="Close";}),"device rejection completes dialog");check(dialog.findChild<QTableWidget*>()->item(0,2)->text().startsWith("Failed"),"device rejection shown as failure");check(until([&]{return deletes==3;}),"failed job cleaned");dialog.reject();
    }
    mode="hold";
    {
        DirectPatternDialog dialog(nullptr,&api,{{path,"local.ntp"}},devices);dialog.show();const int before=polls;
        check(until([&]{return polls>before;}),"cancel test reaches active machine transfer");dialog.reject();
        check(until([&]{return deletes==4;}) && uploads==4,"cancel clears active job and stops remaining devices");
    }
    mode="unsupported";
    {
        DirectPatternDialog dialog(nullptr,&api,{{path,"local.ntp"}},{devices[0]});dialog.show();auto *close=dialog.findChild<QPushButton*>("directTransferClose");
        check(until([&]{return close->text()=="Close";}),"unsupported server fails without fallback");check(dialog.findChild<QTableWidget*>()->item(0,2)->text().contains("Upgrade the Qt server"),"old server requires explicit upgrade");dialog.reject();
    }
    check(unexpected==0 && QFile::exists(path),"no library upload fallback and local file preserved");UiI18n::instance()->setEnglish(false);
    return failures ? 1 : 0;
}
