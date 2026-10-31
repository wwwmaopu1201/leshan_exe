#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>
#include <QUrlQuery>
#include <QTimer>
#include <atomic>

class Database;
class DeviceServer;
class QTcpSocket;
class QTemporaryFile;

class ApiServer : public QObject {
    Q_OBJECT
public:
    explicit ApiServer(Database *db, QObject *parent = nullptr);
    bool listen(quint16 port, QString *error);
    quint16 port() const { return server_.serverPort(); }
    void setDevicePort(quint16 port) { devicePort_ = port; }
    void setDeviceServer(DeviceServer *server) { deviceServer_ = server; }
    void setJwtSettings(const QString &secret,int expireHours) {
        if(!secret.isEmpty()) secret_=secret.toUtf8();
        expireHours_=qBound(1,expireHours,24*365);
    }
signals:
    void requestLogged(const QString &line);
    void deviceDeleted(qint64 deviceId);
private:
    struct Request { QString method, path, peerAddress, uploadTempPath; QHash<QString,QString> headers; QJsonObject body; QJsonArray array; QUrlQuery query; QByteArray rawBody; };
    struct Response { int status=200; QJsonObject body; QByteArray binary; QByteArray contentType; QByteArray disposition; QString filePath; bool removeFile=false; };
    void receive(QTcpSocket *socket);
    void dispatch(QTcpSocket *socket, const QByteArray &bytes,const QString &uploadTempPath={});
    void receiveUpload(QTcpSocket *socket,const QByteArray &bytes);
    Response route(const Request &req);
    Response login(const Request &req);
    Response auth(const Request &req, const QJsonObject &user);
    Response collection(const Request &req, const QJsonObject &user, const QString &section, const QStringList &parts);
    Response statistics(const Request &req, const QJsonObject &user, const QStringList &parts);
    Response system(const Request &req, const QJsonObject &user, const QStringList &parts);
    QJsonObject userForToken(const QString &header) const;
    QString makeToken(const QJsonObject &user) const;
    static Response ok(const QJsonValue &data = QJsonValue(), const QString &message = "success");
    static Response fail(int status, const QString &message);
    static QByteArray jsonBytes(const QJsonObject &body);
    Database *db_;
    QTcpServer server_;
    QHash<QTcpSocket*,QByteArray> pending_;
    struct UploadState {
        QByteArray head,buffer;
        QTemporaryFile *file=nullptr;
        qint64 remaining=0,chunkRemaining=-1;
        bool chunked=false,afterChunk=false;
    };
    QHash<QTcpSocket*,UploadState*> uploadPending_;
    QByteArray secret_;
    int expireHours_=24;
    quint16 devicePort_ = 0;
    DeviceServer *deviceServer_ = nullptr;
    QTimer externalSyncTimer_;
    std::atomic_bool syncing_{false};
};
