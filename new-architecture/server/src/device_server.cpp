#include "device_server.h"
#include "database.h"
#include "alarm_catalog.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QTimeZone>
#include <QUuid>

namespace {
void put16(QByteArray &b,int offset,quint16 value) { b[offset]=char(value>>8); b[offset+1]=char(value); }
void put32(QByteArray &b,int offset,quint32 value) { b[offset]=char(value>>24); b[offset+1]=char(value>>16); b[offset+2]=char(value>>8); b[offset+3]=char(value); }
QString unicodeText(const QByteArray &bytes) {
    if(bytes.size()%2!=0)return {};
    QVector<char16_t> words;
    for(int i=0;i+1<bytes.size();i+=2){const char16_t word=char16_t(quint8(bytes[i])|(quint16(quint8(bytes[i+1]))<<8));if(word==0 || word==0xfdfd || word==0xffff)break;words.append(word);}
    return QString::fromUtf16(words.constData(),words.size()).trimmed();
}
QString text(const QByteArray &bytes) {
    if(bytes.isEmpty()) return {};
    const QByteArray utf8Bytes=bytes.left(bytes.indexOf('\0')>=0 ? bytes.indexOf('\0') : bytes.size());
    const QString utf8=QString::fromUtf8(utf8Bytes).trimmed();
    bool control=false;
    for(QChar character:utf8) if(character.isPrint()==false && !character.isSpace()) { control=true; break; }
    const bool likelyUtf16=bytes.size()%2==0 &&
        ((bytes.size()>1 && bytes[1]=='\0') || utf8.contains(QChar::ReplacementCharacter) || control);
    if(likelyUtf16) {
        const auto decoded=unicodeText(bytes);if(!decoded.isEmpty() && !decoded.contains(QChar::ReplacementCharacter))return decoded;
    }
    return utf8;
}
char bcd(int value) { return char(((value/10)<<4)|(value%10)); }
bool validBcdTime(const QByteArray &data) {
    if(data.size()<7) return false;
    for(int i=0;i<7;++i) if((quint8(data[i])>>4)>9 || (quint8(data[i])&15)>9) return false;
    return true;
}
QString stripPatternExtension(const QString &name) {
    const QString suffix=QFileInfo(name).suffix().toLower();
    if(QStringList{"slw","xdg","nsp","ntp","mtp","sdt","vdt","sdg","dst"}.contains(suffix))
        return name.left(name.size()-suffix.size()-1);
    return name;
}
QString patternText(const QByteArray &bytes) {
    const auto decoded=unicodeText(bytes);
    // Some UTF-16 Chinese byte pairs also form valid UTF-8; the file suffix disambiguates them.
    if(!decoded.isEmpty() && !decoded.contains(QChar::ReplacementCharacter) && stripPatternExtension(decoded)!=decoded)return decoded;
    return text(bytes);
}
QDateTime decodeBcdTime(const QByteArray &data) {
    if(!validBcdTime(data)) return {};
    auto value=[&](int index) { const int byte=quint8(data[index]); return (byte>>4)*10+(byte&15); };
    if(value(3)>7) return {};
    const QDate date(2000+value(0),value(1),value(2));
    const QTime time(value(4),value(5),value(6));
    return date.isValid() && time.isValid() ? QDateTime(date,time) : QDateTime{};
}
}

DeviceServer::DeviceServer(Database *db,QObject *parent):QObject(parent),db_(db) {
    connect(this,&DeviceServer::eventLogged,this,[this](const QString &line) {
        const QString enabled=db_->config("debug_output_enabled","true").trimmed().toLower();
        if(QStringList{"0","false","no","off"}.contains(enabled)) return;
        const QString level=line.contains("失败") || line.contains("超时") ? "error" :
            line.contains("断开") || line.contains("拒绝") ? "warn" : "info";
        db_->insert("debug_logs",{{"level",level},{"source","tcp"},{"message",line}},nullptr,nullptr);
    });
    transferTimer_.setInterval(500);
    connect(&transferTimer_,&QTimer::timeout,this,&DeviceServer::tickTransfers);
    patternSyncTimer_.setInterval(60000);
    connect(&patternSyncTimer_,&QTimer::timeout,this,&DeviceServer::syncDevicePatternFiles);
    connect(&server_,&QTcpServer::newConnection,this,[this] {
        while(QTcpSocket *socket=server_.nextPendingConnection()) {
            Session session; session.lastSeen=QDateTime::currentDateTimeUtc();
            session.probe=new QTimer(socket); session.probe->setInterval(5000);
            session.realtime=new QTimer(socket); session.realtime->setInterval(10000);
            connect(session.probe,&QTimer::timeout,socket,[this,socket] {
                if(!sessions_.contains(socket)) return;
                Session &s=sessions_[socket];
                if(s.deviceId==0) socket->write(frame(0x0B2A,0x0002));
                if(s.lastSeen.secsTo(QDateTime::currentDateTimeUtc())>120) socket->disconnectFromHost();
            });
            connect(session.realtime,&QTimer::timeout,socket,[this,socket] {
                if(sessions_.contains(socket) && sessions_[socket].deviceId>0)
                    socket->write(frame(0x0B29,0x0033));
            });
            sessions_.insert(socket,session); session.probe->start(); session.realtime->start();
            socket->write(frame(0x0B2A,0x0002));
            emit eventLogged("设备连接："+socket->peerAddress().toString());
            connect(socket,&QTcpSocket::readyRead,this,[this,socket]{ receive(socket); });
            connect(socket,&QTcpSocket::disconnected,this,[this,socket] {
                if(uploads_.contains(socket)) failUpload(socket,"设备断开，回传失败");
                if(deleteRequests_.contains(socket)) {
                    deleteRequests_[socket].error="设备连接已断开";
                    if(deleteRequests_[socket].loop) deleteRequests_[socket].loop->quit();
                }
                if(listRequests_.contains(socket)) {
                    listRequests_[socket].error="设备连接已断开";
                    if(listRequests_[socket].loop) listRequests_[socket].loop->quit();
                }
                if(transfers_.contains(socket)) failTransfer(socket,"设备断开，下发失败");
                const Session s=sessions_.take(socket);
                if(s.runtimeId>0) {
                    QSqlQuery runtime(db_->connection());
                    runtime.prepare("UPDATE device_runtime_sessions SET ended_at=CURRENT_TIMESTAMP,last_seen_at=CURRENT_TIMESTAMP,"
                                    "duration_seconds="+QString(db_->isMySql() ?
                                    "GREATEST(0,TIMESTAMPDIFF(SECOND,started_at,CURRENT_TIMESTAMP))" :
                                    "MAX(0,CAST((julianday('now')-julianday(started_at))*86400 AS INTEGER))")+
                                    ",end_reason='disconnect',updated_at=CURRENT_TIMESTAMP WHERE id=? AND ended_at IS NULL");
                    runtime.addBindValue(s.runtimeId); runtime.exec();
                }
                bool stillOnline=false;
                for(const Session &other:sessions_) if(other.deviceId==s.deviceId && s.deviceId>0) stillOnline=true;
                if(s.deviceId>0 && !stillOnline) {
                    QSqlQuery offline(db_->connection());offline.prepare("UPDATE devices SET status='offline',updated_at=CURRENT_TIMESTAMP WHERE id=? AND deleted_at IS NULL");offline.addBindValue(s.deviceId);offline.exec();
                }
                if(!quietDisconnects_.remove(socket))emit eventLogged("设备断开："+socket->peerAddress().toString()); socket->deleteLater();
            });
        }
    });
}

DeviceServer::~DeviceServer() {
    server_.close();for(auto *timer:findChildren<QTimer*>(QString(),Qt::FindDirectChildrenOnly))timer->stop();
    const auto sockets=sessions_.keys();for(auto *socket:sockets){socket->abort();QObject::disconnect(socket,nullptr,this,nullptr);}
    sessions_.clear();
}

