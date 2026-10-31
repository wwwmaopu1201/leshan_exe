#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QObject>
#include <functional>
class QNetworkReply;
class QNetworkRequest;

struct ApiResponse {
    bool ok=false;
    int status=0,code=0;
    QString error;
    QJsonValue data;
    QJsonObject body;
};

class ApiClient : public QObject {
    Q_OBJECT
public:
    using Callback=std::function<void(const ApiResponse&)>;
    explicit ApiClient(QObject *parent=nullptr);
    void setServer(const QString &host,int port);
    QString serverAddress() const { return host_+":"+QString::number(port_); }
    QString baseUrl() const { return "http://"+serverAddress(); }
    void setToken(const QString &token) { token_=token; }
    QString token() const { return token_; }
    void clearToken() { token_.clear(); ++generation_; }
    void request(const QByteArray &method,const QString &path,const QJsonValue &body,Callback callback,
                 int timeoutMs=15000);
    void get(const QString &path,Callback callback) { request("GET",path,{},std::move(callback)); }
    void post(const QString &path,const QJsonValue &body,Callback callback) { request("POST",path,body,std::move(callback)); }
    void put(const QString &path,const QJsonValue &body,Callback callback) { request("PUT",path,body,std::move(callback)); }
    void remove(const QString &path,const QJsonValue &body,Callback callback) { request("DELETE",path,body,std::move(callback)); }
    QNetworkReply *uploadFile(const QString &path,const QString &filePath,const QJsonObject &fields,Callback callback,
                    std::function<void(qint64,qint64)> progress={});
    void downloadToFile(const QString &path,const QString &filePath,Callback callback);
signals:
    void sessionExpired();
    void requestObserved(const QString &path,int status,bool ok);
private:
    QNetworkRequest makeRequest(const QString &path) const;
    void finish(QNetworkReply *reply,Callback callback);
    QNetworkAccessManager network_;
    QString host_="127.0.0.1",token_;
    int port_=8088;
    quint64 generation_=0;
};

QJsonArray apiRows(const QJsonValue &value);
