#include "api_client.h"
#include "ui_i18n.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace {
QString networkFailure(QNetworkReply *reply,int status) {
    if(reply->error()==QNetworkReply::TimeoutError || reply->error()==QNetworkReply::OperationCanceledError)return "请求超时，请稍后重试";
    if(status>=500)return "服务器暂时不可用，请稍后重试";
    if(status==404)return "请求的功能暂不可用，请检查服务端版本";
    if(status==401)return "登录已过期，请重新登录";
    if(status==403)return "没有操作权限";
    if(status==400)return "请求参数错误，请检查后重试";
    return status>0 ? "请求失败，请稍后重试" : "无法连接服务器，请检查网络和服务器地址";
}
}

ApiClient::ApiClient(QObject *parent):QObject(parent) {}

void ApiClient::setServer(const QString &host,int port) {
    const QString nextHost=host.trimmed();const int nextPort=qBound(1,port,65535);
    if(nextHost!=host_ || nextPort!=port_)clearToken();
    host_=nextHost; port_=nextPort;
}

QNetworkRequest ApiClient::makeRequest(const QString &path) const {
    QNetworkRequest request(QUrl(baseUrl()+"/api"+path));
    request.setRawHeader("Accept-Language",UiI18n::instance() && UiI18n::instance()->english() ? "en-US" : "zh-CN");
    if(!token_.isEmpty()) request.setRawHeader("Authorization",("Bearer "+token_).toUtf8());
    return request;
}

void ApiClient::finish(QNetworkReply *reply,Callback callback) {
    const QByteArray bytes=reply->isOpen() ? reply->readAll() : QByteArray();
    const QJsonDocument document=QJsonDocument::fromJson(bytes);
    const QJsonObject body=document.object();
    const bool validEnvelope=document.isObject() && body.value("code").isDouble();
    ApiResponse result;
    result.status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.code=body.value("code").toInt(result.status);
    result.body=body; result.data=body.value("data");
    result.ok=validEnvelope && reply->error()==QNetworkReply::NoError && result.status>=200 && result.status<300 &&
        (result.code==0 || result.code==200);
    result.error=body.value("message").toString();
    if(!result.ok && result.error.isEmpty()) result.error=reply->error()==QNetworkReply::NoError && result.status>=200 && result.status<300 && !validEnvelope
        ? QString("服务器响应格式不正确，请检查服务端版本") : networkFailure(reply,result.status);
    emit requestObserved(reply->request().url().path(),result.status,result.ok);
    if(result.status==401 && !token_.isEmpty()) {
        clearToken(); reply->deleteLater(); emit sessionExpired(); return;
    }
    if(callback) callback(result);
    reply->deleteLater();
}

void ApiClient::request(const QByteArray &method,const QString &path,const QJsonValue &body,
                        Callback callback,int timeoutMs) {
    QNetworkRequest message=makeRequest(path);
    QByteArray payload;
    if(method!="GET") {
        message.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        payload=body.isArray() ? QJsonDocument(body.toArray()).toJson(QJsonDocument::Compact) :
            QJsonDocument(body.isObject() ? body.toObject() : QJsonObject{}).toJson(QJsonDocument::Compact);
    }
    QNetworkReply *reply=method=="GET" ? network_.get(message) :
        network_.sendCustomRequest(message,method,payload);
    const quint64 generation=generation_;
    QTimer::singleShot(timeoutMs,reply,[reply] { if(!reply->isFinished()) reply->abort(); });
    connect(reply,&QNetworkReply::finished,this,[this,reply,generation,callback=std::move(callback)]() mutable {
        if(generation!=generation_) { reply->deleteLater(); return; }
        finish(reply,std::move(callback));
    });
}

QNetworkReply *ApiClient::uploadFile(const QString &path,const QString &filePath,const QJsonObject &fields,
                           Callback callback,std::function<void(qint64,qint64)> progress) {
    auto *multi=new QHttpMultiPart(QHttpMultiPart::FormDataType);
    for(auto it=fields.begin();it!=fields.end();++it) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QString("form-data; name=\"%1\"").arg(it.key()));
        part.setBody(it.value().toVariant().toString().toUtf8()); multi->append(part);
    }
    auto *file=new QFile(filePath,multi);
    if(!file->open(QIODevice::ReadOnly)) {
        ApiResponse error; error.error="无法读取文件："+QFileInfo(filePath).fileName();
        if(callback) callback(error);
        delete multi; return nullptr;
    }
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QString("form-data; name=\"file\"; filename=\"%1\"").arg(QFileInfo(filePath).fileName()));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader,"application/octet-stream");
    filePart.setBodyDevice(file); multi->append(filePart);
    QNetworkReply *reply=network_.post(makeRequest(path),multi);
    multi->setParent(reply);
    const quint64 generation=generation_;
    if(progress) connect(reply,&QNetworkReply::uploadProgress,this,
        [this,generation,progress=std::move(progress)](qint64 sent,qint64 total) {
            if(generation==generation_) progress(sent,total);
        });
    QTimer::singleShot(30*60*1000,reply,[reply] { if(!reply->isFinished()) reply->abort(); });
    connect(reply,&QNetworkReply::finished,this,[this,reply,generation,callback=std::move(callback)]() mutable {
        if(generation!=generation_) { reply->deleteLater(); return; }
        finish(reply,std::move(callback));
    });
    return reply;
}

void ApiClient::downloadToFile(const QString &path,const QString &filePath,Callback callback) {
    auto output=std::make_shared<QSaveFile>(filePath);
    if(!output->open(QIODevice::WriteOnly)) {
        ApiResponse error; error.error="无法保存文件";
        if(callback) callback(error); return;
    }
    QNetworkRequest message=makeRequest(path);
    QNetworkReply *reply=network_.get(message);
    const quint64 generation=generation_;
    connect(reply,&QNetworkReply::readyRead,this,[reply,output] { output->write(reply->readAll()); });
    QTimer::singleShot(30*60*1000,reply,[reply] { if(!reply->isFinished()) reply->abort(); });
    connect(reply,&QNetworkReply::finished,this,[this,reply,output,generation,callback=std::move(callback)]() mutable {
        if(generation!=generation_) { output->cancelWriting(); reply->deleteLater(); return; }
        output->write(reply->readAll());
        ApiResponse result;
        result.status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool received=reply->error()==QNetworkReply::NoError && result.status>=200 && result.status<300;
        result.ok=received && output->commit();
        if(!result.ok) {
            result.error=received ? QString("无法保存文件，请检查目录权限和可用空间") : networkFailure(reply,result.status);
            output->cancelWriting();
            if(result.status==401 && !token_.isEmpty()) {
                clearToken(); reply->deleteLater(); emit sessionExpired(); return;
            }
        }
        if(callback) callback(result);
        reply->deleteLater();
    });
}

QJsonArray apiRows(const QJsonValue &value) {
    if(value.isArray()) return value.toArray();
    return value.toObject().value("list").toArray();
}