bool DeviceServer::listen(quint16 port,QString *error) {
    if(server_.listen(QHostAddress::AnyIPv4,port)) {
        QSqlQuery unfinished(db_->connection());
        unfinished.exec("UPDATE download_tasks SET status='waiting',progress=0,message='等待下发' WHERE status='downloading' AND deleted_at IS NULL");
        unfinished.exec("UPDATE upload_tasks SET status='failed',message='服务重启中断回传，请重新提交' WHERE status='uploading' AND deleted_at IS NULL");
        QSqlQuery stale(db_->connection());
        stale.exec("UPDATE device_runtime_sessions SET ended_at=COALESCE(last_seen_at,CURRENT_TIMESTAMP),"
                   "duration_seconds="+QString(db_->isMySql() ?
                   "GREATEST(0,TIMESTAMPDIFF(SECOND,started_at,COALESCE(last_seen_at,CURRENT_TIMESTAMP)))" :
                   "MAX(0,CAST((julianday(COALESCE(last_seen_at,CURRENT_TIMESTAMP))-julianday(started_at))*86400 AS INTEGER))")+
                   ",end_reason='server_restart',updated_at=CURRENT_TIMESTAMP WHERE ended_at IS NULL");
        QSqlQuery q(db_->connection()); q.exec("UPDATE devices SET status='offline' WHERE status<>'offline' AND deleted_at IS NULL");
        transferTimer_.start();
        patternSyncTimer_.start();
        QTimer::singleShot(30000,this,&DeviceServer::syncDevicePatternFiles);
        return true;
    }
    if(error) *error=server_.errorString();
    return false;
}

void DeviceServer::disconnectDevice(qint64 deviceId) {
    QList<QTcpSocket*> matching;
    for(auto it=sessions_.cbegin();it!=sessions_.cend();++it)
        if(it.value().deviceId==deviceId) matching.append(it.key());
    for(QTcpSocket *socket:matching) socket->disconnectFromHost();
}

bool DeviceServer::deviceConnected(qint64 deviceId) const {
    for(const Session &session:sessions_) if(session.deviceId==deviceId) return true;
    return false;
}

bool DeviceServer::sendCurrentUser(qint64 deviceId,const QString &employeeCode,const QString &employeeName) {
    for(auto it=sessions_.cbegin();it!=sessions_.cend();++it) if(it.value().deviceId==deviceId) {
        QByteArray payload(27,0);
        const QString name=employeeName.trimmed();
        const QByteArray wireId=employeeCode.trimmed().toUtf8().left(11);
        for(int i=0;i<wireId.size();++i)payload[i]=wireId[i];
        for(int i=0;i<qMin(8,name.size());++i) {
            const ushort character=name[i].unicode();
            payload[11+i*2]=char(character&0xff);
            payload[12+i*2]=char(character>>8);
        }
        it.key()->write(frame(0x0B2A,0x00F1,payload));
        emit eventLogged("已向在线设备推送当前用户："+it.value().code);
        return true;
    }
    return false;
}

bool DeviceServer::refreshPatternList(qint64 deviceId,QString *error) {
    QTcpSocket *socket=nullptr;
    for(auto it=sessions_.cbegin();it!=sessions_.cend();++it)
        if(it.value().deviceId==deviceId) { socket=it.key(); break; }
    if(!socket) { if(error) *error="设备未开机连接"; return false; }
    if(transfers_.contains(socket) || uploads_.contains(socket) || listRequests_.contains(socket) || deleteRequests_.contains(socket)) {
        if(error) *error="设备当前忙碌"; return false;
    }
    QEventLoop loop;
    QTimer timeout; timeout.setSingleShot(true);
    connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
    ListRequest request; request.loop=&loop;
    listRequests_.insert(socket,request);
    socket->write(frame(0x0B2A,0x0009));
    timeout.start(8000);
    loop.exec();
    ListRequest result=listRequests_.take(socket);
    if(!result.error.isEmpty()) { if(error) *error=result.error; return false; }
    if(result.totalFrames<1 || result.frames.size()!=result.totalFrames) {
        if(error) *error="读取设备文件列表超时";
        return false;
    }
    QJsonArray records;
    for(int number=1;number<=result.totalFrames;++number) {
        if(!result.frames.contains(number)) { if(error) *error="设备文件列表缺少数据帧"; return false; }
        const QByteArray payload=result.frames.value(number);
        if(payload.isEmpty()) continue;
        const bool withId=payload.size()%46==0;
        const int entrySize=withId ? 46 : payload.size()%44==0 ? 44 : 0;
        if(entrySize==0) { if(error) *error="设备文件列表格式错误"; return false; }
        for(int offset=0;offset+entrySize<=payload.size();offset+=entrySize) {
            const int patternNo=withId ? u16(payload,offset) : 0;
            QString fileName=patternText(payload.mid(offset+(withId ? 2 : 0),44));
            if(fileName.isEmpty()) fileName="pattern_"+QString::number(patternNo);
            records.append(QJsonObject{{"deviceId",deviceId},{"patternNo",patternNo},{"fileName",fileName}});
        }
    }
    QSqlDatabase connection=db_->connection();
    if(!connection.transaction()) { if(error) *error=connection.lastError().text(); return false; }
    for(const QJsonValue &value:db_->rows("device_pattern_files","device_id=?",{deviceId},"id",10000)) {
        QString failure;
        if(!db_->remove("device_pattern_files",value.toObject().value("id").toInteger(),&failure)) {
            connection.rollback(); if(error) *error=failure; return false;
        }
    }
    for(const QJsonValue &value:records) {
        QString failure;
        if(!db_->insert("device_pattern_files",value.toObject(),nullptr,&failure)) {
            connection.rollback(); if(error) *error=failure; return false;
        }
    }
    if(!connection.commit()) { if(error) *error=connection.lastError().text(); return false; }
    return true;
}

void DeviceServer::syncDevicePatternFiles() {
    QSet<qint64> ids;
    for(auto it=sessions_.cbegin();it!=sessions_.cend();++it)
        if(it.value().deviceId>0 && !transfers_.contains(it.key()) && !uploads_.contains(it.key()) &&
           !listRequests_.contains(it.key()) && !deleteRequests_.contains(it.key()))
            ids.insert(it.value().deviceId);
    for(qint64 id:ids) {
        QString error;
        if(!refreshPatternList(id,&error) && error!="设备当前忙碌" && error!="设备未开机连接")
            emit eventLogged("设备花型列表自动刷新失败："+error);
    }
}

bool DeviceServer::deletePatternFile(qint64 deviceId,int patternNo,const QString &name,QString *error) {
    if(db_->count("device_pattern_files","device_id=? AND lower(file_name)=lower(?)",{deviceId,name})>1){if(error)*error="设备存在同名文件，请在设备端确认后删除";return false;}
    QTcpSocket *socket=nullptr;
    for(auto it=sessions_.cbegin();it!=sessions_.cend();++it)
        if(it.value().deviceId==deviceId) { socket=it.key(); break; }
    if(!socket) { if(error) *error="设备未开机连接"; return false; }
    if(transfers_.contains(socket) || uploads_.contains(socket) || listRequests_.contains(socket) || deleteRequests_.contains(socket)) {
        if(error) *error="设备当前忙碌"; return false;
    }
    QByteArray unicode;
    for(QChar character:name) { const ushort code=character.unicode(); unicode.append(char(code&0xff)); unicode.append(char(code>>8)); }
    QVector<QByteArray> attempts;
    if(!unicode.isEmpty()) attempts.append(unicode);
    QByteArray numbered(2,0); put16(numbered,0,quint16(patternNo));
    if(!unicode.isEmpty()) attempts.append(numbered+unicode);
    QByteArray fixed=unicode.leftJustified(44,char(0),true);
    if(!unicode.isEmpty()) { attempts.append(numbered+fixed); attempts.append(fixed); }
    QByteArray compatible=name.toUtf8().leftJustified(44,char(0),true);
    if(!name.isEmpty()) attempts.append(compatible);
    for(const QByteArray &payload:attempts) {
        QEventLoop loop;
        QTimer timeout; timeout.setSingleShot(true);
        connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
        DeleteRequest request; request.loop=&loop;
        deleteRequests_.insert(socket,request);
        socket->write(frame(0x0B2A,0x0008,payload));
        timeout.start(8000);
        loop.exec();
        const DeleteRequest result=deleteRequests_.take(socket);
        if(!result.error.isEmpty()) { if(error) *error=result.error; return false; }
        if(result.result==0) return true;
        if(result.result<0) { if(error) *error="设备删除应答超时"; return false; }
    }
    if(error) *error="设备拒绝删除文件";
    return false;
}

