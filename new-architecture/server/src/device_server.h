#pragma once

#include <QHash>
#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>
#include <QTimer>
#include <QVector>
#include <QMap>
#include <QSet>

class Database;
class QTcpSocket;
class QEventLoop;

// Implements the documented DT frame envelope and the basic online/status messages.
class DeviceServer : public QObject {
    Q_OBJECT
public:
    explicit DeviceServer(Database *db, QObject *parent = nullptr);
    ~DeviceServer() override;
    bool listen(quint16 port, QString *error);
    quint16 port() const { return server_.serverPort(); }
    void disconnectDevice(qint64 deviceId);
    bool deviceConnected(qint64 deviceId) const;
    QString startDirectTransfer(qint64 deviceId,qint64 ownerId,const QString &name,const QByteArray &data,QString *error);
    QJsonObject directTransfer(const QString &id,qint64 ownerId);
    bool cancelDirectTransfer(const QString &id,qint64 ownerId);
    bool sendCurrentUser(qint64 deviceId,const QString &employeeCode,const QString &employeeName);
    bool refreshPatternList(qint64 deviceId,QString *error);
    bool deletePatternFile(qint64 deviceId,int patternNo,const QString &name,QString *error);
signals:
    void eventLogged(const QString &line);
private:
    struct Session { QByteArray buffer; qint64 deviceId=0, runtimeId=0; QString code,mainboardSn; QDateTime lastSeen; QTimer *probe=nullptr,*realtime=nullptr; };
    static quint16 crc(const QByteArray &bytes);
    static QByteArray frame(quint16 type, quint16 number, const QByteArray &payload={},
                            quint8 totalFrames=1,quint8 frameNo=1,quint32 addr1=0,quint32 addr2=0);
    static quint16 u16(const QByteArray &bytes, int offset);
    static quint32 u32(const QByteArray &bytes, int offset);
    void receive(QTcpSocket *socket);
    void process(QTcpSocket *socket, quint16 type, quint16 number, const QByteArray &payload);
    void handleProduction(QTcpSocket *socket,quint16 number,const QByteArray &payload);
    void processPattern(QTcpSocket *socket,quint16 number,const QByteArray &raw,const QByteArray &payload);
    void tickTransfers();
    void sendPatternFrame(QTcpSocket *socket);
    void failTransfer(QTcpSocket *socket,const QString &message);
    void processUpload(QTcpSocket *socket,quint16 number,const QByteArray &raw,const QByteArray &payload);
    void finishUpload(QTcpSocket *socket);
    void failUpload(QTcpSocket *socket,const QString &message);
    void syncDevicePatternFiles();
    void identify(QTcpSocket *socket, const QString &code, const QString &name, const QString &model={});
    bool update(QTcpSocket *socket, const QJsonObject &values);
    Database *db_;
    QTcpServer server_;
    QHash<QTcpSocket*,Session> sessions_;
    struct Transfer { QString directId; qint64 taskId=0; QByteArray data; QString patternName; int chunkSize=256,totalFrames=0,index=0,phase=0;
        quint32 addr1=0,addr2=0; QDateTime deadline; };
    QHash<QTcpSocket*,Transfer> transfers_;
    struct DirectJob { qint64 ownerId=0,deviceId=0; int progress=0; QString status,message; QDateTime lease,started; };
    QHash<QString,DirectJob> directJobs_;
    QSet<QTcpSocket*> quietDisconnects_;
    void transferProgress(const Transfer &transfer,int progress,const QString &message);
    void quietDisconnect(QTcpSocket *socket);
    struct UploadTransfer { qint64 taskId=0; QJsonObject file; QMap<int,QByteArray> frames; int totalFrames=0;
        bool finished=false; QDateTime deadline,startedAt; };
    QHash<QTcpSocket*,UploadTransfer> uploads_;
    QTimer transferTimer_;
    QTimer patternSyncTimer_;
    struct ListRequest { QMap<int,QByteArray> frames; int totalFrames=0; QEventLoop *loop=nullptr; QString error; };
    QHash<QTcpSocket*,ListRequest> listRequests_;
    struct DeleteRequest { QEventLoop *loop=nullptr; int result=-1; QString error; };
    QHash<QTcpSocket*,DeleteRequest> deleteRequests_;
};
