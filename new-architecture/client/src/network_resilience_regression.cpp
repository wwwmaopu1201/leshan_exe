#include "api_client.h"
#include "ui_i18n.h"
#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <memory>
namespace {
void spin(int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms){QApplication::processEvents();QThread::msleep(2);}QApplication::processEvents();}
bool until(const std::function<bool()> &f,int ms=3000){QElapsedTimer t;t.start();while(!f() && t.elapsed()<ms)spin(5);return f();}
}
int runNetworkResilienceRegression(){
    int failures=0;auto check=[&](const QString &name,bool passed){
        QJsonObject row{{"row",345},{"name","网络故障补测 client "+name},{"result",passed ? "PASS" : "FAIL"},{"scope","真实Qt ApiClient与本机TCP故障服务；超时/协议响应/下载原子保存，非物理断网"},{"time",QDateTime::currentDateTime().toString(Qt::ISODate)}};
        QFile file(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG"));if(file.open(QIODevice::Append)){file.write(QJsonDocument(row).toJson(QJsonDocument::Compact)+'\n');file.flush();}qInfo().noquote()<<row["name"].toString()<<row["result"].toString();failures+=!passed;
    };
    QTcpServer server; if(!server.listen(QHostAddress::LocalHost,0))return 2;
    QObject::connect(&server,&QTcpServer::newConnection,&server,[&]{while(auto *peer=server.nextPendingConnection()){
        QObject::connect(peer,&QTcpSocket::disconnected,peer,&QObject::deleteLater);
        auto buffer=std::make_shared<QByteArray>();auto handled=std::make_shared<bool>(false);
        QObject::connect(peer,&QTcpSocket::readyRead,peer,[peer,buffer,handled]{
            *buffer+=peer->readAll();if(*handled || !buffer->contains("\r\n\r\n"))return;*handled=true;
            const QByteArray path=buffer->split(' ').value(1);QByteArray body="{\"code\":0,\"data\":{\"value\":42}}";int status=200,delay=0;
            if(path.endsWith("/hang"))return;
            if(path.endsWith("/full-download"))body=QByteArray(512*1024,'D');
            if(path.endsWith("/html"))body="<html>upstream unavailable</html>";
            if(path.endsWith("/array"))body="[]";
            if(path.endsWith("/empty-object"))body="{}";
            if(path.endsWith("/string-code"))body="{\"code\":\"0\",\"data\":{}}";
            if(path.endsWith("/business"))body="{\"code\":400,\"message\":\"业务拒绝\"}";
            if(path.endsWith("/error")){status=503;body="{\"code\":503}";}
            if(path.endsWith("/auth")){status=401;body="{\"code\":401}";}
            if(path.endsWith("/late"))delay=350;
            if(path.endsWith("/truncated")){peer->write("HTTP/1.1 200 OK\r\nContent-Length: 200\r\nConnection: close\r\n\r\nSHORT");peer->disconnectFromHost();return;}
            if(path.endsWith("/cancel-download")){peer->write("HTTP/1.1 200 OK\r\nContent-Length: 20\r\nConnection: close\r\n\r\nFIRST");QTimer::singleShot(250,peer,[peer]{peer->write("SECOND1234567890");peer->disconnectFromHost();});return;}
            QTimer::singleShot(delay,peer,[peer,body,status]{peer->write("HTTP/1.1 "+QByteArray::number(status)+" Test\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);peer->disconnectFromHost();});
        });
    }});
    ApiClient api;api.setServer("127.0.0.1",server.serverPort());
    auto get=[&](const QString &path,int timeout=15000){auto done=std::make_shared<bool>(false);auto result=std::make_shared<ApiResponse>();api.request("GET",path,{},[=](const ApiResponse &value){*result=value;*done=true;},timeout);bool completed=until([&]{return *done;});return qMakePair(completed,*result);};
    auto okay=get("/ok");check("合法JSON响应正常返回业务数据",okay.first && okay.second.ok && okay.second.data.toObject().value("value")==42);
    for(const QString &path:{QString("/html"),QString("/array"),QString("/empty-object"),QString("/string-code")}){auto bad=get(path);check("HTTP200但响应不是JSON对象时不得假报成功 "+path,bad.first && !bad.second.ok && !bad.second.error.isEmpty());}
    UiI18n::instance()->setEnglish(true);check("新增响应格式错误有英文提示",UiI18n::instance()->text("服务器响应格式不正确，请检查服务端版本")=="The server response format is invalid. Please check the server version.");UiI18n::instance()->setEnglish(false);
    auto business=get("/business");check("HTTP200业务失败码保留失败和业务文案",business.first && !business.second.ok && business.second.error=="业务拒绝");
    auto unavailable=get("/error");check("503无业务文案返回可翻译服务器不可用提示",unavailable.first && !unavailable.second.ok && unavailable.second.error=="服务器暂时不可用，请稍后重试");
    auto timeout=get("/hang",150);check("实际150毫秒无响应请求超时回调一次并提示重试",timeout.first && !timeout.second.ok && timeout.second.error=="请求超时，请稍后重试");
    auto recovered=get("/ok");check("超时后同一ApiClient无需重建即可正常请求",recovered.first && recovered.second.ok);
    int expired=0,authCallback=0;QObject::connect(&api,&ApiClient::sessionExpired,&api,[&]{++expired;});api.setToken("temporary-test-token");api.get("/auth",[&](const ApiResponse &){++authCallback;});until([&]{return expired>0;});spin(50);check("401只触发一次会话失效且不回调旧页面",expired==1 && authCallback==0 && api.token().isEmpty());
    api.setToken("old");bool stale=false;api.get("/late",[&](const ApiResponse &){stale=true;});spin(60);api.clearToken();spin(450);check("退出会话后迟到响应不回调旧页面",!stale);
    QTemporaryDir temp;const QString target=temp.filePath("preserved.ntp");auto seed=[&]{QFile f(target);if(!f.open(QIODevice::WriteOnly) || f.write("ORIGINAL")!=8)qFatal("Unable to seed isolated download test");};auto original=[&]{QFile f(target);return f.open(QIODevice::ReadOnly) && f.readAll()=="ORIGINAL";};
    for(const QString &path:{QString("/error"),QString("/truncated")}){seed();bool done=false;ApiResponse result;api.downloadToFile(path,target,[&](const ApiResponse &value){result=value;done=true;});check("下载失败或传输截断保留原文件 "+path,until([&]{return done;}) && !result.ok && original());}
    seed();bool canceledCallback=false;api.downloadToFile("/cancel-download",target,[&](const ApiResponse &){canceledCallback=true;});spin(80);api.clearToken();spin(400);check("退出会话取消下载不覆盖原文件不回调旧页面",original() && !canceledCallback);
    const QString fullTarget=qEnvironmentVariable("BOER_TEST_FULL_DISK_TARGET");
    if(!fullTarget.isEmpty()){
        bool done=false;ApiResponse result;api.downloadToFile("/full-download",fullTarget,[&](const ApiResponse &value){result=value;done=true;});
        const bool completed=until([&]{return done;});QFile preserved(fullTarget);const bool intact=preserved.open(QIODevice::ReadOnly) && preserved.readAll()=="ORIGINAL";
        check("真实虚拟卷磁盘满下载明确失败并保留原文件",completed && !result.ok && intact);
    }
    bool staleServer=false;api.get("/late",[&](const ApiResponse &){staleServer=true;});spin(60);api.setServer("127.0.0.1",server.serverPort()+1);spin(450);check("切换服务地址后旧地址迟到响应不污染新页面",!staleServer);
    api.setServer("127.0.0.1",server.serverPort());auto final=get("/ok");check("恢复服务地址后请求正常",final.first && final.second.ok);
    return failures ? 1 : 0;
}

#include "client_window.h"
#include "ui_i18n.h"
int runConcurrentClientRegression(ClientWindow &window,const QString &host,int port){
    const QString username=qEnvironmentVariable("BOER_TEST_MULTI_USER");auto *api=window.api();api->setServer(host,port);
    bool logged=false;ApiResponse login;api->post("/auth/login",QJsonObject{{"username",username},{"password","test1234"}},[&](const ApiResponse &value){login=value;logged=true;});
    if(!until([&]{return logged;},15000) || !login.ok)return 2;
    window.testSession(host,port,login.data.toObject().value("user").toObject(),login.data.toObject().value("token").toString());
    int failed=0,requests=0;const auto observation=QObject::connect(api,&ApiClient::requestObserved,&window,[&](const QString &,int,bool okay){++requests;failed+=!okay;});
    const QStringList pages={"/home","/dashboard","/device/list","/employee/list","/file/pattern","/statistics/salary","/statistics/process","/statistics/alarm"};
    QElapsedTimer elapsed;elapsed.start();int steps=0;while(elapsed.elapsed()<40000){window.navigate(pages[steps%pages.size()]);UiI18n::instance()->setEnglish(steps%2);spin(500);++steps;}
    spin(500);bool final=false;api->get("/device/list?fullModel=1",[&](const ApiResponse &value){final=value.ok && apiRows(value.data).size()==40;});until([&]{return final;});
    const bool passed=steps>=64 && requests>=60 && failed==0 && final && api->token()==login.data.toObject().value("token").toString();
    QJsonObject row{{"row",345},{"name","多客户端补测 "+username+" 40秒实际页面中英文轮换及会话数据完整"},{"result",passed ? "PASS" : "FAIL"},{"scope","4个独立Qt客户端进程同时连接真实Qt服务端；本机offscreen，非四台物理工作站"},{"steps",steps},{"requests",requests},{"failedRequests",failed}};
    QFile log(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG"));if(log.open(QIODevice::Append)){log.write(QJsonDocument(row).toJson(QJsonDocument::Compact)+'\n');log.flush();}qInfo().noquote()<<QJsonDocument(row).toJson(QJsonDocument::Compact);
    QObject::disconnect(observation);
    QFile marker(qEnvironmentVariable("BOER_TEST_MULTI_MARKER"));if(!marker.open(QIODevice::WriteOnly))return 3;marker.write(QJsonDocument(QJsonObject{{"complete",true},{"passed",passed},{"user",username}}).toJson());marker.close();
    return passed ? 0 : 1;
}