quint16 DeviceServer::u16(const QByteArray &b,int offset) { return (quint8(b[offset])<<8)|quint8(b[offset+1]); }
quint32 DeviceServer::u32(const QByteArray &b,int offset) { return (quint32(u16(b,offset))<<16)|u16(b,offset+2); }
quint16 DeviceServer::crc(const QByteArray &bytes) {
    quint16 result=0xffff;
    for(unsigned char byte:bytes) {
        result^=byte;
        for(int i=0;i<8;++i) result=(result&1) ? (result>>1)^0xA001 : result>>1;
    }
    return result;
}
QByteArray DeviceServer::frame(quint16 type,quint16 number,const QByteArray &payload,
                               quint8 totalFrames,quint8 frameNo,quint32 addr1,quint32 addr2) {
    QByteArray bytes(23+payload.size()+2,0);
    bytes[0]='D'; bytes[1]='T'; put32(bytes,2,addr1); put32(bytes,6,addr2); put16(bytes,13,type); put16(bytes,15,number);
    bytes[17]=char(totalFrames); bytes[18]=char(frameNo); put32(bytes,19,payload.size()+2);
    for(int i=0;i<payload.size();++i) bytes[23+i]=payload[i];
    put16(bytes,23+payload.size(),crc(bytes.left(23+payload.size())));
    return bytes;
}

void DeviceServer::receive(QTcpSocket *socket) {
    if(!sessions_.contains(socket)) return;
    sessions_[socket].buffer+=socket->readAll();
    if(sessions_[socket].buffer.size()>1024*1024) { socket->disconnectFromHost(); return; }
    while(sessions_.contains(socket) && sessions_.value(socket).buffer.size()>=25) {
        Session &s=sessions_[socket];
        const int at=s.buffer.indexOf("DT");
        if(at<0) { s.buffer=s.buffer.endsWith('D') ? QByteArray("D") : QByteArray(); break; }
        if(at>0) s.buffer.remove(0,at);
        if(s.buffer.size()<25) break;
        const quint32 length=u32(s.buffer,19);
        if(length<2 || length>65538) { s.buffer.remove(0,2); continue; }
        if(s.buffer.size()<23+int(length)) break;
        const QByteArray raw=s.buffer.left(23+length); s.buffer.remove(0,raw.size());
        if(u16(raw,raw.size()-2)!=crc(raw.left(raw.size()-2))) { if(!transfers_.contains(socket) || transfers_[socket].directId.isEmpty())emit eventLogged("设备数据 CRC 校验失败"); continue; }
        s.lastSeen=QDateTime::currentDateTimeUtc();
        if(s.runtimeId>0) {
            QSqlQuery runtime(db_->connection());
            runtime.prepare("UPDATE device_runtime_sessions SET last_seen_at=CURRENT_TIMESTAMP WHERE id=? AND ended_at IS NULL");
            runtime.addBindValue(s.runtimeId); runtime.exec();
        }
        const quint16 type=u16(raw,13), number=u16(raw,15);
        if(type==0x0B2A && (number==0x0004 || number==0x0005 || number==0x0006 || number==0x0007 || number==0x0008 || number==0x0009 || number==0x000D || number==0x000E || number==0x000F))
            processPattern(socket,number,raw,raw.mid(23,length-2));
        else process(socket,type,number,raw.mid(23,length-2));
        if(sessions_.contains(socket) && sessions_.value(socket).deviceId>0) update(socket,{{"lastProtocolAt",QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")}});
    }
}

void DeviceServer::identify(QTcpSocket *socket,const QString &code,const QString &name,const QString &model) {
    if(code.isEmpty()) return;
    Session &s=sessions_[socket];
    qint64 id=0;
    if(!s.mainboardSn.isEmpty()) {
        const QJsonArray matched=db_->rows("devices","mainboard_sn=?",{s.mainboardSn},"id",1);
        if(!matched.isEmpty()) id=matched.first().toObject().value("id").toInteger();
    }
    QSet<qint64> occupied;for(auto it=sessions_.cbegin();it!=sessions_.cend();++it)if(it.key()!=socket && it.key()->state()!=QAbstractSocket::UnconnectedState)occupied.insert(it.value().deviceId);auto pendingInOtherSession=[&](qint64 candidate){return occupied.contains(candidate);};
    // A reported stable code must resolve a pre-registered device before the temporary session row.
    if(id==0 && !code.startsWith("PENDING-")){const auto matched=db_->rows("devices","code=?",{code},"id",1);if(!matched.isEmpty())id=matched.first().toObject().value("id").toInteger();}
    if(id==0 && s.deviceId>0 && !db_->row("devices",s.deviceId).isEmpty())id=s.deviceId;
    if(id==0 && code.startsWith("PENDING-")){const auto matched=db_->rows("devices","code=?",{code},"id",1);if(!matched.isEmpty()){const auto candidate=matched.first().toObject().value("id").toInteger();if(!pendingInOtherSession(candidate))id=candidate;}}
    if(id==0){const auto matched=db_->rows("devices","ip=? AND code LIKE 'PENDING-%'",{socket->peerAddress().toString()},"id",10000);for(const auto &value:matched){const auto candidate=value.toObject().value("id").toInteger();if(!pendingInOtherSession(candidate)){id=candidate;break;}}}
    if(id>0 && s.deviceId>0 && id!=s.deviceId) {
        QSqlDatabase connection=db_->connection();
        const QJsonObject source=db_->row("devices",s.deviceId), target=db_->row("devices",id);
        bool merged=connection.transaction();
        if(merged && target.value("groupId").isNull() && !source.value("groupId").isNull())
            merged=db_->update("devices",id,{{"groupId",source.value("groupId")}},nullptr);
        for(const QString &table:{"device_pattern_files","upload_tasks","download_tasks",
                                   "production_records","alarm_records","salary_records",
                                   "employee_devices","device_runtime_sessions"}) {
            if(!merged) break;
            QSqlQuery move(connection);
            move.prepare("UPDATE "+table+" SET device_id=?,updated_at=CURRENT_TIMESTAMP WHERE device_id=?");
            move.addBindValue(id); move.addBindValue(s.deviceId);
            merged=move.exec();
        }
        if(merged) merged=db_->remove("devices",s.deviceId,nullptr);
        if(merged) merged=connection.commit();
        if(!merged) { connection.rollback(); id=s.deviceId; }
    }
    const QJsonObject existing=id>0 ? db_->row("devices",id) : QJsonObject{};
    QJsonObject body{{"ip",socket->peerAddress().toString()},
        {"status","idle"},{"lastOnline",QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")}};
    const QString protocolName=name.trimmed();
    if(id==0) {
        body["code"]=code;
        body["name"]=protocolName.isEmpty() ? "设备"+code : protocolName;
        body["initialName"]=body.value("name");
    } else {
        const QString existingCode=existing.value("code").toString().trimmed();
        const bool replaceCode=existingCode.isEmpty() || existingCode.startsWith("PENDING-") ||
            (!s.mainboardSn.isEmpty() && existingCode==s.mainboardSn);
        if(replaceCode && !code.startsWith("PENDING-") && existingCode!=code &&
           db_->count("devices","code=?",{code})==0)
            body["code"]=code;
        const QString current=existing.value("name").toString().trimmed();
        const QString initial=existing.value("initialName").toString().trimmed();
        const QString oldCode=existing.value("code").toString().trimmed();
        const bool placeholder=current.isEmpty() || current.startsWith("待识别设备") ||
            current=="设备 "+oldCode || current=="设备"+oldCode ||
            (!s.mainboardSn.isEmpty() && current=="设备 "+s.mainboardSn);
        if(!protocolName.isEmpty() && (placeholder || current==initial)) body["name"]=protocolName;
        if(!protocolName.isEmpty() && initial!=protocolName) body["initialName"]=protocolName;
    }
    if(!s.mainboardSn.isEmpty()) { body["mainboardSn"]=s.mainboardSn; body["identifiedBy"]="mainboard"; }
    if(!model.isEmpty()) body["modelName"]=model;
    QString error;
    if(id>0) db_->update("devices",id,body,&error);
    else db_->insert("devices",body,&id,&error);
    if(id>0) {
        if(s.runtimeId>0 && s.deviceId!=id) {
            QSqlQuery runtime(db_->connection()); runtime.prepare("UPDATE device_runtime_sessions SET device_id=? WHERE id=?");
            runtime.addBindValue(id); runtime.addBindValue(s.runtimeId); runtime.exec();
        }
        s.deviceId=id; s.code=code;
        if(s.runtimeId==0) {
            QSqlQuery runtime(db_->connection());
            if(runtime.exec("INSERT INTO device_runtime_sessions(device_id,started_at,last_seen_at) VALUES("+QString::number(id)+",CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)"))
                s.runtimeId=runtime.lastInsertId().toLongLong();
        }
        emit eventLogged("设备已识别："+code);
        QList<QTcpSocket*> replacements;
        for(auto it=sessions_.cbegin();it!=sessions_.cend();++it)
            if(it.key()!=socket && it.value().deviceId==id) replacements.append(it.key());
        for(QTcpSocket *older:replacements) older->disconnectFromHost();
    }
    else emit eventLogged("设备保存失败："+error);
}

bool DeviceServer::update(QTcpSocket *socket,const QJsonObject &values) {
    if(!sessions_.contains(socket)) return false;
    const qint64 id=sessions_[socket].deviceId;
    return id>0 && db_->update("devices",id,values,nullptr);
}

void DeviceServer::process(QTcpSocket *socket,quint16 type,quint16 number,const QByteArray &payload) {
    Session &s=sessions_[socket];
    if(type==0x0B2A && (number==0x000C || number==0x000B)) {
        handleProduction(socket,number,payload); return;
    }
    if(type==0x0B2A && number==0x0003) {
        if(!validBcdTime(payload)) {
            const QDateTime now=QDateTime::currentDateTime();
            const QDate day=now.date(); const QTime clock=now.time();
            QByteArray answer;
            answer.append(bcd(day.year()%100)); answer.append(bcd(day.month())); answer.append(bcd(day.day()));
            answer.append(bcd(day.dayOfWeek())); answer.append(bcd(clock.hour())); answer.append(bcd(clock.minute()));
            answer.append(bcd(clock.second())); answer.append(char(0));
            socket->write(frame(type,number,answer));
        }
        return;
    }
    if(type==0x0B2A && (number==0x0002 || number==0x0001)) {
        if(s.deviceId==0) {
            QString code="PENDING-"+socket->peerAddress().toString().replace('.','-')+"-"+QString::number(socket->peerPort());
            identify(socket,code,"待识别设备 "+socket->peerAddress().toString());
        }
        QString status="idle";
        const auto current=sessions_.value(socket);if(current.deviceId>0 && db_->row("devices",current.deviceId).value("status").toString()=="working") status="working";
        update(socket,{{"lastOnline",QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")},{"status",status}});
        socket->write(frame(type,number)); return;
    }
    if(type==0x1302 && number==0x10FA && payload.size()>=8) {
        QString code=QString::number(u32(payload,4));
        if(code=="0") code=s.code;
        identify(socket,code,text(payload.mid(8)),QString::number(u32(payload,0)));
        return;
    }
    if(type==0x1302 && number==0x157C) {
        const QString sn=text(payload);
        if(!sn.isEmpty()) {
            s.mainboardSn=sn;
            identify(socket,s.code.isEmpty() || s.code.startsWith("PENDING-") ? sn : s.code,
                     s.deviceId==0 ? "设备 "+sn : QString());
        }
        update(socket,{{"mainboardSn",sn},{"identifiedBy","mainboard"}}); return;
    }
    if(s.deviceId==0) return;
    if(type==0x0B2A && number==0x00F1) {
        if(payload.isEmpty()) emit eventLogged("设备更新当前用户应答缺少结果："+s.code);
        else if(quint8(payload.back())==0) emit eventLogged("设备已接受当前用户更新："+s.code);
        else emit eventLogged(QString("设备拒绝当前用户更新：%1，结果%2").arg(s.code).arg(quint8(payload.back())));
        return;
    }
    if(type==0x0B2A && number==0x00F2) {
        const QString employeeCode=text(payload.left(11));
        QSqlQuery employee(db_->connection());
        employee.prepare("SELECT code,name FROM employees WHERE lower(code)=lower(?) AND deleted_at IS NULL LIMIT 1");
        employee.addBindValue(employeeCode);
        const bool accepted=!employeeCode.isEmpty() && employee.exec() && employee.next();
        if(accepted)update(socket,{{"employeeCode",employee.value(0).toString()},{"employeeName",employee.value(1).toString()}});
        socket->write(frame(type,number,QByteArray(1,accepted ? char(0) : char(1))));
        return;
    }
    if(type==0x0B29 && number==0x0032 && !payload.isEmpty()) update(socket,{{"status",quint8(payload[0])==1 ? "working" : "idle"}});
    else if(type==0x0B29 && number==0x0033 && payload.size()>=3) {
        QJsonObject values{{"currentPatternNo",int(u16(payload,1))},{"currentPatternName",text(payload.mid(3))}};
        if(quint8(payload[0])<=1) values["status"]=quint8(payload[0])==1 ? "working" : "idle";
        update(socket,values);
    }
    else if(type==0x0B29 && number==0x0015 && payload.size()>=8) update(socket,{{"sewingRangeX",int(u32(payload,0))},{"sewingRangeY",int(u32(payload,4))}});
    else if(type==0x0B29 && number==0x0016 && payload.size()>=2) update(socket,{{"currentPatternNo",int(u16(payload,0))},{"currentPatternName",text(payload.mid(2))}});
    else if(type==0x0B29 && number==0x0003 && payload.size()>=8) update(socket,{{"productionTotal",int(u32(payload,0))},{"productionCurrent",int(u32(payload,4))}});
    else if(type==0x0B29 && number==0x0006 && payload.size()>=8) update(socket,{{"bottomThreadTotal",int(u32(payload,0))},{"bottomThreadRemain",int(u32(payload,4))}});
    else if(type==0x1302 && number==0x107C && payload.size()>=2) {
        QJsonObject values{{"currentSpeed",int(u16(payload,0))}};
        if(u16(payload,0)>0) values["status"]="working";
        update(socket,values);
    }
    else if(type==0x1301 && number==0x00A3 && payload.size()>=2) update(socket,{{"maxSpeedValue",int(u16(payload,0))}});
    else if(type==0x0B01 && number==0x001F && payload.size()>=4) update(socket,{{"needleCountValue",int(u32(payload,0))}});
    else if(type==0x0B29 && (number==0x0035 || number==0x0036 || number==0x0037 || number==0x0038) && payload.size()>=2) {
        update(socket,{{number==0x0035 || number==0x0036 ? "highPointValue" : "lowPointValue",int(u16(payload,0))}});
        if(number==0x0036 || number==0x0038) socket->write(frame(type,number,QByteArray(1,char(0))));
    }
    else if(type==0x0B29 && number==0x0039) {
        if(payload.size()>=2) update(socket,{{"currentSpeed",int(u16(payload,0))}});
        socket->write(frame(type,number,QByteArray(1,char(0))));
    }
    else if(type==0x0B29 && number==0x0034) emit eventLogged("设备剪线完成："+s.code);
    else if(type==0x0B97 && number==0x0002 && payload.size()>=3)
        emit eventLogged(QString("设备空闲报警：%1，分钟%2，状态%3")
            .arg(s.code).arg(u16(payload,0)).arg(quint8(payload[2])));
    else if(type==0x0B29 && number==0x0029 && !payload.isEmpty())
        emit eventLogged(QString("设备加油提示：%1，提示%2").arg(s.code).arg(quint8(payload[0])));
    else if(type==0x0B97 && number==0x0001 && payload.size()>=2) {
        const quint16 rawCode=u16(payload,0);
        if(rawCode>0) {
            const auto alarm=describeAlarm(rawCode);
            update(socket,{{"alarmCode",alarm.first}});
            db_->insert("alarm_records",{{"deviceId",s.deviceId},{"alarmCode",alarm.first},
                {"alarmType",alarm.second},{"description",alarm.second},
                {"status","pending"},{"startTime",QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")}},nullptr,nullptr);
        } else {
            update(socket,{{"alarmCode",""}});
            QSqlQuery resolved(db_->connection());
            resolved.prepare("UPDATE alarm_records SET status='resolved',end_time=CURRENT_TIMESTAMP,"
                             "duration="+QString(db_->isMySql() ?
                             "GREATEST(0,TIMESTAMPDIFF(SECOND,start_time,CURRENT_TIMESTAMP))" :
                             "MAX(0,CAST((julianday('now')-julianday(start_time))*86400 AS INTEGER))")+
                             ",updated_at=CURRENT_TIMESTAMP "
                             "WHERE device_id=? AND status='pending' AND deleted_at IS NULL");
            resolved.addBindValue(s.deviceId); resolved.exec();
        }
    }
}

void DeviceServer::handleProduction(QTcpSocket *socket,quint16 number,const QByteArray &payload) {
    if(!sessions_.contains(socket)) return;
    const bool modern=number==0x000C;
    if(payload.size()<(modern ? 82 : 30)) return;
    const int nameEnd=modern ? 50 : payload.size()-24;
    const QDateTime start=decodeBcdTime(payload.mid(nameEnd,7));
    const QDateTime end=decodeBcdTime(payload.mid(nameEnd+11,7));
    if(!start.isValid() || !end.isValid()) return;
    const quint32 rawDevice=u32(payload,0);
    const int patternNo=u16(payload,4);
    const QString rawName=patternText(payload.mid(6,nameEnd-6));
    const quint32 startNeedle=u32(payload,nameEnd+7), endNeedle=u32(payload,nameEnd+18);
    QString protocolUser;
    int stopReason=0;
    if(modern) {
        const QString userBefore=text(payload.mid(72,8)), userAfter=text(payload.mid(74,8));
        const int stopAfter=u16(payload,80), stopBefore=u16(payload,72);
        const bool reverse=!userAfter.isEmpty() && (userBefore.isEmpty() || (stopAfter>1000 && stopBefore<=255));
        protocolUser=reverse ? userAfter : userBefore;
        stopReason=reverse ? stopBefore : stopAfter;
    } else stopReason=u16(payload,nameEnd+22);
    socket->write(frame(0x0B2A,number,QByteArray(1,char(0))));
    Session session=sessions_.value(socket);
    if(session.deviceId==0 && rawDevice>0){identify(socket,QString::number(rawDevice),"设备"+QString::number(rawDevice));session=sessions_.value(socket);}
    if(session.deviceId==0) return;
    // Historical reports do not overwrite the current pattern. Employee binding keeps its existing behavior pending confirmation.

    const QJsonObject device=db_->row("devices",session.deviceId);
    QString employeeCode=protocolUser.isEmpty() ? device.value("employeeCode").toString() : protocolUser;
    QJsonObject employee;
    if(!employeeCode.isEmpty()) {
        QSqlQuery employeeQuery(db_->connection());
        employeeQuery.prepare("SELECT id FROM employees WHERE (lower(code)=lower(?) OR id=?) AND deleted_at IS NULL ORDER BY id LIMIT 1");
        employeeQuery.addBindValue(employeeCode); employeeQuery.addBindValue(employeeCode.toLongLong());
        if(employeeQuery.exec() && employeeQuery.next()) employee=db_->row("employees",employeeQuery.value(0).toLongLong());
    }
    QJsonObject pattern;
    if(!rawName.isEmpty()) {
        const auto matches=db_->rows("patterns","lower(name)=lower(?) OR lower(file_name)=lower(?)",
                                     {rawName,rawName},"id DESC",1);
        if(!matches.isEmpty()) pattern=matches.first().toObject();
        else if(stripPatternExtension(rawName)!=rawName) {
            const auto byName=db_->rows("patterns","lower(name)=lower(?)",{stripPatternExtension(rawName)},"id DESC",1);
            if(!byName.isEmpty())pattern=byName.first().toObject();
        }
    }
    QString patternName=rawName;
    if(!pattern.isEmpty()) patternName=pattern.value("name").toString();
    if(patternName.isEmpty()) patternName="花型#"+QString::number(patternNo);
    const double unitPrice=pattern.value("unitPrice").toDouble();
    const QString orderNo=pattern.value("orderNo").toString();
    const QStringList keyParts={QString::number(session.deviceId),QString::number(patternNo),rawName,
        QString::number(startNeedle),QString::number(endNeedle),QString::number(stopReason),
        start.toUTC().toString("yyyy-MM-ddTHH:mm:ssZ"),end.toUTC().toString("yyyy-MM-ddTHH:mm:ssZ")};
    const QString sourceKey=QString::fromLatin1(QCryptographicHash::hash(keyParts.join('|').toUtf8(),QCryptographicHash::Sha1).toHex());
    auto legacyParts=keyParts;legacyParts[2]=text(payload.mid(6,nameEnd-6));
    const QString legacyKey=QString::fromLatin1(QCryptographicHash::hash(legacyParts.join('|').toUtf8(),QCryptographicHash::Sha1).toHex());
    if(db_->count("production_records","source_key=? OR source_key=?",{sourceKey,legacyKey})>0) return;
    const QDateTime recordTime=end.isValid() ? end : start;
    const double hours=qMax(0.0,start.secsTo(end)/3600.0);
    QSqlDatabase connection=db_->connection();
    if(!connection.transaction()) { emit eventLogged("生产记录事务启动失败："+connection.lastError().text()); return; }
    qint64 productionId=0; QString error;
    const QJsonObject record{{"deviceId",session.deviceId},{"employeeId",employee.value("id").toInteger()},
        {"patternId",pattern.value("id").toInteger()},{"patternNo",patternNo},{"patternName",patternName},
        {"protocolUserId",protocolUser},{"pieces",1},{"stitches",qMax(0,qint64(endNeedle)-qint64(startNeedle))},
        {"runningTime",hours},{"idleTime",0},{"startTime",start.toString("yyyy-MM-dd HH:mm:ss")},
        {"endTime",end.toString("yyyy-MM-dd HH:mm:ss")},{"startNeedle",qint64(startNeedle)},
        {"endNeedle",qint64(endNeedle)},{"stopReason",stopReason},{"unitPrice",unitPrice},
        {"orderNo",orderNo},{"sourceKey",sourceKey},{"recordDate",recordTime.toString("yyyy-MM-dd HH:mm:ss")}};
    if(!db_->insert("production_records",record,&productionId,&error)) {
        connection.rollback(); emit eventLogged("生产记录保存失败："+error); return;
    }
    if(!employee.isEmpty()) {
        const double amount=qRound64(unitPrice*100.0)/100.0;
        if(!db_->insert("salary_records",{{"sourceProductionId",productionId},{"employeeId",employee.value("id")},
            {"deviceId",session.deviceId},{"patternId",pattern.value("id")},{"patternName",patternName},
            {"orderNo",orderNo},{"pieces",1},{"unitPrice",unitPrice},{"salary",amount},
            {"bonus",0},{"totalAmount",amount},{"recordDate",recordTime.toString("yyyy-MM-dd HH:mm:ss")}},nullptr,&error)) {
            connection.rollback(); emit eventLogged("工资记录保存失败："+error); return;
        }
    }
    if(!employee.isEmpty())update(socket,{{"employeeCode",employee.value("code")},{"employeeName",employee.value("name")}});
    if(!connection.commit()) { emit eventLogged("生产记录事务提交失败："+connection.lastError().text()); return; }
    emit eventLogged("生产记录已保存：设备"+session.code+"，花型"+patternName);
}

QString DeviceServer::startDirectTransfer(qint64 deviceId,qint64 ownerId,const QString &name,const QByteArray &data,QString *error) {
    auto reject=[error](const QString &message){if(error)*error=message;return QString();};
    if(data.isEmpty() || data.size()>255*65536 || name.isEmpty() || name.size()>22 || name.contains('/') || name.contains('\\'))return reject("花型文件或名称不符合设备协议要求");
    for(QChar c:name)if(!c.isPrint())return reject("花型名称包含非法字符");
    const auto device=db_->row("devices",deviceId);
    if(device.isEmpty())return reject("设备不存在");
    if(device.value("status").toString()=="working")return reject("设备缝纫中，请空闲后重试");
    QTcpSocket *socket=nullptr;
    for(auto it=sessions_.cbegin();it!=sessions_.cend();++it)if(it->deviceId==deviceId && it.key()->state()==QAbstractSocket::ConnectedState){socket=it.key();break;}
    if(!socket)return reject("设备未连接，请开机后重试");
    if(transfers_.contains(socket) || uploads_.contains(socket) || listRequests_.contains(socket) || deleteRequests_.contains(socket))return reject("设备正在传输，请稍后重试");
    qint64 bytes=0;int active=0;for(const auto &t:transfers_)if(!t.directId.isEmpty()){bytes+=t.data.size();++active;}
    if(active>=8 || bytes+data.size()>64*1024*1024 || directJobs_.size()>=128)return reject("临时传输繁忙，请稍后重试");
    const QString id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto now=QDateTime::currentDateTimeUtc();
    directJobs_.insert(id,{ownerId,deviceId,2,"downloading","等待设备确认下载指令",now.addSecs(60),now});
    Transfer transfer;transfer.directId=id;transfer.data=data;transfer.patternName=name;
    transfer.chunkSize=qMax(256,((data.size()+255*256-1)/(255*256))*256);
    transfer.totalFrames=(data.size()+transfer.chunkSize-1)/transfer.chunkSize;transfer.deadline=now.addSecs(60);
    transfers_.insert(socket,transfer);
    QByteArray encoded;for(QChar c:name){encoded.append(char(c.unicode()&255));encoded.append(char(c.unicode()>>8));}
    socket->write(frame(0x0B2A,0x0004,encoded));
    return id;
}
QJsonObject DeviceServer::directTransfer(const QString &id,qint64 ownerId) {
    auto it=directJobs_.find(id);if(it==directJobs_.end() || it->ownerId!=ownerId)return {};
    if(it->status=="downloading")it->lease=QDateTime::currentDateTimeUtc().addSecs(60);
    return {{"id",id},{"deviceId",it->deviceId},{"status",it->status},{"progress",it->progress},{"message",it->message}};
}
void DeviceServer::quietDisconnect(QTcpSocket *socket){quietDisconnects_.insert(socket);socket->disconnectFromHost();}
bool DeviceServer::cancelDirectTransfer(const QString &id,qint64 ownerId) {
    auto it=directJobs_.find(id);if(it==directJobs_.end() || it->ownerId!=ownerId)return false;
    for(auto *socket:transfers_.keys())if(transfers_[socket].directId==id){transfers_.remove(socket);quietDisconnect(socket);break;}
    directJobs_.erase(it);return true;
}
void DeviceServer::transferProgress(const Transfer &transfer,int progress,const QString &message) {
    if(transfer.directId.isEmpty())db_->update("download_tasks",transfer.taskId,{{"progress",progress},{"message",message}},nullptr);
    else if(directJobs_.contains(transfer.directId)){auto &job=directJobs_[transfer.directId];job.progress=progress;job.message=message;}
}
void DeviceServer::failTransfer(QTcpSocket *socket,const QString &message) {
    if(!transfers_.contains(socket)) return;
    const Transfer transfer=transfers_.take(socket);
    if(!transfer.directId.isEmpty()) {
        if(directJobs_.contains(transfer.directId)){auto &job=directJobs_[transfer.directId];job.status="failed";job.message=message;job.lease=QDateTime::currentDateTimeUtc().addSecs(60);}
        quietDisconnects_.insert(socket);return;
    }
    const QJsonObject task=db_->row("download_tasks",transfer.taskId);
    if(task.value("status").toString()=="downloading")db_->update("download_tasks",transfer.taskId,{{"status","failed"},{"message",message}},nullptr);
    emit eventLogged("花型下发失败："+message);
}

void DeviceServer::sendPatternFrame(QTcpSocket *socket) {
    if(!transfers_.contains(socket)) return;
    Transfer &transfer=transfers_[socket];
    if(transfer.index<0 || transfer.index>=transfer.totalFrames) return;
    const QByteArray chunk=transfer.data.mid(transfer.index*transfer.chunkSize,transfer.chunkSize);
    socket->write(frame(0x0B2A,0x0005,chunk,quint8(transfer.totalFrames),quint8(transfer.index+1),
                        transfer.addr1,transfer.addr2));
    transfer.phase=1;
    transfer.deadline=QDateTime::currentDateTimeUtc().addSecs(60);
    transferProgress(transfer,5+int(double(transfer.index)*80.0/transfer.totalFrames),QString("正在下发 %1/%2").arg(transfer.index+1).arg(transfer.totalFrames));
}

void DeviceServer::processPattern(QTcpSocket *socket,quint16 number,const QByteArray &raw,const QByteArray &payload) {
    if(uploads_.contains(socket) && (number==0x0007 || number==0x000D || number==0x000E)) {
        processUpload(socket,number,raw,payload);
        return;
    }
    if(deleteRequests_.contains(socket) && number==0x0008 && !payload.isEmpty()) {
        DeleteRequest &request=deleteRequests_[socket];
        request.result=quint8(payload.back());
        if(request.loop) request.loop->quit();
        return;
    }
    if(listRequests_.contains(socket) && (number==0x0009 || number==0x000F)) {
        ListRequest &request=listRequests_[socket];
        const int total=qMax(1,int(quint8(raw[17]))), index=qMax(1,int(quint8(raw[18])));
        if(request.totalFrames==0) request.totalFrames=total;
        if(total!=request.totalFrames || index>total) {
            request.error="设备文件列表帧号不一致";
            if(request.loop) request.loop->quit();
            return;
        }
        request.frames[index]=payload;
        if(request.frames.size()==request.totalFrames && request.loop) request.loop->quit();
        return;
    }
    if(!transfers_.contains(socket)) return;
    // An HTTP pause/cancel may have committed since the last 500 ms worker tick.
    // A late ACK must never advance a task that is no longer downloading.
    if(transfers_.value(socket).directId.isEmpty() && db_->row("download_tasks",transfers_.value(socket).taskId).value("status").toString()!="downloading") {
        transfers_.remove(socket); socket->disconnectFromHost(); return;
    }
    Transfer &transfer=transfers_[socket];
    if(transfer.phase==0 && number==0x0004) {
        transfer.addr1=u32(raw,2); transfer.addr2=u32(raw,6);
        sendPatternFrame(socket);
        return;
    }
    if(transfer.phase==1 && number==0x000E && payload.size()>=2 && quint8(payload[0])==2) {
        const int requested=qBound(1,int(quint8(payload[1])),transfer.totalFrames);
        const QByteArray chunk=transfer.data.mid((requested-1)*transfer.chunkSize,transfer.chunkSize);
        socket->write(frame(0x0B2A,0x0005,chunk,quint8(transfer.totalFrames),quint8(requested),
                            transfer.addr1,transfer.addr2));
        transfer.deadline=QDateTime::currentDateTimeUtc().addSecs(60);
        return;
    }
    if(transfer.phase==1 && number==0x0006 && !payload.isEmpty()) {
        if(quint8(payload[0])!=0) {
            failTransfer(socket,QString("设备拒绝第%1帧").arg(transfer.index+1));
            socket->disconnectFromHost();
            return;
        }
        ++transfer.index;
        if(transfer.index<transfer.totalFrames) sendPatternFrame(socket);
        else {
            transfer.phase=2;
            transfer.deadline=QDateTime::currentDateTimeUtc().addSecs(60);
            transferProgress(transfer,95,"等待设备完成写入");
        }
        return;
    }
    if(transfer.phase==2 && number==0x0005 && !payload.isEmpty()) {
        if(quint8(payload.back())!=0) {
            failTransfer(socket,"设备写入花型失败");
            socket->disconnectFromHost();
            return;
        }
        if(payload.size()>1) {
            const QString echoed=patternText(payload.left(payload.size()-1));
            if(!echoed.isEmpty() &&
               stripPatternExtension(echoed)!=stripPatternExtension(transfer.patternName) &&
               stripPatternExtension(unicodeText(payload.left(payload.size()-1)))!=stripPatternExtension(transfer.patternName)) {
                failTransfer(socket,"设备返回的花型名称与当前任务不一致");
                socket->disconnectFromHost();
                return;
            }
        }
        const qint64 taskId=transfer.taskId;const QString directId=transfer.directId;
        transfers_.remove(socket);
        if(!directId.isEmpty()){
            if(directJobs_.contains(directId)){auto &job=directJobs_[directId];job.status="completed";job.progress=100;job.message="下发完成";job.lease=QDateTime::currentDateTimeUtc().addSecs(60);}
            return;
        }
        db_->update("download_tasks",taskId,{{"status","completed"},{"progress",100},{"message","下发完成"}},nullptr);
        emit eventLogged("花型下发完成：任务"+QString::number(taskId));
    }
}

void DeviceServer::failUpload(QTcpSocket *socket,const QString &message) {
    if(!uploads_.contains(socket)) return;
    const qint64 taskId=uploads_.take(socket).taskId;
    if(db_->row("upload_tasks",taskId).value("status").toString()=="uploading")
        db_->update("upload_tasks",taskId,{{"status","failed"},{"message",message}},nullptr);
    emit eventLogged("花型回传失败："+message);
}

void DeviceServer::processUpload(QTcpSocket *socket,quint16 number,const QByteArray &raw,const QByteArray &payload) {
    if(!uploads_.contains(socket)) return;
    UploadTransfer &upload=uploads_[socket];
    if(number==0x0007 && !payload.isEmpty()) {
        const int status=quint8(payload[0]);
        if(status==2) {
            upload.finished=true;
            upload.deadline=QDateTime::currentDateTimeUtc().addSecs(30);
            if(upload.totalFrames>0 && upload.frames.size()==upload.totalFrames) finishUpload(socket);
        } else if(status==0) upload.deadline=QDateTime::currentDateTimeUtc().addSecs(30);
        else {
            failUpload(socket,QString("设备拒绝或中止回传，状态%1").arg(status));
            socket->disconnectFromHost();
        }
        return;
    }
    if(number==0x000D) {
        const int total=qMax(1,int(quint8(raw[17]))), index=qMax(1,int(quint8(raw[18])));
        if(upload.totalFrames==0) upload.totalFrames=total;
        if(upload.totalFrames!=total || index>total) {
            failUpload(socket,"回传帧号不一致"); socket->disconnectFromHost(); return;
        }
        upload.frames[index]=payload;
        qsizetype bytes=0; for(const QByteArray &chunk:upload.frames) bytes+=chunk.size();
        if(bytes>64*1024*1024) {
            failUpload(socket,"回传文件超过64 MiB"); socket->disconnectFromHost(); return;
        }
        socket->write(frame(0x0B2A,0x000D,QByteArray(1,char(0)),quint8(total),quint8(index)));
        upload.deadline=QDateTime::currentDateTimeUtc().addSecs(3);
        db_->update("upload_tasks",upload.taskId,{{"progress",5+int(upload.frames.size()*90.0/upload.totalFrames)},
            {"message",QString("正在回传 %1/%2").arg(upload.frames.size()).arg(upload.totalFrames)}},nullptr);
        if(upload.finished && upload.frames.size()==upload.totalFrames) finishUpload(socket);
        return;
    }
    if(number==0x000E && payload.size()>=2 && quint8(payload[0])==1) {
        socket->write(frame(0x0B2A,0x000E,payload.left(2)));
        upload.deadline=QDateTime::currentDateTimeUtc().addSecs(3);
    }
}

void DeviceServer::finishUpload(QTcpSocket *socket) {
    if(!uploads_.contains(socket)) return;
    const UploadTransfer transfer=uploads_.value(socket);
    if(transfer.totalFrames<1 || transfer.frames.size()!=transfer.totalFrames) return;
    QByteArray data;
    for(int index=1;index<=transfer.totalFrames;++index) {
        if(!transfer.frames.contains(index)) return;
        data+=transfer.frames.value(index);
    }
    if(data.isEmpty()) { failUpload(socket,"设备回传空文件"); return; }
    const QJsonObject task=db_->row("upload_tasks",transfer.taskId);
    if(task.isEmpty() || task.value("status").toString()!="uploading") { uploads_.remove(socket); return; }
    const QJsonObject file=transfer.file;
    const qint64 deviceId=task.value("deviceId").toInteger();
    const QString originalName=file.value("fileName").toString();
    QString baseName=QFileInfo(originalName).completeBaseName().trimmed();
    if(baseName.isEmpty()) baseName=QString("device_%1_pattern_%2").arg(deviceId).arg(file.value("patternNo").toInt());
    const QString mode=task.value("conflictMode").toString();
    QString name=mode=="rename" && !task.value("targetName").toString().trimmed().isEmpty() ?
        task.value("targetName").toString().trimmed() : baseName;
    QJsonArray existing=db_->rows("patterns","name=?",{baseName},"id DESC",1);
    if(mode=="ask" && !existing.isEmpty()) { failUpload(socket,"花型名称重复"); return; }
    if(mode=="rename") {
        const QString root=name;
        for(int suffix=1;db_->count("patterns","name=?",{name})>0 && suffix<1000;++suffix)
            name=root+QString("(%1)").arg(suffix);
        if(db_->count("patterns","name=?",{name})>0) { failUpload(socket,"无法分配花型名称"); return; }
    }
    const QString directory=QDir(db_->dataDirectory()).filePath("uploads/patterns");
    if(!QDir().mkpath(directory)) { failUpload(socket,"创建上传目录失败"); return; }
    const QString extension=QFileInfo(originalName).suffix().isEmpty() ? "dst" : QFileInfo(originalName).suffix().toLower();
    const QString storageName=QString("device_%1_%2_%3_%4.%5").arg(deviceId).arg(file.value("patternNo").toInt())
        .arg(QDateTime::currentDateTime().toString("yyyyMMddHHmmsszzz"))
        .arg(QRandomGenerator::global()->generate()).arg(extension);
    const QString path=QDir(directory).filePath(storageName);
    QFile output(path);
    if(!output.open(QIODevice::WriteOnly) || output.write(data)!=data.size()) {
        output.remove(); failUpload(socket,"保存回传文件失败"); return;
    }
    output.close();
    QSqlDatabase connection=db_->connection();
    if(!connection.transaction()) { qWarning("Upload save failed: %s",qPrintable(connection.lastError().text()));QFile::remove(path); failUpload(socket,"保存回传信息失败，请重试"); return; }
    qint64 patternId=0; QString error, oldPath;
    if(mode=="overwrite" && !existing.isEmpty()) {
        patternId=existing.first().toObject().value("id").toInteger();
        oldPath=existing.first().toObject().value("filePath").toString();
        if(!db_->update("patterns",patternId,{{"fileName",originalName},{"filePath",path},{"fileSize",data.size()},
            {"uploadedBy",task.value("operatorId")},{"uploadedAt",QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")}},&error)) {
            qWarning("Upload save failed: %s",qPrintable(error));connection.rollback(); QFile::remove(path); failUpload(socket,"保存回传信息失败，请重试"); return;
        }
    } else if(!db_->insert("patterns",{{"name",name},{"patternType",file.value("patternType")},
        {"fileName",originalName},{"filePath",path},{"fileSize",data.size()},{"stitches",file.value("stitches")},
        {"unitPrice",file.value("unitPrice")},{"orderNo",file.value("orderNo")},{"uploadedBy",task.value("operatorId")},
        {"uploadedAt",QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")}},&patternId,&error)) {
        qWarning("Upload save failed: %s",qPrintable(error));connection.rollback(); QFile::remove(path); failUpload(socket,"保存回传信息失败，请重试"); return;
    }
    if(!db_->update("upload_tasks",transfer.taskId,{{"patternId",patternId},{"status","completed"},
        {"progress",100},{"message","回传完成"}},&error) || !connection.commit()) {
        qWarning("Upload save failed: %s",qPrintable(error.isEmpty() ? connection.lastError().text() : error));connection.rollback(); QFile::remove(path); failUpload(socket,"保存回传信息失败，请重试"); return;
    }
    uploads_.remove(socket);
    if(!oldPath.isEmpty() && oldPath!=path) QFile::remove(oldPath);
    emit eventLogged("花型回传完成：任务"+QString::number(transfer.taskId));
}

void DeviceServer::tickTransfers() {
    const auto now=QDateTime::currentDateTimeUtc();
    for(const QString &id:directJobs_.keys()) {
        const auto job=directJobs_.value(id);
        if(job.lease<now || (job.status=="downloading" && job.started.secsTo(now)>600))cancelDirectTransfer(id,job.ownerId);
    }
    const QList<QTcpSocket*> active=transfers_.keys();
    for(QTcpSocket *socket:active) {
        if(!transfers_.contains(socket)) continue;
        const Transfer transfer=transfers_.value(socket);
        if(transfer.directId.isEmpty() && db_->row("download_tasks",transfer.taskId).value("status").toString()!="downloading") {
            transfers_.remove(socket);
            socket->disconnectFromHost();
        } else if(transfer.deadline<QDateTime::currentDateTimeUtc()) {
            failTransfer(socket,"设备响应超时");
            socket->disconnectFromHost();
        }
    }
    const QList<QTcpSocket*> activeUploads=uploads_.keys();
    for(QTcpSocket *socket:activeUploads) {
        if(!uploads_.contains(socket)) continue;
        UploadTransfer &upload=uploads_[socket];
        if(db_->row("upload_tasks",upload.taskId).value("status").toString()!="uploading") {
            uploads_.remove(socket);
            socket->disconnectFromHost();
        } else if(upload.startedAt.secsTo(QDateTime::currentDateTimeUtc())>600) {
            failUpload(socket,"回传超过10分钟");
            socket->disconnectFromHost();
        } else if(upload.deadline<QDateTime::currentDateTimeUtc()) {
            if(upload.totalFrames>0 && upload.frames.size()<upload.totalFrames) {
                int missing=1; while(missing<=upload.totalFrames && upload.frames.contains(missing)) ++missing;
                if(missing<=upload.totalFrames) {
                    socket->write(frame(0x0B2A,0x000E,QByteArray{char(1),char(missing)}));
                    upload.deadline=QDateTime::currentDateTimeUtc().addSecs(3);
                }
            } else if(!upload.finished) {
                failUpload(socket,"等待设备完成回传超时");
                socket->disconnectFromHost();
            }
        }
    }
    const QJsonArray waiting=db_->rows("download_tasks","status='waiting'",{},"device_id,created_at,id",10000);
    QSet<qint64> selectedDevices;
    for(const QJsonValue &value:waiting) {
        const QJsonObject task=value.toObject();
        const qint64 deviceId=task.value("deviceId").toInteger(), taskId=task.value("id").toInteger();
        if(selectedDevices.contains(deviceId)) continue;
        selectedDevices.insert(deviceId);
        QTcpSocket *socket=nullptr;
        for(auto it=sessions_.cbegin();it!=sessions_.cend();++it)
            if(it.value().deviceId==deviceId && it.key()->state()==QAbstractSocket::ConnectedState && !transfers_.contains(it.key()) && !uploads_.contains(it.key()) &&
               !listRequests_.contains(it.key()) && !deleteRequests_.contains(it.key())) { socket=it.key(); break; }
        const QJsonObject device=db_->row("devices",deviceId);
        QDateTime queued=QDateTime::fromString(task.value("updatedAt").toString(),"yyyy-MM-dd HH:mm:ss");
        queued.setTimeZone(QTimeZone::UTC);
        const bool expired=queued.isValid() && queued.secsTo(QDateTime::currentDateTimeUtc())>600;
        if(device.value("status").toString()=="working") {
            if(expired)
                db_->update("download_tasks",taskId,{{"status","failed"},{"message","设备缝纫中，等待超过10分钟，任务失败"}},nullptr);
            else if(task.value("message").toString()!="设备缝纫中，等待空闲")
                db_->update("download_tasks",taskId,{{"message","设备缝纫中，等待空闲"}},nullptr);
            continue;
        }
        if(!socket) {
            const bool connected=deviceConnected(deviceId);
            if(expired && !connected)
                db_->update("download_tasks",taskId,{{"status","failed"},{"message","设备未连接，等待超过10分钟，任务失败"}},nullptr);
            else if(!connected && task.value("message").toString()!="设备未连接，等待开机")
                db_->update("download_tasks",taskId,{{"message","设备未连接，等待开机"}},nullptr);
            continue;
        }
        const QJsonObject pattern=db_->row("patterns",task.value("patternId").toInteger());
        QFile file(pattern.value("filePath").toString());
        if(pattern.isEmpty() || !file.open(QIODevice::ReadOnly)) {
            db_->update("download_tasks",taskId,{{"status","failed"},{"message","花型文件不存在"}},nullptr);
            continue;
        }
        const QByteArray data=file.readAll();
        const QString name=pattern.value("name").toString().trimmed();
        if(data.isEmpty() || name.isEmpty() || name.size()*2>44) {
            db_->update("download_tasks",taskId,{{"status","failed"},{"message","花型文件或名称不合法"}},nullptr);
            continue;
        }
        const int chunkSize=qMax(256,((data.size()+255*256-1)/(255*256))*256);
        if(chunkSize>65536) {
            db_->update("download_tasks",taskId,{{"status","failed"},{"message","花型文件超过设备协议帧数限制"}},nullptr);
            continue;
        }
        Transfer transfer; transfer.taskId=taskId; transfer.data=data; transfer.patternName=name;
        transfer.chunkSize=chunkSize; transfer.totalFrames=(data.size()+chunkSize-1)/chunkSize;
        transfer.deadline=QDateTime::currentDateTimeUtc().addSecs(60);
        transfers_.insert(socket,transfer);
        db_->update("download_tasks",taskId,{{"status","downloading"},{"progress",2},{"message","等待设备确认下载指令"}},nullptr);
        QByteArray encoded;
        for(QChar character:name) { const ushort value=character.unicode(); encoded.append(char(value&0xff)); encoded.append(char(value>>8)); }
        socket->write(frame(0x0B2A,0x0004,encoded));
    }
    const QJsonArray uploads=db_->rows("upload_tasks","status='waiting'",{},"created_at,id",10000);
    QSet<qint64> selectedUploads;
    for(const QJsonValue &value:uploads) {
        const QJsonObject task=value.toObject();
        const qint64 deviceId=task.value("deviceId").toInteger(), taskId=task.value("id").toInteger();
        if(selectedUploads.contains(deviceId)) continue;
        selectedUploads.insert(deviceId);
        QTcpSocket *socket=nullptr;
        for(auto it=sessions_.cbegin();it!=sessions_.cend();++it)
            if(it.value().deviceId==deviceId && it.key()->state()==QAbstractSocket::ConnectedState && !transfers_.contains(it.key()) && !uploads_.contains(it.key()) &&
               !listRequests_.contains(it.key()) && !deleteRequests_.contains(it.key())) { socket=it.key(); break; }
        if(!socket) continue;
        QJsonObject file=QJsonDocument::fromJson(task.value("fileSnapshot").toString().toUtf8()).object();
        if(file.isEmpty()) file=db_->row("device_pattern_files",task.value("deviceFileId").toInteger());
        if(file.isEmpty()) {
            db_->update("upload_tasks",taskId,{{"status","failed"},{"message","设备文件记录不存在"}},nullptr);
            continue;
        }
        UploadTransfer transfer; transfer.taskId=taskId; transfer.file=file;
        transfer.startedAt=QDateTime::currentDateTimeUtc(); transfer.deadline=transfer.startedAt.addSecs(30);
        uploads_.insert(socket,transfer);
        db_->update("upload_tasks",taskId,{{"status","uploading"},{"progress",1},{"message","正在从设备回传"}},nullptr);
        QByteArray payload(2,0); put16(payload,0,quint16(file.value("patternNo").toInt()));
        for(QChar character:file.value("fileName").toString()) {
            const ushort code=character.unicode(); payload.append(char(code&0xff)); payload.append(char(code>>8));
        }
        socket->write(frame(0x0B2A,0x0007,payload));
    }
}
