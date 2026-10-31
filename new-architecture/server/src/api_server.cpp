#include "api_server.h"
#include "upload_summary.h"
#include <QUuid>
#include "path_validation.h"
#include "database.h"
#include "device_server.h"
#include "external_db.h"
#include "stats_time.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QMap>
#include <QNetworkInterface>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QTime>
#include <QTimeZone>
#include <QThread>
#include <QUrlQuery>
#include <algorithm>
#include <limits>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
constexpr qsizetype maxHttpBody=256*1024*1024;
QString portablePatternFilename(QString name,const QString &extension,const QString &disambiguator={}) {
    QString suffix=extension;suffix.remove(QRegularExpression("[^A-Za-z0-9]"));suffix=suffix.left(12);const QString ending=suffix.isEmpty() ? QString() : "."+suffix;
    if(!ending.isEmpty() && name.endsWith(ending,Qt::CaseInsensitive))name.chop(ending.size());
    name.replace(QRegularExpression(R"pat([<>:"/\\|?*\x00-\x1f])pat"),"_");
    while(name.endsWith(' ') || name.endsWith('.'))name.chop(1);
    if(name.isEmpty())name="pattern";
    if(QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$").match(name.section('.',0,0).toUpper()).hasMatch())name.prepend('_');
    const qsizetype budget=qMax<qsizetype>(1,240-ending.toUtf8().size()-disambiguator.toUtf8().size());
    while(name.toUtf8().size()>budget && !name.isEmpty()){const int count=name.at(name.size()-1).isLowSurrogate() && name.size()>1 && name.at(name.size()-2).isHighSurrogate() ? 2 : 1;name.chop(count);}
    return name+disambiguator+ending;
}

bool decodeChunked(const QByteArray &input,QByteArray *body,bool *complete) {
    qsizetype position=0;
    *complete=false;
    QByteArray decoded;
    while(true) {
        const qsizetype lineEnd=input.indexOf("\r\n",position);
        if(lineEnd<0) return true;
        QByteArray sizeText=input.mid(position,lineEnd-position).split(';').first().trimmed();
        bool valid=false;
        const qint64 chunkSize=sizeText.toLongLong(&valid,16);
        if(!valid || chunkSize<0 || chunkSize>maxHttpBody) return false;
        position=lineEnd+2;
        if(chunkSize==0) {
            if(input.size()<position+2) return true;
            if(input.mid(position,2)=="\r\n") { *body=decoded; *complete=true; return true; }
            if(input.indexOf("\r\n\r\n",position)<0) return true;
            *body=decoded; *complete=true; return true;
        }
        if(decoded.size()+chunkSize>maxHttpBody) return false;
        if(input.size()<position+chunkSize+2) return true;
        if(input.mid(position+chunkSize,2)!="\r\n") return false;
        decoded+=input.mid(position,chunkSize);
        position+=chunkSize+2;
    }
}
struct Multipart {
    QHash<QString,QByteArray> fields;
    QByteArray file;
    QString fileName;
};
struct DiskMultipart {
    QHash<QString,QByteArray> fields;
    QString fileName;
    qint64 fileOffset=-1,fileLength=0;
};
qint64 findMultipartMarker(QFile *file,const QByteArray &marker) {
    QByteArray carry;
    while(!file->atEnd()) {
        const qint64 start=file->pos();
        const QByteArray chunk=file->read(65536);
        const QByteArray search=carry+chunk;
        const qsizetype at=search.indexOf(marker);
        if(at>=0) return start-carry.size()+at;
        carry=search.right(qMax(0,marker.size()-1));
    }
    return -1;
}
DiskMultipart parseMultipartFile(const QString &path,const QString &contentType) {
    DiskMultipart result;
    const auto match=QRegularExpression("(?:^|;)\\s*boundary=\"?([^;\"]+)\"?").match(contentType);
    if(!match.hasMatch()) return result;
    const QByteArray boundary="--"+match.captured(1).toLatin1();
    QFile input(path);
    if(!input.open(QIODevice::ReadOnly) || input.readLine(65536).trimmed()!=boundary) return result;
    const QByteArray marker="\r\n"+boundary;
    while(!input.atEnd()) {
        QByteArray headers;
        while(true) {
            const QByteArray line=input.readLine(65536);
            if(line.isEmpty()) return {};
            if(line=="\r\n") break;
            if(headers.size()+line.size()>65536) return {};
            headers+=line;
        }
        const qint64 start=input.pos();
        const qint64 boundaryAt=findMultipartMarker(&input,marker);
        if(boundaryAt<start) return {};
        const QString headerText=QString::fromUtf8(headers);
        const auto name=QRegularExpression("name=\"([^\"]+)\"").match(headerText);
        const auto file=QRegularExpression("filename=\"([^\"]*)\"").match(headerText);
        if(name.hasMatch()) {
            if(name.captured(1)=="file" && file.hasMatch()) {
                result.fileName=QFileInfo(file.captured(1).replace('\\','/')).fileName();
                result.fileOffset=start; result.fileLength=boundaryAt-start;
            } else if(boundaryAt-start<=65536) {
                input.seek(start); result.fields[name.captured(1)]=input.read(boundaryAt-start);
            }
        }
        input.seek(boundaryAt+marker.size());
        const QByteArray ending=input.read(2);
        if(ending=="--") return result;
        if(ending!="\r\n") return {};
    }
    return result;
}
Multipart parseMultipart(const QByteArray &body,const QString &contentType) {
    Multipart result;
    const QRegularExpression boundaryExpression("(?:^|;)\\s*boundary=\"?([^;\"]+)\"?");
    const auto match=boundaryExpression.match(contentType);
    if(!match.hasMatch()) return result;
    const QByteArray marker="--"+match.captured(1).toLatin1();
    qsizetype at=body.indexOf(marker);
    while(at>=0) {
        at+=marker.size();
        if(body.mid(at,2)=="--") break;
        if(body.mid(at,2)!="\r\n") break;
        at+=2;
        const qsizetype headerEnd=body.indexOf("\r\n\r\n",at);
        if(headerEnd<0) break;
        const QByteArray headers=body.mid(at,headerEnd-at);
        const qsizetype next=body.indexOf("\r\n"+marker,headerEnd+4);
        if(next<0) break;
        const QByteArray content=body.mid(headerEnd+4,next-(headerEnd+4));
        const QRegularExpression nameExpression("name=\"([^\"]+)\"");
        const QRegularExpression fileExpression("filename=\"([^\"]*)\"");
        const QString headerText=QString::fromUtf8(headers);
        const auto nameMatch=nameExpression.match(headerText), fileMatch=fileExpression.match(headerText);
        if(nameMatch.hasMatch()) {
            const QString field=nameMatch.captured(1);
            if(field=="file" && fileMatch.hasMatch()) {
                result.file=content;
                result.fileName=QFileInfo(fileMatch.captured(1).replace('\\','/')).fileName();
            } else result.fields[field]=content;
        }
        at=next+2;
    }
    return result;
}
QByteArray b64(const QByteArray &data) { return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals); }
QByteArray un64(const QByteArray &data) { return QByteArray::fromBase64(data, QByteArray::Base64UrlEncoding); }
QStringList pieces(const QString &path) { return path.split('/', Qt::SkipEmptyParts); }
void appendDeviceIds(const QUrlQuery &query,const QString &alias,QString *where,QVariantList *binds) {
    if(query.queryItemValue("deviceIds",QUrl::FullyDecoded).trimmed()=="0") {
        *where+=" AND "+alias+".device_id=0"; return;
    }
    QStringList ids;
    for(const QString &piece:query.queryItemValue("deviceIds",QUrl::FullyDecoded).split(',',Qt::SkipEmptyParts)) {
        bool valid=false; const qulonglong id=piece.trimmed().toULongLong(&valid);
        if(valid && id>0 && !ids.contains(QString::number(id))) ids << QString::number(id);
    }
    if(ids.isEmpty()) return;
    *where+=" AND "+alias+".device_id IN ("+QStringList(ids.size(),"?").join(',')+")";
    for(const QString &id:ids) *binds << id.toLongLong();
}
QSet<qint64> matchingDeviceIds(Database *db,const QUrlQuery &query,bool *restricted=nullptr) {
    const QString single=query.queryItemValue("deviceId",QUrl::FullyDecoded).trimmed();
    const QString multiple=query.queryItemValue("deviceIds",QUrl::FullyDecoded).trimmed();
    const QString keyword=query.queryItemValue("deviceName",QUrl::FullyDecoded).trimmed();
    const bool active=!single.isEmpty() || !multiple.isEmpty() || !keyword.isEmpty();
    if(restricted) *restricted=active;
    QSet<qint64> requested,matched;
    if(!multiple.isEmpty()) for(const QString &piece:multiple.split(',',Qt::SkipEmptyParts)) {
        bool valid=false; const qint64 id=piece.trimmed().toLongLong(&valid);
        if(valid && id>0) requested.insert(id);
    }
    for(const QJsonValue &value:db->rows("devices",{}, {},"id",10000)) {
        const QJsonObject device=value.toObject(); const qint64 id=device.value("id").toInteger();
        if(!single.isEmpty() && QString::number(id)!=single) continue;
        if(!multiple.isEmpty() && !requested.contains(id)) continue;
        const QString name=device.value("name").toString();
        if(!keyword.isEmpty() && !name.contains(keyword,Qt::CaseInsensitive) &&
           (name.isEmpty() || !keyword.contains(name,Qt::CaseInsensitive)) &&
           !device.value("initialName").toString().contains(keyword,Qt::CaseInsensitive)) continue;
        matched.insert(id);
    }
    return matched;
}
void appendDeviceName(Database *db,const QUrlQuery &query,const QString &alias,QString *where) {
    if(query.queryItemValue("deviceName",QUrl::FullyDecoded).trimmed().isEmpty()) return;
    const QSet<qint64> ids=matchingDeviceIds(db,query);
    QStringList numbers; for(qint64 id:ids) numbers << QString::number(id);
    *where+=" AND "+alias+".device_id IN ("+(numbers.isEmpty() ? QString("0") : numbers.join(','))+")";
}
QJsonArray selected(const QJsonArray &all, const QString &key, const QString &value) {
    if (value.isEmpty()) return all;
    QJsonArray out;
    for (const auto &item : all) if (item.toObject().value(key).toVariant().toString()==value) out.append(item);
    return out;
}
QJsonArray tree(const QJsonArray &all, qint64 parent) {
    QJsonArray out;
    for (const auto &item : all) {
        QJsonObject row=item.toObject();
        const QJsonValue p=row.value("parentId");
        if ((parent==0 && (p.isNull() || p.isUndefined())) || (parent!=0 && p.toInteger()==parent)) {
            row["children"]=tree(all,row.value("id").toInteger());
            out.append(row);
        }
    }
    return out;
}
QJsonObject permissionMap(const QJsonObject &user) {
    const QJsonDocument parsed=QJsonDocument::fromJson(user.value("permissions").toString().toUtf8());
    if(parsed.isObject()) return parsed.object();
    QJsonObject map;
    if(parsed.isArray()) for(const QJsonValue &value:parsed.array())
        if(!value.toString().trimmed().isEmpty()) map[value.toString().trimmed()]=true;
    return map;
}
bool adminRole(const QJsonObject &user) {
    return user.value("role").toString().compare("admin",Qt::CaseInsensitive)==0;
}
bool detailedPermissions(const QJsonObject &map,const QString &parent) {
    static const QHash<QString,QStringList> children={
        {"deviceManagement",{"deviceInfo","remoteMonitoring"}},
        {"fileManagement",{"patternFiles","devicePatternFiles","downloadLog"}},
        {"statistics",{"salaryStatistics","statusStatistics"}}};
    for(const QString &child:children.value(parent)) {
        if(!map.contains(child)) continue;
        if(parent=="deviceManagement" && child=="remoteMonitoring" &&
           map.value(child).toBool() && map.value(parent).toBool() && !map.contains("deviceInfo"))
            continue;
        return true;
    }
    return false;
}
bool hasPermission(const QJsonObject &user,const QStringList &required) {
    if(adminRole(user)) return true;
    const QJsonObject map=permissionMap(user);
    static const QHash<QString,QStringList> children={
        {"deviceManagement",{"deviceInfo","remoteMonitoring"}},
        {"fileManagement",{"patternFiles","devicePatternFiles","downloadLog"}},
        {"statistics",{"salaryStatistics","statusStatistics"}}};
    for(const QString &key:required) {
        if(map.value(key).toBool() && !detailedPermissions(map,key)) return true;
        if(map.contains(key)) continue;
        for(auto it=children.cbegin();it!=children.cend();++it)
            if(it.value().contains(key) && !detailedPermissions(map,it.key()) && map.value(it.key()).toBool())
                return true;
    }
    return false;
}
bool canUse(const QJsonObject &user,const QString &section,bool write) {
    if(adminRole(user)) return true;
    if(section=="group" || section=="role" || section=="user" || section=="operator") return false;
    if(section=="pattern") return hasPermission(user,{"fileManagement","patternFiles","devicePatternFiles","downloadLog"});
    if(section=="device") return write ? hasPermission(user,{"deviceManagement","deviceInfo"}) :
        hasPermission(user,{"deviceManagement","deviceInfo","remoteMonitoring","fileManagement",
            "patternFiles","devicePatternFiles","downloadLog","statistics","salaryStatistics",
            "statusStatistics","employeeManagement","dashboard","home"});
    if(section=="employee") return write ? hasPermission(user,{"employeeManagement"}) :
        hasPermission(user,{"employeeManagement","statistics","salaryStatistics"});
    return false;
}
bool inScope(const QJsonObject &item,const QJsonObject &user) {
    Q_UNUSED(item); Q_UNUSED(user);
    return true;
}
QJsonArray scoped(const QJsonArray &all,const QJsonObject &user) {
    Q_UNUSED(user);
    return all;
}
QJsonArray scopedPatterns(const QJsonArray &all,const QJsonObject &user,const Database *db) {
    Q_UNUSED(user); Q_UNUSED(db);
    return all;
}
QJsonArray scopedDeviceRecords(const QJsonArray &all,const QJsonObject &user,const Database *db) {
    Q_UNUSED(user); Q_UNUSED(db);
    return all;
}
QJsonArray visibleGroups(const QJsonArray &all,const QJsonObject &user) {
    Q_UNUSED(user);
    return all;
}
QJsonArray deviceTree(const QJsonArray &groups,const QJsonArray &devices,qint64 parent) {
    QJsonArray result;
    for(const auto &value:groups) {
        QJsonObject group=value.toObject();
        const QJsonValue p=group.value("parentId");
        if((parent==0 && p.isNull()) || (parent!=0 && p.toInteger()==parent)) {
            QJsonArray children=deviceTree(groups,devices,group.value("id").toInteger());
            for(const auto &dv:devices) {
                QJsonObject device=dv.toObject();
                if(device.value("groupId").toInteger()!=group.value("id").toInteger()) continue;
                device["label"]=device.value("name"); device["type"]="device"; children.append(device);
            }
            group["label"]=group.value("name"); group["type"]="group"; group["children"]=children;
            result.append(group);
        }
    }
    return result;
}
QJsonObject patternUpdates(const QJsonObject &request,QString *error) {
    QJsonObject body;
    if(request.contains("name")) {
        const QString value=request.value("name").toString().trimmed();
        if(value.isEmpty() || value.size()>255) { if(error) *error="花型名称不能为空且不能超过255个字符"; return {}; }
        body["name"]=value;
    }
    for(const QString &key:{"patternType","orderNo"}) if(request.contains(key)) {
        const QString raw=request.value(key).toString();
        if(key=="orderNo" && (!request.value(key).isString() || (!raw.isEmpty() && !QRegularExpression("^[A-Za-z0-9]+$").match(raw).hasMatch()))) {if(error)*error="订单编号只允许字母和数字";return {};}
        const QString value=raw.trimmed();
        if(value.size()>100) { if(error) *error="花型类型或订单编号不能超过100个字符"; return {}; }
        body[key]=value;
    }
    if(request.contains("stitches")) {
        const qint64 value=request.value("stitches").toInteger(-1);
        if(value<0 || value>2147483647) { if(error) *error="针数不能为负数"; return {}; }
        body["stitches"]=value;
    }
    if(request.contains("unitPrice")) {
        const double value=request.value("unitPrice").toDouble(-1);
        if(!qIsFinite(value) || value<0 || value>999999999.999) { if(error) *error="工价不能为负数"; return {}; }
        body["unitPrice"]=qRound64(value*1000.0)/1000.0;
    }
    return body;
}
QDateTime runtimeTimestamp(const QString &text) {
    const QDate date=QDate::fromString(text.left(10),"yyyy-MM-dd");
    const QTime time=QTime::fromString(text.mid(11,8),"HH:mm:ss");
    return date.isValid() && time.isValid() ? QDateTime(date,time,QTimeZone::utc()).toLocalTime() : QDateTime{};
}
QMap<QString,QMap<qint64,double>> sessionHours(Database *db,const QDate &first,const QDate &last) {
    QMap<QString,QMap<qint64,double>> result;
    if(!first.isValid() || !last.isValid() || first>last) return result;
    const QDateTime beginning(first,QTime(0,0)), ending(last.addDays(1),QTime(0,0));
    const QJsonArray sessions=db->rows("device_runtime_sessions",
        "started_at<? AND (ended_at IS NULL OR ended_at>?)",
        {ending.toUTC().toString("yyyy-MM-dd HH:mm:ss"),beginning.toUTC().toString("yyyy-MM-dd HH:mm:ss")},
        "started_at",10000);
    for(const QJsonValue &value:sessions) {
        const QJsonObject row=value.toObject();
        QDateTime start=runtimeTimestamp(row.value("startedAt").toString());
        QDateTime end=row.value("endedAt").isNull() ? QDateTime::currentDateTime() :
            runtimeTimestamp(row.value("endedAt").toString());
        if(!start.isValid() || !end.isValid() || end<=start) continue;
        if(start<beginning) start=beginning;
        if(end>ending) end=ending;
        if(end<=start) continue;
        for(QDateTime cursor=start;cursor<end;) {
            const QDateTime nextDay(cursor.date().addDays(1),QTime(0,0));
            const QDateTime segmentEnd=nextDay<end ? nextDay : end;
            result[cursor.date().toString("yyyy-MM-dd")][row.value("deviceId").toInteger()]+=
                cursor.secsTo(segmentEnd)/3600.0;
            cursor=segmentEnd;
        }
    }
    return result;
}
double runtimeForDay(const QMap<QString,QMap<qint64,double>> &sessions,const QString &date,
                     qint64 deviceId,const QPair<double,double> &production) {
    const double connected=sessions.value(date).value(deviceId);
    return connected>0 ? connected : production.first+production.second;
}
QMap<QString,QPair<QString,QString>> historicalEmployees(Database *db,
    const QString &startDate,const QString &endDate) {
    QMap<QString,QPair<QString,QString>> result;
    QString sql="SELECT pr.device_id,substr(pr.record_date,1,10),"
        "COALESCE(MAX(NULLIF(e.code,'')),MAX(NULLIF(pr.protocol_user_id,'')),'-'),"
        "COALESCE(MAX(NULLIF(e.name,'')),'-') "
        "FROM production_records pr LEFT JOIN employees e ON e.id=pr.employee_id "
        "WHERE pr.deleted_at IS NULL";
    QVariantList binds;
    if(!startDate.isEmpty()) { sql+=" AND substr(pr.record_date,1,10)>=?"; binds << startDate; }
    if(!endDate.isEmpty()) { sql+=" AND substr(pr.record_date,1,10)<=?"; binds << endDate; }
    sql+=" GROUP BY pr.device_id,substr(pr.record_date,1,10)";
    QSqlQuery query(db->connection()); query.prepare(sql);
    for(const QVariant &bind:binds) query.addBindValue(bind);
    if(query.exec()) while(query.next())
        result[query.value(0).toString()+"|"+query.value(1).toString()]=
            {query.value(2).toString(),query.value(3).toString()};
    return result;
}
QString alarmMessage(const QString &description,const QString &type,const QString &code) {
    QString message=(description.isEmpty() ? type : description).trimmed();
    if(!code.isEmpty() && message.startsWith(code,Qt::CaseInsensitive)) {
        const QString tail=message.mid(code.size()).trimmed();
        if(tail.startsWith('-') || tail.startsWith(':') || tail.startsWith(QChar(0xff1a)))message=tail.mid(1).trimmed();
    }
    return message.isEmpty() ? QString("未知报警") : message;
}
struct AlarmWindow {
    qint64 deviceId=0;
    QDateTime started,ended;
    QString name,time;
};
QVector<AlarmWindow> alarmWindows(Database *db,const QString &startDate,const QString &endDate) {
    QVector<AlarmWindow> result;
    QString sql="SELECT ar.device_id,ar.start_time,ar.end_time,ar.duration,"
        "ar.description,ar.alarm_type,ar.alarm_code "
        "FROM alarm_records ar WHERE ar.deleted_at IS NULL";
    QVariantList binds;
    if(!startDate.isEmpty()) { sql+=" AND substr(ar.start_time,1,10)>=?"; binds << startDate; }
    if(!endDate.isEmpty()) { sql+=" AND substr(ar.start_time,1,10)<=?"; binds << endDate; }
    sql+=" ORDER BY ar.start_time,ar.id";
    QSqlQuery query(db->connection()); query.prepare(sql);
    for(const QVariant &bind:binds) query.addBindValue(bind);
    if(query.exec()) while(query.next()) {
        AlarmWindow window;
        window.deviceId=query.value(0).toLongLong();
        window.time=sqlText(query.value(1));
        window.started=QDateTime::fromString(window.time,"yyyy-MM-dd HH:mm:ss");
        window.ended=QDateTime::fromString(sqlText(query.value(2)),"yyyy-MM-dd HH:mm:ss");
        if(!window.ended.isValid() || window.ended<window.started)
            window.ended=window.started.addSecs(qMax(0,query.value(3).toInt()));
        window.name=alarmMessage(query.value(4).toString(),query.value(5).toString(),query.value(6).toString());
        if(window.started.isValid()) result.append(window);
    }
    return result;
}
QPair<qint64,QString> lineForGroup(const QMap<qint64,QJsonObject> &groups,qint64 groupId) {
    if(groupId<1 || !groups.contains(groupId)) return {0,{}};
    QList<qint64> chain;
    QSet<qint64> visited;
    qint64 current=groupId;
    while(current>0 && groups.contains(current) && !visited.contains(current)) {
        chain.prepend(current); visited.insert(current);
        current=groups.value(current).value("parentId").toInteger();
    }
    if(chain.isEmpty()) return {0,{}};
    if(chain.size()>1 && chain.first()==1) chain.removeFirst();
    return {chain.first(),groups.value(chain.first()).value("name").toString()};
}
QString durationText(double hours) {
    const qint64 seconds=qMax<qint64>(0,qRound64(hours*3600.0));
    const qint64 h=seconds/3600,m=(seconds%3600)/60,s=seconds%60;
    if(h>0) return QString("%1小时%2分%3秒").arg(h).arg(m).arg(s);
    if(m>0) return QString("%1分%2秒").arg(m).arg(s);
    return QString("%1秒").arg(s);
}
QByteArray csvField(const QString &value) {
    QByteArray bytes=value.toUtf8();
    if(bytes.contains('"') || bytes.contains(',') || bytes.contains('\n') || bytes.contains('\r')) {
        bytes.replace("\"","\"\"");
        return "\""+bytes+"\"";
    }
    return bytes;
}
void zip16(QByteArray &bytes,quint16 value) { bytes.append(char(value&0xff)); bytes.append(char((value>>8)&0xff)); }
void zip32(QByteArray &bytes,quint32 value) { zip16(bytes,quint16(value&0xffff)); zip16(bytes,quint16(value>>16)); }
quint32 zipCrc(const QByteArray &data) {
    quint32 crc=0xffffffff;
    for(uchar byte:data) {
        crc^=byte;
        for(int i=0;i<8;++i) crc=(crc>>1)^((crc&1) ? 0xedb88320u : 0u);
    }
    return crc^0xffffffff;
}
QByteArray makeStoredZip(const QVector<QPair<QString,QByteArray>> &files) {
    QByteArray archive,central;
    const QDateTime now=QDateTime::currentDateTime();
    const quint16 dosTime=quint16((now.time().hour()<<11)|(now.time().minute()<<5)|(now.time().second()/2));
    const quint16 dosDate=quint16(((now.date().year()-1980)<<9)|(now.date().month()<<5)|now.date().day());
    for(const auto &entry:files) {
        const QByteArray name=entry.first.toUtf8(), data=entry.second;
        const quint32 offset=quint32(archive.size()), crc=zipCrc(data), size=quint32(data.size());
        zip32(archive,0x04034b50); zip16(archive,20); zip16(archive,0x0800); zip16(archive,0);
        zip16(archive,dosTime); zip16(archive,dosDate); zip32(archive,crc); zip32(archive,size);
        zip32(archive,size); zip16(archive,quint16(name.size())); zip16(archive,0);
        archive+=name; archive+=data;
        zip32(central,0x02014b50); zip16(central,20); zip16(central,20); zip16(central,0x0800);
        zip16(central,0); zip16(central,dosTime); zip16(central,dosDate); zip32(central,crc);
        zip32(central,size); zip32(central,size); zip16(central,quint16(name.size()));
        zip16(central,0); zip16(central,0); zip16(central,0); zip16(central,0);
        zip32(central,0); zip32(central,offset); central+=name;
    }
    const quint32 centralOffset=quint32(archive.size());
    archive+=central;
    zip32(archive,0x06054b50); zip16(archive,0); zip16(archive,0);
    zip16(archive,quint16(files.size())); zip16(archive,quint16(files.size()));
    zip32(archive,quint32(central.size())); zip32(archive,centralOffset); zip16(archive,0);
    return archive;
}
bool makeStoredZipFile(const QVector<QPair<QString,QString>> &files,const QString &path) {
    if(files.size()>65535) return false;
    QFile output(path);
    if(!output.open(QIODevice::WriteOnly|QIODevice::Truncate)) return false;
    QByteArray central;
    const QDateTime now=QDateTime::currentDateTime();
    const quint16 dosTime=quint16((now.time().hour()<<11)|(now.time().minute()<<5)|(now.time().second()/2));
    const quint16 dosDate=quint16(((now.date().year()-1980)<<9)|(now.date().month()<<5)|now.date().day());
    for(const auto &entry:files) {
        const QByteArray name=entry.first.toUtf8();
        QFile source(entry.second);
        if(name.size()>65535 || !source.open(QIODevice::ReadOnly) ||
           source.size()>std::numeric_limits<quint32>::max() ||
           output.pos()>std::numeric_limits<quint32>::max()) return false;
        quint32 crc=0xffffffff;
        while(!source.atEnd()) for(uchar byte:source.read(65536)) {
            crc^=byte;
            for(int i=0;i<8;++i) crc=(crc>>1)^((crc&1) ? 0xedb88320u : 0u);
        }
        crc^=0xffffffff;
        if(!source.seek(0)) return false;
        const quint32 offset=quint32(output.pos()), size=quint32(source.size());
        QByteArray local;
        zip32(local,0x04034b50); zip16(local,20); zip16(local,0x0800); zip16(local,0);
        zip16(local,dosTime); zip16(local,dosDate); zip32(local,crc); zip32(local,size);
        zip32(local,size); zip16(local,quint16(name.size())); zip16(local,0); local+=name;
        if(output.write(local)!=local.size()) return false;
        while(!source.atEnd()) {
            const QByteArray chunk=source.read(65536);
            if(chunk.isEmpty() || output.write(chunk)!=chunk.size()) return false;
        }
        zip32(central,0x02014b50); zip16(central,20); zip16(central,20); zip16(central,0x0800);
        zip16(central,0); zip16(central,dosTime); zip16(central,dosDate); zip32(central,crc);
        zip32(central,size); zip32(central,size); zip16(central,quint16(name.size()));
        zip16(central,0); zip16(central,0); zip16(central,0); zip16(central,0);
        zip32(central,0); zip32(central,offset); central+=name;
    }
    if(output.pos()+central.size()+22>std::numeric_limits<quint32>::max()) return false;
    const quint32 centralOffset=quint32(output.pos());
    if(output.write(central)!=central.size()) return false;
    QByteArray end;
    zip32(end,0x06054b50); zip16(end,0); zip16(end,0);
    zip16(end,quint16(files.size())); zip16(end,quint16(files.size()));
    zip32(end,quint32(central.size())); zip32(end,centralOffset); zip16(end,0);
    return output.write(end)==end.size();
}
const QString managementRule="Bohr LAN Server - Management TCP";
const QString deviceRule="Bohr LAN Server - Device TCP";
bool localConsole(const QString &peer,const QString &origin,bool checkOrigin) {
    QHostAddress address(peer);
    if(!address.isLoopback()) return false;
    if(!checkOrigin || origin.trimmed().isEmpty()) return true;
    const QString host=QUrl(origin).host().toLower();
    if(host=="localhost" || host=="tauri.localhost") return true;
    return QHostAddress(host).isLoopback();
}
QJsonObject firewallStatus(int httpPort,int tcpPort) {
    QJsonObject management{{"name",managementRule},{"port",httpPort},{"exists",false}};
    QJsonObject device{{"name",deviceRule},{"port",tcpPort},{"exists",false}};
#ifdef Q_OS_WIN
    auto inspect=[](const QString &name,int port) {
        QProcess process;
        process.start(QDir(qEnvironmentVariable("SystemRoot","C:\\Windows")).filePath("System32/netsh.exe"),
                      {"advfirewall","firewall","show","rule","name="+name,"verbose"});
        process.waitForFinished(5000);
        const QString output=QString::fromLocal8Bit(process.readAllStandardOutput()+process.readAllStandardError()).toLower();
        const bool disabled=output.contains("enabled: no") || output.contains("已启用: 否") ||
            output.contains("已启用:否") || output.contains("啟用: 否");
        return !disabled && output.contains(name.toLower()) && output.contains(QString::number(port));
    };
    management["exists"]=inspect(managementRule,httpPort) || inspect(QString(managementRule).replace("Bohr","Boer"),httpPort);
    device["exists"]=inspect(deviceRule,tcpPort) || inspect(QString(deviceRule).replace("Bohr","Boer"),tcpPort);
    const bool repair=!management.value("exists").toBool() || !device.value("exists").toBool();
    return {{"supported",true},{"needsRepair",repair},{"management",management},{"device",device},
        {"message",repair ? "Windows 防火墙可能阻止局域网客户端连接" : "局域网防火墙规则已配置"},
        {"allowedScopes",QJsonArray{"LocalSubnet"}}};
#else
    return {{"supported",false},{"needsRepair",false},{"management",management},{"device",device},
        {"message","当前系统不需要 Windows 防火墙规则"}};
#endif
}
QString repairFirewall(int httpPort,int tcpPort) {
#ifdef Q_OS_WIN
    if(httpPort<1 || httpPort>65535 || tcpPort<1 || tcpPort>65535) return "端口不合法";
    const QString netsh=QDir(qEnvironmentVariable("SystemRoot","C:\\Windows")).filePath("System32/netsh.exe");
    const QString command=QString(
        "/d /s /c \"\"%5\" advfirewall firewall delete rule name=\"%1\" >nul 2>&1 & "
        "\"%5\" advfirewall firewall add rule name=\"%1\" dir=in action=allow protocol=TCP "
        "localport=%2 remoteip=LocalSubnet profile=any enable=yes & "
        "\"%5\" advfirewall firewall delete rule name=\"%3\" >nul 2>&1 & "
        "\"%5\" advfirewall firewall add rule name=\"%3\" dir=in action=allow protocol=TCP "
        "localport=%4 remoteip=LocalSubnet profile=any enable=yes\"")
        .arg(managementRule).arg(httpPort).arg(deviceRule).arg(tcpPort).arg(netsh);
    const std::wstring parameters=command.toStdWString();
    SHELLEXECUTEINFOW info{};
    info.cbSize=sizeof(info); info.fMask=SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb=L"runas"; info.lpFile=L"cmd.exe"; info.lpParameters=parameters.c_str(); info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info)) return GetLastError()==ERROR_CANCELLED ?
        "用户取消了管理员授权" : "请求管理员权限失败";
    if(!info.hProcess) return "无法读取防火墙命令进程";
    WaitForSingleObject(info.hProcess,INFINITE);
    DWORD exitCode=1; GetExitCodeProcess(info.hProcess,&exitCode); CloseHandle(info.hProcess);
    if(exitCode!=0) return QString("防火墙命令失败，退出码%1").arg(exitCode);
    if(firewallStatus(httpPort,tcpPort).value("needsRepair").toBool()) return "规则执行后仍未检测到放行状态";
    return {};
#else
    Q_UNUSED(httpPort); Q_UNUSED(tcpPort);
    return {};
#endif
}
}

ApiServer::ApiServer(Database *db, QObject *parent) : QObject(parent), db_(db) {
    secret_=db_->config("jwt_secret").toUtf8();
    if (secret_.isEmpty()) {
        secret_=QByteArray(32,0);
        for(char &c:secret_) c=char(QRandomGenerator::system()->generate() & 0xff);
        db_->setConfig("jwt_secret",QString::fromLatin1(secret_.toHex()));
    } else secret_=QByteArray::fromHex(secret_);
    connect(&server_,&QTcpServer::newConnection,this,[this] {
        while (QTcpSocket *socket=server_.nextPendingConnection()) {
            connect(socket,&QTcpSocket::readyRead,this,[this,socket]{ receive(socket); });
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            connect(socket,&QTcpSocket::destroyed,this,[this,socket] {
                pending_.remove(socket);
                if(auto *state=uploadPending_.take(socket)) { delete state->file; delete state; }
            });
        }
    });
    externalSyncTimer_.setInterval(60000);
    auto syncDue=[this] {
        const QJsonObject config=ExternalDb::normalize(
            QJsonDocument::fromJson(db_->config("external_db_config").toUtf8()).object());
        if(!config.value("enabled").toBool()) return;
        const QString watermark=db_->config("external_db_last_sync_at");
        const QDateTime last=QDateTime::fromString(watermark,"yyyy-MM-dd HH:mm:ss");
        if(last.isValid() && last.secsTo(QDateTime::currentDateTime())<
            config.value("syncIntervalMinutes").toInt()*60) return;
        if(syncing_.exchange(true)) return;
        const QJsonObject source=db_->connectionOptions();
        auto failure=std::make_shared<QString>();
        QThread *worker=QThread::create([source,config,failure] { ExternalDb::sync(source,config,false,failure.get()); });
        connect(worker,&QThread::finished,this,[this,worker,failure] {
            syncing_=false;
            if(!failure->isEmpty()) emit requestLogged("外部数据库同步失败："+*failure);
            else emit requestLogged("外部数据库同步完成");
            worker->deleteLater();
        });
        worker->start();
    };
    connect(&externalSyncTimer_,&QTimer::timeout,this,syncDue);
    externalSyncTimer_.start();
    QTimer::singleShot(0,this,syncDue);
}

bool ApiServer::listen(quint16 port, QString *error) {
    if (server_.listen(QHostAddress::AnyIPv4,port)) return true;
    if(error) *error=server_.errorString();
    return false;
}

void ApiServer::receive(QTcpSocket *socket) {
    if(uploadPending_.contains(socket)) { receiveUpload(socket,socket->readAll()); return; }
    QByteArray &data=pending_[socket]; data+=socket->readAll();
    const qsizetype end=data.indexOf("\r\n\r\n");
    if(end<0) { if(data.size()>65536) socket->disconnectFromHost(); return; }
    if(end>65536) { socket->disconnectFromHost(); return; }
    const QByteArray head=data.left(end);
    const auto lines=head.split('\n');
    bool chunked=false;
    qint64 length=0;
    for (const auto &line:lines) {
        const QByteArray lower=line.trimmed().toLower();
        if(lower.startsWith("content-length:")) length=line.mid(line.indexOf(':')+1).trimmed().toLongLong();
        if(lower.startsWith("transfer-encoding:") && lower.contains("chunked")) chunked=true;
    }
    const QByteArray requestLine=lines.first().trimmed();
    if(requestLine.startsWith("POST /api/pattern/upload ") ||
       requestLine.startsWith("POST /api/pattern/upload?")) {
        auto *state=new UploadState;
        state->head=head; state->chunked=chunked; state->remaining=length;
        state->file=new QTemporaryFile(QDir(db_->dataDirectory()).filePath("pattern-upload-XXXXXX.tmp"));
        if(length<0 || !state->file->open()) {
            delete state->file; delete state; pending_.remove(socket); socket->disconnectFromHost(); return;
        }
        uploadPending_.insert(socket,state);
        const QByteArray body=data.mid(end+4);
        pending_.remove(socket);
        receiveUpload(socket,body);
        return;
    }
    const bool directUpload=requestLine.startsWith("POST ") && QUrl::fromEncoded(requestLine.split(' ').value(1)).path()=="/api/pattern/direct-upload";
    if(directUpload && (length>17*1024*1024 || data.size()>17*1024*1024+65536)) {pending_.remove(socket);socket->disconnectFromHost();return;}
    if(data.size()>maxHttpBody+65536) { socket->disconnectFromHost(); return; }
    if(chunked) {
        QByteArray body; bool complete=false;
        if(!decodeChunked(data.mid(end+4),&body,&complete)) { socket->disconnectFromHost(); return; }
        if(!complete) return;
        pending_.remove(socket);
        dispatch(socket,head+"\r\n\r\n"+body);
        return;
    }
    if(length<0 || length>maxHttpBody) { socket->disconnectFromHost(); return; }
    if(data.size()<end+4+length) return;
    const QByteArray request=data.left(end+4+length);
    pending_.remove(socket);
    dispatch(socket,request);
}

void ApiServer::receiveUpload(QTcpSocket *socket,const QByteArray &bytes) {
    UploadState *state=uploadPending_.value(socket,nullptr);
    if(!state) return;
    auto fail=[this,socket] {
        if(auto *old=uploadPending_.take(socket)) { delete old->file; delete old; }
        socket->disconnectFromHost();
    };
    bool complete=false;
    if(!state->chunked) {
        const qint64 count=qMin<qint64>(state->remaining,bytes.size());
        if(count>0 && state->file->write(bytes.constData(),count)!=count) { fail(); return; }
        state->remaining-=count; complete=state->remaining==0;
    } else {
        state->buffer+=bytes;
        while(true) {
            if(state->chunkRemaining==-2) {
                if(state->buffer.startsWith("\r\n") || state->buffer.contains("\r\n\r\n")) complete=true;
                break;
            }
            if(state->afterChunk) {
                if(state->buffer.size()<2) break;
                if(state->buffer.left(2)!="\r\n") { fail(); return; }
                state->buffer.remove(0,2); state->afterChunk=false; state->chunkRemaining=-1;
            }
            if(state->chunkRemaining<0) {
                const qsizetype end=state->buffer.indexOf("\r\n");
                if(end<0) { if(state->buffer.size()>16384) { fail(); return; } break; }
                bool valid=false;
                const qint64 size=state->buffer.left(end).split(';').first().trimmed().toLongLong(&valid,16);
                if(!valid || size<0) { fail(); return; }
                state->buffer.remove(0,end+2); state->chunkRemaining=size;
                if(size==0) {
                    state->chunkRemaining=-2;
                    if(state->buffer.startsWith("\r\n") || state->buffer.contains("\r\n\r\n")) complete=true;
                    break;
                }
            }
            const qint64 count=qMin<qint64>(state->chunkRemaining,state->buffer.size());
            if(count==0) break;
            if(state->file->write(state->buffer.constData(),count)!=count) { fail(); return; }
            state->buffer.remove(0,count); state->chunkRemaining-=count;
            if(state->chunkRemaining==0) state->afterChunk=true;
        }
    }
    if(!complete) return;
    state->file->flush();
    const QString path=state->file->fileName();
    const QByteArray head=state->head;
    uploadPending_.remove(socket);
    dispatch(socket,head+"\r\n\r\n",path);
    delete state->file; delete state;
}

QByteArray ApiServer::jsonBytes(const QJsonObject &body) { return QJsonDocument(body).toJson(QJsonDocument::Compact); }

void ApiServer::dispatch(QTcpSocket *socket, const QByteArray &bytes,const QString &uploadTempPath) {
    const qsizetype split=bytes.indexOf("\r\n\r\n");
    const auto lines=bytes.left(split).split('\n');
    const auto first=lines.first().trimmed().split(' ');
    Request req;
    req.peerAddress=socket->peerAddress().toString();
    if(first.size()>=2) {
        req.method=QString::fromLatin1(first[0]);
        QUrl url=QUrl::fromEncoded(first[1]);
        req.path=url.path(); req.query=QUrlQuery(url);
    }
    for(int i=1;i<lines.size();++i) {
        QByteArray line=lines[i].trimmed(); int at=line.indexOf(':');
        if(at>0) req.headers[QString::fromLatin1(line.left(at)).toLower()]=QString::fromUtf8(line.mid(at+1).trimmed());
    }
    req.rawBody=bytes.mid(split+4);
    req.uploadTempPath=uploadTempPath;
    if(!req.rawBody.isEmpty()) {
        const QJsonDocument parsed=QJsonDocument::fromJson(req.rawBody);
        req.body=parsed.object(); req.array=parsed.array();
    }
    Response response=req.method=="OPTIONS" ? ok() : route(req);
    const QByteArray payload=response.contentType.isEmpty() ? jsonBytes(response.body) : response.binary;
    const qint64 contentLength=response.filePath.isEmpty() ? payload.size() : QFileInfo(response.filePath).size();
    const QByteArray phrase=response.status==200 ? "OK" : response.status==400 ? "Bad Request" : response.status==401 ? "Unauthorized" : response.status==403 ? "Forbidden" : response.status==404 ? "Not Found" : "Error";
    QByteArray wire="HTTP/1.1 "+QByteArray::number(response.status)+" "+phrase+"\r\nContent-Type: "+
        (response.contentType.isEmpty() ? QByteArray("application/json; charset=utf-8") : response.contentType)+
        "\r\nContent-Length: "+QByteArray::number(contentLength)+
        "\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Authorization,Content-Type,X-Client-Version\r\n"
        "Access-Control-Allow-Methods: GET,POST,PUT,DELETE,OPTIONS\r\nAccess-Control-Expose-Headers: Content-Disposition\r\nConnection: close\r\n";
    if(!response.disposition.isEmpty()) wire+="Content-Disposition: "+response.disposition+"\r\n";
    wire+="\r\n";
    if(response.filePath.isEmpty()) {
        socket->write(wire+payload); socket->disconnectFromHost();
    } else {
        auto *source=new QFile(response.filePath,socket);
        if(!source->open(QIODevice::ReadOnly)) {
            if(response.removeFile) QFile::remove(response.filePath);
            socket->disconnectFromHost();
        } else {
            socket->write(wire);
            const QString path=response.filePath;
            const bool remove=response.removeFile;
            auto done=std::make_shared<bool>(false);
            auto pump=[socket,source,path,remove,done] {
                if(*done) return;
                while(socket->bytesToWrite()<512*1024 && !source->atEnd()) {
                    const QByteArray chunk=source->read(65536);
                    if(chunk.isEmpty() || socket->write(chunk)<0) break;
                }
                if(source->atEnd() && socket->bytesToWrite()==0) {
                    *done=true; source->close();
                    if(remove) QFile::remove(path);
                    socket->disconnectFromHost();
                }
            };
            connect(socket,&QTcpSocket::bytesWritten,socket,[pump](qint64){ pump(); });
            connect(socket,&QTcpSocket::disconnected,socket,[source,path,remove,done] {
                if(*done) return;
                *done=true; source->close(); if(remove) QFile::remove(path);
            });
            pump();
        }
    }
    if(!req.path.startsWith("/api/pattern/direct-"))emit requestLogged(req.method+" "+req.path+" "+QString::number(response.status));
}

ApiServer::Response ApiServer::ok(const QJsonValue &data, const QString &message) {
    QJsonObject body{{"code",0},{"message",message}};
    if(!data.isUndefined()) body["data"]=data;
    return {200,body};
}
ApiServer::Response ApiServer::fail(int status,const QString &message) { return {status,{{"code",status},{"message",message}}}; }

QString ApiServer::makeToken(const QJsonObject &user) const {
    const QByteArray head=b64(R"({"alg":"HS256","typ":"JWT"})");
    const QJsonObject payload{{"userId",user.value("id")},{"username",user.value("username")},{"role",user.value("role")},
        {"iat",QDateTime::currentSecsSinceEpoch()},{"exp",QDateTime::currentSecsSinceEpoch()+qint64(expireHours_)*3600},{"iss","boer-lan-server"}};
    const QByteArray unsignedToken=head+"."+b64(jsonBytes(payload));
    const QByteArray signature=QMessageAuthenticationCode::hash(unsignedToken,secret_,QCryptographicHash::Sha256);
    return QString::fromLatin1(unsignedToken+"."+b64(signature));
}

QJsonObject ApiServer::userForToken(const QString &header) const {
    if(!header.startsWith("Bearer ")) return {};
    const QList<QByteArray> parts=header.mid(7).toLatin1().split('.');
    if(parts.size()!=3) return {};
    const QByteArray unsignedToken=parts[0]+"."+parts[1];
    const QByteArray expected=b64(QMessageAuthenticationCode::hash(unsignedToken,secret_,QCryptographicHash::Sha256));
    if(expected!=parts[2]) return {};
    QJsonObject payload=QJsonDocument::fromJson(un64(parts[1])).object();
    if(payload.value("exp").toInteger()<QDateTime::currentSecsSinceEpoch()) return {};
    QJsonObject user=db_->row("users",payload.value("userId").toInteger());
    const QJsonArray roles=db_->rows("roles","name=?",{user.value("role").toString()});
    if(!roles.isEmpty()) user["permissions"]=roles.first().toObject().value("permissions");
    return user.value("disabled").toBool() ? QJsonObject{} : user;
}

ApiServer::Response ApiServer::login(const Request &req) {
    const QString username=req.body.value("username").toString().trimmed(), password=req.body.value("password").toString();
    QSqlQuery q(db_->connection()); q.prepare("SELECT id,password FROM users WHERE username=? AND deleted_at IS NULL"); q.addBindValue(username);
    bool valid=false; qint64 id=0;
    if(q.exec() && q.next()) { id=q.value(0).toLongLong(); valid=Database::checkPassword(password,q.value(1).toString()); }
    QJsonObject user=id ? db_->row("users",id) : QJsonObject{};
    db_->insert("login_logs",{{"userId",id},{"username",username},{"ip",req.peerAddress},{"device",req.headers.value("user-agent")},
        {"clientVersion",req.body.value("clientVersion")},{"status",valid && !user.value("disabled").toBool() ? "成功" : "失败"}},nullptr,nullptr);
    if(!valid) return fail(401,"账号或密码错误");
    if(user.value("disabled").toBool()) return fail(403,"账号已被禁用");
    const QString role=user.value("role").toString();
    auto roles=db_->rows("roles","name=?",{role});
    if(!roles.isEmpty()) user["permissions"]=roles.first().toObject().value("permissions");
    QJsonArray groupIds=QJsonDocument::fromJson(user.value("groupIds").toString().toUtf8()).array();
    user["groupIds"]=groupIds;
    user["createTime"]=user.value("createdAt");
    return ok(QJsonObject{{"token",makeToken(user)},{"serverVersion",("qt-"+QCoreApplication::applicationVersion())},{"deviceTcpPort",int(devicePort_)},{"user",user}});
}

ApiServer::Response ApiServer::route(const Request &req) {
    if(req.method=="GET" &&
       (req.path.startsWith("/uploads/avatars/") || req.path.startsWith("/uploads/patterns/"))) {
        const QString category=req.path.startsWith("/uploads/avatars/") ? "avatars" : "patterns";
        const QString fileName=QFileInfo(req.path).fileName();
        if(fileName.isEmpty() || req.path!="/uploads/"+category+"/"+fileName) return fail(404,"文件不存在");
        QFile file(QDir(db_->dataDirectory()).filePath("uploads/"+category+"/"+fileName));
        if(!file.open(QIODevice::ReadOnly)) return fail(404,"文件不存在");
        const QString suffix=QFileInfo(fileName).suffix().toLower();
        const QByteArray mime=category=="patterns" ? "application/octet-stream" :
            suffix=="png" ? "image/png" : suffix=="webp" ? "image/webp" : "image/jpeg";
        Response response; response.filePath=file.fileName(); response.contentType=mime; return response;
    }
    if(req.path=="/api/healthz") return ok(QJsonValue(),"ok");
    if(req.path=="/api/auth/login" && req.method=="POST") return login(req);
    if(req.path=="/api/system/firewall/status" && req.method=="GET") {
        if(!localConsole(req.peerAddress,req.headers.value("origin"),false))
            return fail(403,"仅允许在服务端电脑上检查防火墙");
        return ok(firewallStatus(server_.serverPort(),devicePort_));
    }
    if(req.path=="/api/system/firewall/repair" && req.method=="POST") {
        if(!localConsole(req.peerAddress,req.headers.value("origin"),true))
            return fail(403,"仅允许在服务端电脑上修复防火墙");
        const QString error=repairFirewall(server_.serverPort(),devicePort_);
        const QJsonObject status=firewallStatus(server_.serverPort(),devicePort_);
        if(!error.isEmpty()) return {200,{{"code",1},{"message","防火墙修复未完成："+error},{"data",status}}};
        return ok(status,"防火墙规则修复成功");
    }
    QJsonObject user=userForToken(req.headers.value("authorization"));
    if(user.isEmpty()) return fail(401,"未提供认证信息或认证已过期");
    QStringList p=pieces(req.path);
    if(p.size()<2 || p[0]!="api") return fail(404,"接口不存在");
    p.removeFirst(); const QString section=p.takeFirst();
    if(section=="auth") return auth(req,user);
    if(section=="system") return system(req,user,p);
    if(section=="statistics") return statistics(req,user,p);
    return collection(req,user,section,p);
}

ApiServer::Response ApiServer::auth(const Request &req,const QJsonObject &user) {
    if(req.path.endsWith("/logout") && req.method=="POST") return ok();
    if(req.path.endsWith("/userinfo") && req.method=="GET") {
        QJsonObject result=user;
        const auto roles=db_->rows("roles","name=?",{user.value("role").toString()});
        if(!roles.isEmpty()) result["permissions"]=roles.first().toObject().value("permissions");
        result["serverVersion"]=("qt-"+QCoreApplication::applicationVersion());
        return ok(result);
    }
    if(req.path.endsWith("/password") && req.method=="PUT") {
        const QString old=req.body.value("oldPassword").toString(), newer=req.body.value("newPassword").toString();
        if(newer.size()<6 || newer.size()>32 || newer==old) return fail(400,"新密码长度需在6-32位且不能与原密码相同");
        QSqlQuery q(db_->connection()); q.prepare("SELECT password FROM users WHERE id=?"); q.addBindValue(user.value("id").toInteger());
        if(!q.exec() || !q.next() || !Database::checkPassword(old,q.value(0).toString())) return fail(400,"原密码错误");
        QString error;
        if(!db_->update("users",user.value("id").toInteger(),{{"password",Database::hashPassword(newer)}},&error)) return fail(500,error);
        return ok(QJsonValue(),"密码修改成功");
    }
    if(req.path.endsWith("/profile") && req.method=="PUT") {
        QJsonObject body;
        for(const QString &key:{"nickname","phone","email"}) if(req.body.contains(key)) body[key]=req.body.value(key);
        QString error;
        if(!db_->update("users",user.value("id").toInteger(),body,&error)) return fail(500,error);
        return ok(QJsonValue(),"信息更新成功");
    }
    if(req.path.endsWith("/avatar") && req.method=="POST") {
        const Multipart form=parseMultipart(req.rawBody,req.headers.value("content-type"));
        if(form.fileName.isEmpty()) return fail(400,"请选择头像文件");
        const QString suffix=QFileInfo(form.fileName).suffix().toLower();
        if(!QStringList{"png","jpg","jpeg","webp"}.contains(suffix)) return fail(400,"头像仅支持 png/jpg/jpeg/webp 格式");
        if(form.file.size()>2*1024*1024) return fail(400,"头像文件不能超过 2MB");
        const QString directory=QDir(db_->dataDirectory()).filePath("uploads/avatars");
        if(!QDir().mkpath(directory)) return fail(500,"创建头像目录失败");
        const QString fileName=QDateTime::currentDateTime().toString("yyyyMMddHHmmsszzz")+"_user_"+
            QString::number(user.value("id").toInteger())+"_"+QString::number(QRandomGenerator::global()->generate())+"."+suffix;
        const QString path=QDir(directory).filePath(fileName), url="/uploads/avatars/"+fileName;
        QFile file(path);
        if(!file.open(QIODevice::WriteOnly) || file.write(form.file)!=form.file.size()) { file.remove(); return fail(500,"头像保存失败"); }
        file.close();
        QString error;
        if(!db_->update("users",user.value("id").toInteger(),{{"avatar",url}},&error)) { file.remove(); return fail(500,error); }
        const QString previous=user.value("avatar").toString();
        if(previous.startsWith("/uploads/avatars/") && QFileInfo(previous).fileName()==previous.section('/',-1))
            QFile::remove(QDir(directory).filePath(QFileInfo(previous).fileName()));
        return ok(QJsonObject{{"avatar",url}},"头像更新成功");
    }
    if(req.path.endsWith("/login-logs") && req.method=="GET") {
        const int page=qMax(1,req.query.queryItemValue("page",QUrl::FullyDecoded).toInt());
        const int requestedSize=req.query.queryItemValue("pageSize",QUrl::FullyDecoded).toInt();
        const int size=qBound(1,requestedSize>0 ? requestedSize : 10,200);
        const bool admin=adminRole(user);
        QJsonArray all=admin ? db_->rows("login_logs",{}, {},"id DESC",10000) : db_->rows("login_logs","user_id=?",{user.value("id").toInteger()},"id DESC",10000);
        const QString keyword=req.query.queryItemValue("keyword",QUrl::FullyDecoded).trimmed();
        const QString status=req.query.queryItemValue("status",QUrl::FullyDecoded).trimmed();
        const QString start=req.query.queryItemValue("startDate",QUrl::FullyDecoded).trimmed(), end=req.query.queryItemValue("endDate",QUrl::FullyDecoded).trimmed();
        QJsonArray filtered;
        for(const auto &value:all) {
            const QJsonObject item=value.toObject();
            if(!status.isEmpty() && item.value("status").toString()!=status) continue;
            const QString timestamp=item.value("loginTime").toString();
            if(!start.isEmpty() && timestamp.left(10)<start) continue;
            if(!end.isEmpty() && timestamp.left(10)>end) continue;
            if(!keyword.isEmpty() && !item.value("username").toString().contains(keyword,Qt::CaseInsensitive) &&
               !item.value("ip").toString().contains(keyword,Qt::CaseInsensitive)) continue;
            filtered.append(value);
        }
        all=filtered;
        QJsonArray list;
        for(int i=(page-1)*size;i<qMin(page*size,all.size());++i) list.append(all[i]);
        return ok(QJsonObject{{"list",list},{"total",all.size()},{"page",page},{"pageSize",size}});
    }
    return fail(404,"接口尚未实现");
}

ApiServer::Response ApiServer::statistics(const Request &req,const QJsonObject &user,const QStringList &parts) {
    if(req.method!="GET" || parts.isEmpty()) return fail(404,"接口不存在");
    const QString action=parts.first();
    const bool permitted=(action=="home" || action=="production-trend") ? hasPermission(user,{"home"}) :
        action=="dashboard" ? hasPermission(user,{"dashboard"}) :
        action=="salary" ? hasPermission(user,{"statistics","salaryStatistics"}) :
        action=="export" ? hasPermission(user,{"statistics","salaryStatistics","statusStatistics"}) :
        hasPermission(user,{"statistics","statusStatistics"});
    if(!permitted)
        return fail(403,"没有访问权限");
    if(action=="export") {
        if(parts.size()!=2 || !QStringList{"salary","process","duration","alarm"}.contains(parts[1]))
            return fail(400,"不支持的导出类型");
        const QString kind=parts[1];
        const QString mode=req.query.queryItemValue("mode",QUrl::FullyDecoded).isEmpty() ? "all" : req.query.queryItemValue("mode",QUrl::FullyDecoded);
        QStringList headers,fields;
        QString prefix;
        if(kind=="salary") {
            prefix=mode=="merged" ? "salary_stats_merged" : mode=="current" ? "salary_stats_current" : "salary_stats_all";
            if(mode=="merged") {
                headers={"员工工号","员工姓名","设备名称","加工件数","单价(元)","工资(元)","合计(元)"};
                fields={"employeeCode","employeeName","deviceName","totalPieces","unitPrice","salary","totalAmount"};
            } else {
                headers={"员工工号","员工姓名","设备名称","花型名称","订单号","加工件数","单价(元)","工资(元)","合计(元)","日期"};
                fields={"employeeCode","employeeName","deviceName","patternName","orderNo","totalPieces","unitPrice","salary","totalAmount","date"};
            }
        } else if(kind=="process") {
            prefix="process_overview";
            headers={"设备名称","员工工号","员工姓名","日期","花型名称","花型针数","平均缝纫速度(针/分钟)","开始时间","加工次数","平均加工时长(min/次)","花型缝纫次数","报警信息","报警时间","累计开机时长(h)"};
            fields={"deviceName","employeeCode","employeeName","date","patternName","patternStitches","sewSpeed","startTime","processCount","avgProcessDuration","patternSewCount","alarmInfo","alarmTime","cumulativeUpTime"};
        } else if(kind=="duration") {
            prefix="duration_stats";
            headers={"设备名称","员工工号","员工姓名","日期","花型名称","开始时间","结束时间","缝纫时长(h)","平均缝纫时长(min/次)"};
            fields={"deviceName","employeeCode","employeeName","date","patternName","startTime","endTime","sewDuration","avgSewDuration"};
        } else {
            prefix="alarm_stats";
            headers={"设备名称","员工工号","员工姓名","报警时间","报警信息","报警代码","持续时长","处理状态"};
            fields={"deviceName","employeeCode","employeeName","alarmTime","alarmInfo","alarmCode","duration","status"};
        }
        QByteArray csv="\xEF\xBB\xBF";
        for(int i=0;i<headers.size();++i) { if(i) csv+=','; csv+=csvField(headers[i]); }
        csv+='\n';
        const int requestedPage=qMax(1,req.query.queryItemValue("page",QUrl::FullyDecoded).toInt());
        qint64 processed=0,total=-1;
        QMap<QString,QJsonObject> mergedRows;
        for(int page=1;page<=100000;++page) {
            Request pageRequest=req;
            pageRequest.query.removeAllQueryItems("page");
            pageRequest.query.removeAllQueryItems("pageSize");
            pageRequest.query.addQueryItem("page",QString::number(mode=="current" ? requestedPage : page));
            const int requestedSize=req.query.queryItemValue("pageSize",QUrl::FullyDecoded).toInt();
            pageRequest.query.addQueryItem("pageSize",QString::number(mode=="current" ? qBound(1,requestedSize>0 ? requestedSize : 10,200) : 200));
            const Response result=statistics(pageRequest,user,{kind});
            if(result.status!=200 || result.body.value("code").toInt()!=0) return result;
            const QJsonObject data=result.body.value("data").toObject();
            const QJsonArray rows=data.value("list").toArray();
            if(total<0) total=data.value("total").toInteger();
            for(const QJsonValue &rowValue:rows) {
                const QJsonObject row=rowValue.toObject();
                if(kind=="salary" && mode=="merged") {
                    const QString key=row.value("employeeCode").toString()+"|"+row.value("deviceId").toVariant().toString()+
                        "|"+row.value("unitPrice").toVariant().toString();
                    QJsonObject current=mergedRows.value(key,row);
                    if(mergedRows.contains(key)) {
                        current["totalPieces"]=current.value("totalPieces").toInteger()+row.value("totalPieces").toInteger();
                        current["salary"]=current.value("salary").toDouble()+row.value("salary").toDouble();
                        current["totalAmount"]=current.value("totalAmount").toDouble()+row.value("totalAmount").toDouble();
                    }
                    mergedRows[key]=current;
                    continue;
                }
                for(int i=0;i<fields.size();++i) {
                    if(i) csv+=',';
                    csv+=csvField(row.value(fields[i]).toVariant().toString());
                }
                csv+='\n';
            }
            processed+=rows.size();
            if(mode=="current" || rows.isEmpty() || processed>=total) break;
        }
        if(kind=="salary" && mode=="merged") {
            QVector<QJsonObject> ranked;
            for(const QJsonObject &row:mergedRows) ranked.append(row);
            std::sort(ranked.begin(),ranked.end(),[](const QJsonObject &a,const QJsonObject &b) {
                return a.value("totalAmount").toDouble()>b.value("totalAmount").toDouble();
            });
            for(const QJsonObject &row:ranked) {
                for(int i=0;i<fields.size();++i) {
                    if(i) csv+=',';
                    csv+=csvField(row.value(fields[i]).toVariant().toString());
                }
                csv+='\n';
            }
        }
        Response response; response.binary=csv; response.contentType="text/csv; charset=utf-8";
        response.disposition=("attachment; filename=\""+prefix+"_"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")+".csv\"").toLatin1();
        return response;
    }

    QDate start=QDate::currentDate(), end=start;
    const QString startText=req.query.queryItemValue("startDate",QUrl::FullyDecoded), endText=req.query.queryItemValue("endDate",QUrl::FullyDecoded);
    if(!startText.isEmpty()) start=QDate::fromString(startText,"yyyy-MM-dd");
    if(!endText.isEmpty()) end=QDate::fromString(endText,"yyyy-MM-dd");
    if(!start.isValid() || !end.isValid() ||
       (!startText.isEmpty() && start.toString("yyyy-MM-dd")!=startText) ||
       (!endText.isEmpty() && end.toString("yyyy-MM-dd")!=endText)) return fail(400,"日期格式应为 yyyy-MM-dd");
    // A trend always has two effective endpoints: omitted dates default to today.
    // Validate those defaults as well, otherwise a single old date bypasses the cap.
    const bool boundedRange=action=="production-trend" || (!startText.isEmpty() && !endText.isEmpty());
    if(boundedRange && start>end) qSwap(start,end);
    if(boundedRange && start.daysTo(end)>366) return fail(400,"查询时间跨度不能超过一年");

    QString scope="1=1";
    if(action=="production-trend") {
        QMap<QString,qint64> totals;
        QSqlQuery query(db_->connection());
        query.prepare("SELECT substr(pr.record_date,1,10),COALESCE(SUM(pr.pieces),0) "
                      "FROM production_records pr JOIN devices d ON d.id=pr.device_id "
                      "WHERE pr.deleted_at IS NULL AND d.deleted_at IS NULL AND "+scope+
                      " AND substr(pr.record_date,1,10) BETWEEN ? AND ? GROUP BY substr(pr.record_date,1,10)");
        query.addBindValue(start.toString("yyyy-MM-dd")); query.addBindValue(end.toString("yyyy-MM-dd"));
        if(!query.exec()) return fail(500,query.lastError().text());
        while(query.next()) totals[query.value(0).toString()]=query.value(1).toLongLong();
        QJsonArray trend;
        for(QDate day=start;day<=end;day=day.addDays(1)) {
            const QString key=day.toString("yyyy-MM-dd");
            trend.append(QJsonObject{{"date",key},{"pieces",totals.value(key)}});
        }
        return ok(QJsonObject{{"productionTrend",trend}});
    }
    if(action=="device-patterns") {
        QString where="pr.deleted_at IS NULL AND "+scope;
        QVariantList binds;
        if(!startText.isEmpty()) { where+=" AND substr(pr.record_date,1,10)>=?"; binds << start.toString("yyyy-MM-dd"); }
        if(!endText.isEmpty()) { where+=" AND substr(pr.record_date,1,10)<=?"; binds << end.toString("yyyy-MM-dd"); }
        const QString selected=req.query.queryItemValue("deviceId",QUrl::FullyDecoded).trimmed();
        if(!selected.isEmpty()) {
            bool valid=false; const qint64 id=selected.toLongLong(&valid);
            if(!valid || id<1) return fail(400,"设备ID不合法");
            where+=" AND pr.device_id=?"; binds << id;
        }
        appendDeviceIds(req.query,"pr",&where,&binds);
        appendDeviceName(db_,req.query,"pr",&where);
        QSqlQuery records(db_->connection());
        records.prepare("SELECT pr.id,pr.pattern_id,pr.pattern_no,"
                        "COALESCE(NULLIF(pr.pattern_name,''),p.name,'未命名花型'),"
                        "pr.pieces,pr.running_time,pr.start_time,pr.end_time,pr.created_at,"
                        "COALESCE(d.name,''),pr.protocol_user_id,pr.stop_reason,pr.record_date FROM production_records pr "
                        "LEFT JOIN devices d ON d.id=pr.device_id LEFT JOIN patterns p ON p.id=pr.pattern_id "
                        "WHERE "+where+" ORDER BY COALESCE(pr.end_time,pr.start_time,pr.created_at),pr.id");
        for(const QVariant &bind:binds) records.addBindValue(bind);
        if(!records.exec()) return fail(500,records.lastError().text());
        QJsonArray details, aggregateTable;
        QMap<QString,int> indices, pieceIndices;
        QVector<QJsonObject> aggregates;
        while(records.next()) {
            const qint64 patternId=records.value(1).toLongLong();
            const int patternNo=records.value(2).toInt(), pieces=records.value(4).toInt();
            const QString patternName=records.value(3).toString();
            const QString key=patternId>0 ? "id:"+QString::number(patternId) : "name:"+patternName.toLower();
            const QString startValue=sqlText(records.value(6)),endValue=sqlText(records.value(7));
            const QDateTime started=QDateTime::fromString(startValue,"yyyy-MM-dd HH:mm:ss");
            const QDateTime stopped=QDateTime::fromString(endValue,"yyyy-MM-dd HH:mm:ss");
            const double hours=started.isValid() && stopped.isValid() && stopped>started ?
                started.secsTo(stopped)/3600.0 : records.value(5).toDouble();
            const auto display=productionDisplayRange(sqlText(records.value(12)),sqlText(records.value(8)),
                startValue,endValue,hours);
            const QString completed=!endValue.isEmpty() ? endValue : !startValue.isEmpty() ? startValue : sqlText(records.value(8));
            if(!indices.contains(key)) {
                indices[key]=aggregates.size();
                aggregates.append(QJsonObject{{"patternNo",patternNo},{"patternName",patternName},
                    {"totalPieces",0},{"totalDuration",0.0},{"lastCompleted","-"},{"recentInfo",""}});
            }
            QJsonObject aggregate=aggregates[indices[key]];
            aggregate["totalPieces"]=aggregate.value("totalPieces").toInt()+pieces;
            aggregate["totalDuration"]=aggregate.value("totalDuration").toDouble()+hours;
            if(completed>aggregate.value("lastCompleted").toString()) {
                aggregate["lastCompleted"]=completed;
                aggregate["recentInfo"]=QString("设备:%1 / 工号:%2 / 停止原因:%3")
                    .arg(records.value(9).toString(),records.value(10).toString())
                    .arg(records.value(11).toInt());
            }
            aggregates[indices[key]]=aggregate;
            pieceIndices[key]++;
            details.append(QJsonObject{{"id",records.value(0).toLongLong()},{"patternNo",patternNo},
                {"patternName",patternName},{"pieceIndex",pieceIndices.value(key)},
                {"startTime",display.first},{"endTime",display.second},
                {"durationHours",qRound64(hours*1000.0)/1000.0},{"durationText",durationText(hours)}});
        }
        std::sort(aggregates.begin(),aggregates.end(),[](const QJsonObject &a,const QJsonObject &b) {
            const QString left=a.value("lastCompleted").toString(), right=b.value("lastCompleted").toString();
            return left==right ? a.value("patternName").toString()<b.value("patternName").toString() : left>right;
        });
        for(QJsonObject aggregate:aggregates) {
            const double hours=aggregate.value("totalDuration").toDouble();
            aggregate["totalDuration"]=qRound64(hours*1000.0)/1000.0;
            aggregate["totalDurationText"]=durationText(hours);
            aggregateTable.append(aggregate);
        }
        return ok(QJsonObject{{"patternTable",aggregateTable},{"detailTable",details}});
    }
    if(action=="process") {
        QString where="pr.deleted_at IS NULL AND "+scope;
        QVariantList binds;
        if(!startText.isEmpty()) { where+=" AND substr(pr.record_date,1,10)>=?"; binds << start.toString("yyyy-MM-dd"); }
        if(!endText.isEmpty()) { where+=" AND substr(pr.record_date,1,10)<=?"; binds << end.toString("yyyy-MM-dd"); }
        const QString deviceText=req.query.queryItemValue("deviceId",QUrl::FullyDecoded).trimmed();
        if(!deviceText.isEmpty()) {
            bool valid=false; const qint64 id=deviceText.toLongLong(&valid);
            if(!valid || id<1) return fail(400,"设备ID不合法");
            where+=" AND pr.device_id=?"; binds << id;
        }
        appendDeviceIds(req.query,"pr",&where,&binds);
        appendDeviceName(db_,req.query,"pr",&where);
        QSqlQuery records(db_->connection());
        records.prepare(QString("SELECT pr.id,pr.device_id,COALESCE(d.name,%1),")
                        .arg(db_->isMySql() ? "CONCAT('设备-',pr.device_id)" : "'设备-'||pr.device_id")+
                        "COALESCE(e.code,pr.protocol_user_id,'-'),COALESCE(e.name,'-'),"
                        "substr(pr.record_date,1,10),COALESCE(NULLIF(pr.pattern_name,''),p.name,'未命名花型'),"
                        "COALESCE(NULLIF(p.stitches,0),pr.stitches,0),pr.start_time,pr.end_time,"
                        "COALESCE(pr.pieces,0),COALESCE(pr.stitches,0),COALESCE(pr.thread_length,0),"
                        "COALESCE(pr.running_time,0),COALESCE(pr.idle_time,0),pr.pattern_id "
                        "FROM production_records pr LEFT JOIN devices d ON d.id=pr.device_id "
                        "LEFT JOIN employees e ON e.id=pr.employee_id LEFT JOIN patterns p ON p.id=pr.pattern_id "
                        "WHERE "+where+" ORDER BY pr.record_date DESC,pr.id DESC");
        for(const QVariant &bind:binds) records.addBindValue(bind);
        if(!records.exec()) return fail(500,records.lastError().text());
        QJsonArray all, list, trend, distribution;
        const auto processAlarms=alarmWindows(db_,
            startText.isEmpty() ? QString() : start.toString("yyyy-MM-dd"),
            endText.isEmpty() ? QString() : end.toString("yyyy-MM-dd"));
        QMap<QString,qint64> byDate, byDevice, byPatternDay;
        QMap<QString,QPair<double,double>> hoursByDeviceDay, hoursByDate;
        const QDate runtimeFirst=startText.isEmpty() ? (endText.isEmpty() ? QDate::currentDate() : QDate(1970,1,1)) : start;
        const QDate runtimeLast=endText.isEmpty() ? QDate::currentDate() : end;
        const auto runtimeSessions=sessionHours(db_,runtimeFirst,runtimeLast);
        qint64 totalPieces=0; double totalThread=0,totalHours=0,totalProcessing=0;
        while(records.next()) {
            const qint64 deviceId=records.value(1).toLongLong(), pieces=records.value(10).toLongLong();
            const QString deviceName=records.value(2).toString(), date=records.value(5).toString();
            const QString patternName=records.value(6).toString();
            const QDateTime started=QDateTime::fromString(sqlText(records.value(8)),"yyyy-MM-dd HH:mm:ss");
            const QDateTime ended=QDateTime::fromString(sqlText(records.value(9)),"yyyy-MM-dd HH:mm:ss");
            const double hours=started.isValid() && ended.isValid() && ended>started ?
                started.secsTo(ended)/3600.0 : records.value(13).toDouble();
            const double idle=records.value(14).toDouble();
            const QString deviceDay=QString::number(deviceId)+"|"+date;
            const QString patternDay=deviceDay+"|"+QString::number(records.value(15).toLongLong())+"|"+patternName;
            totalPieces+=pieces; totalThread+=records.value(12).toDouble();
            byDate[date]+=pieces; byDevice[deviceName]+=pieces;
            byPatternDay[patternDay]+=pieces;
            auto day=hoursByDeviceDay.value(deviceDay); day.first+=hours; day.second+=idle; hoursByDeviceDay[deviceDay]=day;
            auto dateHours=hoursByDate.value(date); dateHours.first+=hours; dateHours.second+=idle; hoursByDate[date]=dateHours;
            QJsonObject item{{"id",records.value(0).toLongLong()},{"deviceId",deviceId},{"deviceName",deviceName},
                {"employeeCode",records.value(3).toString()},{"employeeName",records.value(4).toString()},
                {"date",date},{"patternName",patternName},{"patternStitches",records.value(7).toLongLong()},
                {"startTime",sqlText(records.value(8))},{"endTime",sqlText(records.value(9))},
                {"processCount",pieces},
                {"totalPieces",pieces},{"totalStitches",records.value(11).toLongLong()},
                {"threadLength",records.value(12).toDouble()},{"runningTime",hours},
                {"idleTime",idle},{"patternDayKey",patternDay},{"deviceDayKey",deviceDay}};
            all.append(item);
        }
        QMap<QString,double> runtimeByDate;
        for(auto it=hoursByDeviceDay.cbegin();it!=hoursByDeviceDay.cend();++it) {
            const QString date=it.key().section('|',1,1);
            if(date<runtimeFirst.toString("yyyy-MM-dd") || date>runtimeLast.toString("yyyy-MM-dd")) continue;
            const qint64 deviceId=it.key().section('|',0,0).toLongLong();
            totalProcessing+=it.value().first;
            const double runtime=runtimeForDay(runtimeSessions,date,deviceId,it.value());
            totalHours+=runtime; runtimeByDate[date]+=runtime;
        }
        for(auto day=runtimeSessions.cbegin();day!=runtimeSessions.cend();++day)
            for(auto device=day.value().cbegin();device!=day.value().cend();++device)
                if(!hoursByDeviceDay.contains(QString::number(device.key())+"|"+day.key())) {
                    totalHours+=device.value(); runtimeByDate[day.key()]+=device.value();
                }
        const double efficiency=totalHours>0 ? qBound(0.0,totalProcessing/totalHours*100.0,100.0) : 0;
        const int page=qMax(1,req.query.queryItemValue("page",QUrl::FullyDecoded).toInt());
        const int rawSize=req.query.queryItemValue("pageSize",QUrl::FullyDecoded).toInt();
        const int size=qBound(1,rawSize>0 ? rawSize : 10,200);
        for(int i=(page-1)*size;i<qMin(page*size,all.size());++i) {
            QJsonObject item=all[i].toObject();
            const QString deviceDay=item.take("deviceDayKey").toString();
            const QString patternDay=item.take("patternDayKey").toString();
            const QString endValue=item.take("endTime").toString();
            const double processing=item.value("runningTime").toDouble();
            const double runtime=runtimeForDay(runtimeSessions,item.value("date").toString(),
                item.value("deviceId").toInteger(),hoursByDeviceDay.value(deviceDay));
            const qint64 pieces=item.value("processCount").toInteger();
            item["sewSpeed"]=processing>0 ? qRound64(item.value("totalStitches").toDouble()/(processing*60.0)*100.0)/100.0 : 0;
            item["avgProcessDuration"]=pieces>0 ? qRound64(processing*60.0/pieces*100.0)/100.0 : 0;
            item["patternSewCount"]=byPatternDay.value(patternDay);
            const QDateTime processStart=QDateTime::fromString(item.value("startTime").toString(),"yyyy-MM-dd HH:mm:ss");
            QDateTime processEnd=QDateTime::fromString(endValue,"yyyy-MM-dd HH:mm:ss");
            if(!processEnd.isValid() || processEnd<processStart) processEnd=processStart;
            item["alarmInfo"]="无"; item["alarmTime"]="无";
            for(const AlarmWindow &window:processAlarms)
                if(window.deviceId==item.value("deviceId").toInteger() && processStart.isValid() &&
                   window.started<=processEnd && window.ended>=processStart) {
                    item["alarmInfo"]=window.name; item["alarmTime"]=window.time; break;
                }
            item["cumulativeUpTime"]=runtime;
            item["efficiency"]=runtime>0 ? qBound(0.0,hoursByDeviceDay.value(deviceDay).first/runtime*100.0,100.0) : 0;
            list.append(item);
        }
        auto appendTrend=[&](const QString &date) {
            const auto hours=hoursByDate.value(date);
            const double runtime=runtimeByDate.value(date);
            trend.append(QJsonObject{{"date",date},{"pieces",byDate.value(date)},{"value",byDate.value(date)},
                {"efficiency",runtime>0 ? qBound(0.0,hours.first/runtime*100.0,100.0) : 0}});
        };
        if(!startText.isEmpty() && !endText.isEmpty())
            for(QDate day=start;day<=end;day=day.addDays(1)) appendTrend(day.toString("yyyy-MM-dd"));
        else for(auto it=byDate.cbegin();it!=byDate.cend();++it) appendTrend(it.key());
        QVector<QPair<QString,qint64>> deviceRanking;
        for(auto it=byDevice.cbegin();it!=byDevice.cend();++it) deviceRanking.append({it.key(),it.value()});
        std::sort(deviceRanking.begin(),deviceRanking.end(),[](const auto &a,const auto &b){ return a.second>b.second; });
        for(int i=0;i<qMin(10,deviceRanking.size());++i)
            distribution.append(QJsonObject{{"name",deviceRanking[i].first},{"value",deviceRanking[i].second}});
        const QJsonObject overview{{"totalPieces",totalPieces},{"totalThread",qRound64(totalThread*100.0)/100.0},
            {"totalHours",qRound64(totalHours*100.0)/100.0},{"avgEfficiency",qRound64(efficiency*100.0)/100.0}};
        return ok(QJsonObject{{"overview",overview},{"list",list},{"total",all.size()},
            {"productionTrend",trend},{"deviceDistribution",distribution},
            {"totalPieces",totalPieces},{"totalThread",overview.value("totalThread")},
            {"totalHours",overview.value("totalHours")},{"avgEfficiency",overview.value("avgEfficiency")}});
    }
    if(action=="duration") {
        QString productionWhere="pr.deleted_at IS NULL AND "+scope;
        QString alarmWhere="ar.deleted_at IS NULL AND "+scope;
        QVariantList productionBinds,alarmBinds;
        if(!startText.isEmpty()) {
            productionWhere+=" AND substr(pr.record_date,1,10)>=?";
            alarmWhere+=" AND substr(ar.start_time,1,10)>=?";
            productionBinds << start.toString("yyyy-MM-dd"); alarmBinds << start.toString("yyyy-MM-dd");
        }
        if(!endText.isEmpty()) {
            productionWhere+=" AND substr(pr.record_date,1,10)<=?";
            alarmWhere+=" AND substr(ar.start_time,1,10)<=?";
            productionBinds << end.toString("yyyy-MM-dd"); alarmBinds << end.toString("yyyy-MM-dd");
        }
        const QString selected=req.query.queryItemValue("deviceId",QUrl::FullyDecoded).trimmed();
        if(!selected.isEmpty()) {
            bool valid=false; const qint64 id=selected.toLongLong(&valid);
            if(!valid || id<1) return fail(400,"设备ID不合法");
            productionWhere+=" AND pr.device_id=?"; alarmWhere+=" AND ar.device_id=?";
            productionBinds << id; alarmBinds << id;
        }
        appendDeviceIds(req.query,"pr",&productionWhere,&productionBinds);
        appendDeviceIds(req.query,"ar",&alarmWhere,&alarmBinds);
        appendDeviceName(db_,req.query,"pr",&productionWhere);
        appendDeviceName(db_,req.query,"ar",&alarmWhere);
        bool runtimeRestricted=false;
        const QSet<qint64> runtimeAllowed=matchingDeviceIds(db_,req.query,&runtimeRestricted);
        const QDate runtimeFirst=startText.isEmpty() ? (endText.isEmpty() ? QDate::currentDate() : QDate(1970,1,1)) : start;
        const QDate runtimeLast=endText.isEmpty() ? QDate::currentDate() : end;
        const auto runtimeSessions=sessionHours(db_,runtimeFirst,runtimeLast);
        QMap<QString,QPair<double,double>> dateHours,deviceHours,productionByDeviceDay;
        QMap<QString,double> runtimeByDate,runtimeByDevice;
        QMap<QString,double> dateAlarms,deviceAlarms;
        QMap<QString,QString> deviceNames;
        QJsonArray all,list,pie,trend,deviceStats;
        QSqlQuery records(db_->connection());
        records.prepare(QString("SELECT pr.id,pr.device_id,COALESCE(d.name,%1),")
                        .arg(db_->isMySql() ? "CONCAT('设备-',pr.device_id)" : "'设备-'||pr.device_id")+
                        "COALESCE(e.code,pr.protocol_user_id,'-'),COALESCE(e.name,'-'),"
                        "substr(pr.record_date,1,10),COALESCE(NULLIF(pr.pattern_name,''),p.name,'未命名花型'),"
                        "pr.start_time,pr.end_time,COALESCE(pr.running_time,0),COALESCE(pr.idle_time,0),"
                        "COALESCE(pr.pieces,0) FROM production_records pr "
                        "LEFT JOIN devices d ON d.id=pr.device_id LEFT JOIN employees e ON e.id=pr.employee_id "
                        "LEFT JOIN patterns p ON p.id=pr.pattern_id WHERE "+productionWhere+
                        " ORDER BY pr.record_date DESC,pr.id DESC");
        for(const QVariant &bind:productionBinds) records.addBindValue(bind);
        if(!records.exec()) return fail(500,records.lastError().text());
        while(records.next()) {
            const QString date=records.value(5).toString(), deviceKey=records.value(1).toString();
            const QDateTime startAt=QDateTime::fromString(sqlText(records.value(7)),"yyyy-MM-dd HH:mm:ss");
            const QDateTime endAt=QDateTime::fromString(sqlText(records.value(8)),"yyyy-MM-dd HH:mm:ss");
            const double processing=startAt.isValid() && endAt.isValid() && endAt>startAt ?
                startAt.secsTo(endAt)/3600.0 : records.value(9).toDouble();
            const double idle=records.value(10).toDouble();
            if(date>=runtimeFirst.toString("yyyy-MM-dd") && date<=runtimeLast.toString("yyyy-MM-dd")) {
                auto dateValue=dateHours.value(date); dateValue.first+=processing; dateValue.second+=idle; dateHours[date]=dateValue;
                auto deviceValue=deviceHours.value(deviceKey); deviceValue.first+=processing; deviceValue.second+=idle; deviceHours[deviceKey]=deviceValue;
                auto bucket=productionByDeviceDay.value(deviceKey+"|"+date);
                bucket.first+=processing; bucket.second+=idle;
                productionByDeviceDay[deviceKey+"|"+date]=bucket;
                deviceNames[deviceKey]=records.value(2).toString();
            }
            const qint64 pieces=records.value(11).toLongLong();
            all.append(QJsonObject{{"id",records.value(0).toLongLong()},{"deviceName",records.value(2).toString()},
                {"employeeCode",records.value(3).toString()},{"employeeName",records.value(4).toString()},
                {"date",date},{"patternName",records.value(6).toString()},
                {"startTime",sqlText(records.value(7))},{"endTime",sqlText(records.value(8))},
                {"sewDuration",processing},{"sewDurationSeconds",qRound64(processing*3600.0)},
                {"avgSewDuration",pieces>0 ? processing*60.0/pieces : 0.0},
                {"avgSewDurationSeconds",pieces>0 ? qRound64(processing*3600.0/pieces) : 0},
                {"totalTime",processing+idle},{"runningTime",processing},{"idleTime",idle}});
        }
        QSqlQuery alarms(db_->connection());
        alarms.prepare("SELECT ar.device_id,substr(ar.start_time,1,10),COALESCE(ar.duration,0) "
                       "FROM alarm_records ar LEFT JOIN devices d ON d.id=ar.device_id WHERE "+alarmWhere);
        for(const QVariant &bind:alarmBinds) alarms.addBindValue(bind);
        if(!alarms.exec()) return fail(500,alarms.lastError().text());
        while(alarms.next()) {
            const double hours=alarms.value(2).toDouble()/3600.0;
            dateAlarms[alarms.value(1).toString()]+=hours;
            deviceAlarms[alarms.value(0).toString()]+=hours;
        }
        for(auto it=productionByDeviceDay.cbegin();it!=productionByDeviceDay.cend();++it) {
            const QString deviceKey=it.key().section('|',0,0),date=it.key().section('|',1,1);
            const double runtime=runtimeForDay(runtimeSessions,date,deviceKey.toLongLong(),it.value());
            runtimeByDate[date]+=runtime; runtimeByDevice[deviceKey]+=runtime;
        }
        for(auto day=runtimeSessions.cbegin();day!=runtimeSessions.cend();++day)
            for(auto device=day.value().cbegin();device!=day.value().cend();++device) {
                if(runtimeRestricted && !runtimeAllowed.contains(device.key())) continue;
                const QString deviceKey=QString::number(device.key());
                if(productionByDeviceDay.contains(deviceKey+"|"+day.key())) continue;
                runtimeByDate[day.key()]+=device.value(); runtimeByDevice[deviceKey]+=device.value();
            }
        double processingTotal=0,runtimeTotal=0,alarmTotal=0;
        for(const auto &hours:dateHours) processingTotal+=hours.first;
        for(double runtime:runtimeByDate) runtimeTotal+=runtime;
        for(double hours:dateAlarms) alarmTotal+=hours;
        const double total=qMax(runtimeTotal,processingTotal+alarmTotal);
        const double idle=qMax(0.0,total-processingTotal-alarmTotal);
        const auto rounded=[](double value){ return qRound64(value*1000000.0)/1000000.0; };
        const QJsonObject summary{{"totalTime",rounded(total)},{"runningTime",rounded(processingTotal)},
            {"idleTime",rounded(idle)},{"alarmTime",rounded(alarmTotal)}};
        pie.append(QJsonObject{{"name","加工时长"},{"value",rounded(processingTotal)}});
        pie.append(QJsonObject{{"name","空闲时长"},{"value",rounded(idle)}});
        pie.append(QJsonObject{{"name","报警时长"},{"value",rounded(alarmTotal)}});
        QSet<QString> dates;
        for(auto it=dateHours.cbegin();it!=dateHours.cend();++it) dates.insert(it.key());
        for(auto it=dateAlarms.cbegin();it!=dateAlarms.cend();++it) dates.insert(it.key());
        for(auto it=runtimeByDate.cbegin();it!=runtimeByDate.cend();++it) dates.insert(it.key());
        QStringList sortedDates=dates.values(); sortedDates.sort();
        for(const QString &date:sortedDates) {
            const auto hours=dateHours.value(date); const double alarms=dateAlarms.value(date);
            const double dayTotal=qMax(runtimeByDate.value(date),hours.first+alarms);
            trend.append(QJsonObject{{"date",date},{"totalTime",rounded(dayTotal)},
                {"runningTime",rounded(hours.first)},{"idleTime",rounded(qMax(0.0,dayTotal-hours.first-alarms))},
                {"alarmTime",rounded(alarms)}});
        }
        QSet<QString> deviceKeys;
        for(auto it=deviceHours.cbegin();it!=deviceHours.cend();++it) deviceKeys.insert(it.key());
        for(auto it=deviceAlarms.cbegin();it!=deviceAlarms.cend();++it) deviceKeys.insert(it.key());
        for(auto it=runtimeByDevice.cbegin();it!=runtimeByDevice.cend();++it) deviceKeys.insert(it.key());
        QVector<QJsonObject> ranked;
        for(const QString &key:deviceKeys) {
            const auto hours=deviceHours.value(key); const double alarms=deviceAlarms.value(key);
            const double deviceTotal=qMax(runtimeByDevice.value(key),hours.first+alarms);
            QString deviceName=deviceNames.value(key);
            if(deviceName.isEmpty()) {
                const QJsonObject device=db_->row("devices",key.toLongLong());
                deviceName=device.value("name").toString().trimmed();
                if(deviceName.isEmpty()) deviceName=device.value("code").toString().trimmed();
            }
            if(deviceName.isEmpty()) deviceName="设备#"+key;
            ranked.append(QJsonObject{{"name",deviceName},{"totalTime",rounded(deviceTotal)},
                {"runningTime",rounded(hours.first)},{"idleTime",rounded(qMax(0.0,deviceTotal-hours.first-alarms))},
                {"alarmTime",rounded(alarms)}});
        }
        std::sort(ranked.begin(),ranked.end(),[](const QJsonObject &a,const QJsonObject &b){
            return a.value("totalTime").toDouble()>b.value("totalTime").toDouble();
        });
        for(int i=0;i<qMin(10,ranked.size());++i) deviceStats.append(ranked[i]);
        const int page=qMax(1,req.query.queryItemValue("page",QUrl::FullyDecoded).toInt());
        const int rawSize=req.query.queryItemValue("pageSize",QUrl::FullyDecoded).toInt();
        const int size=qBound(1,rawSize>0 ? rawSize : 10,200);
        for(int i=(page-1)*size;i<qMin(page*size,all.size());++i) list.append(all[i]);
        return ok(QJsonObject{{"summary",summary},{"list",list},{"total",all.size()},
            {"durationPie",pie},{"durationTrend",trend},{"deviceStats",deviceStats},
            {"totalTime",summary.value("totalTime")},{"runningTime",summary.value("runningTime")},
            {"idleTime",summary.value("idleTime")},{"alarmTime",summary.value("alarmTime")}});
    }
    if(action=="salary" && parts.size()==1) {
        QString where="sr.deleted_at IS NULL AND "+scope;
        QVariantList binds;
        const QString employeeText=req.query.queryItemValue("employeeId",QUrl::FullyDecoded).trimmed();
        if(!employeeText.isEmpty()) {
            bool valid=false; const qint64 employeeId=employeeText.toLongLong(&valid);
            if(!valid || employeeId<1) return fail(400,"员工ID不合法");
            where+=" AND sr.employee_id=?"; binds << employeeId;
        }
        const QString employeeKeyword=req.query.queryItemValue("employeeKeyword",QUrl::FullyDecoded).trimmed();
        if(!employeeKeyword.isEmpty()) {
            where+=" AND (e.name LIKE ? OR e.code LIKE ?)";
            binds << "%"+employeeKeyword+"%" << "%"+employeeKeyword+"%";
        }
        const QString deviceText=req.query.queryItemValue("deviceId",QUrl::FullyDecoded).trimmed();
        if(!deviceText.isEmpty()) {
            bool valid=false; const qint64 deviceId=deviceText.toLongLong(&valid);
            if(!valid || deviceId<1) return fail(400,"设备ID不合法");
            where+=" AND sr.device_id=?"; binds << deviceId;
        }
        const QString deviceIdsText=req.query.queryItemValue("deviceIds",QUrl::FullyDecoded).trimmed();
        if(!deviceIdsText.isEmpty()) {
            if(deviceIdsText=="0") where+=" AND sr.device_id=0";
            else {
            QStringList ids;
            for(const QString &part:deviceIdsText.split(',',Qt::SkipEmptyParts)) {
                bool valid=false; const qint64 deviceId=part.trimmed().toLongLong(&valid);
                if(!valid || deviceId<1) return fail(400,"设备ID不合法");
                ids << QString::number(deviceId);
            }
            if(!ids.isEmpty()) where+=" AND sr.device_id IN ("+ids.join(',')+")";
            }
        }
        appendDeviceName(db_,req.query,"sr",&where);
        const QString whereNoDate=where;
        const QVariantList bindsNoDate=binds;
        if(!startText.isEmpty()) { where+=" AND substr(sr.record_date,1,10)>=?"; binds << start.toString("yyyy-MM-dd"); }
        if(!endText.isEmpty()) { where+=" AND substr(sr.record_date,1,10)<=?"; binds << end.toString("yyyy-MM-dd"); }
        const QString base=" FROM salary_records sr LEFT JOIN employees e ON e.id=sr.employee_id "
                           "LEFT JOIN devices d ON d.id=sr.device_id LEFT JOIN patterns p ON p.id=sr.pattern_id WHERE "+where;
        QSqlQuery records(db_->connection());
        records.prepare("SELECT sr.id,sr.employee_id,COALESCE(e.name,'未知员工'),COALESCE(e.code,''),"
                        "sr.device_id,COALESCE(d.name,''),COALESCE(NULLIF(sr.pattern_name,''),p.name,''),"
                        "COALESCE(NULLIF(p.order_no,''),sr.order_no,''),COALESCE(sr.pieces,0),"
                        "COALESCE(NULLIF(p.unit_price,0),"
                        "NULLIF((SELECT p_live.unit_price FROM patterns p_live WHERE p_live.deleted_at IS NULL "
                        "AND TRIM(p_live.name)=TRIM(sr.pattern_name) ORDER BY p_live.updated_at DESC,p_live.id DESC LIMIT 1),0),"
                        "sr.unit_price,0),substr(sr.record_date,1,10)"+base+
                        " ORDER BY sr.record_date DESC,sr.id DESC");
        for(const auto &value:binds) records.addBindValue(value);
        if(!records.exec()) return fail(500,records.lastError().text());
        QJsonArray all, rank, trend;
        QMap<qint64,QPair<QString,double>> byEmployee;
        QMap<QString,double> byDate;
        double totalSalary=0; qint64 totalPieces=0;
        while(records.next()) {
            const qint64 employeeId=records.value(1).toLongLong();
            const qint64 pieces=records.value(8).toLongLong();
            const double price=records.value(9).toDouble();
            const double amount=qRound64(pieces*price*100.0)/100.0;
            totalSalary+=amount; totalPieces+=pieces;
            const QString date=records.value(10).toString(), name=records.value(2).toString();
            auto current=byEmployee.value(employeeId); current.first=name; current.second+=amount; byEmployee[employeeId]=current;
            byDate[date]+=amount;
            all.append(QJsonObject{{"id",records.value(0).toLongLong()},{"employeeId",employeeId},
                {"employeeName",name},{"employeeCode",records.value(3).toString()},
                {"deviceId",records.value(4).toLongLong()},{"deviceName",records.value(5).toString()},
                {"workDays",1},{"patternName",records.value(6).toString()},
                {"orderNo",records.value(7).toString()},{"totalPieces",pieces},
                {"unitPrice",price},{"salary",amount},{"totalAmount",amount},{"date",date}});
        }
        if(all.isEmpty() && req.query.queryItemValue("fallbackLatest",QUrl::FullyDecoded)=="1") {
            QSqlQuery latest(db_->connection());
            latest.prepare("SELECT MAX(substr(sr.record_date,1,10)) FROM salary_records sr "
                           "LEFT JOIN employees e ON e.id=sr.employee_id "
                           "LEFT JOIN devices d ON d.id=sr.device_id WHERE "+whereNoDate);
            for(const QVariant &bind:bindsNoDate) latest.addBindValue(bind);
            if(!latest.exec()) return fail(500,latest.lastError().text());
            if(latest.next()) {
                const QDate lastDay=QDate::fromString(latest.value(0).toString(),"yyyy-MM-dd");
                if(lastDay.isValid()) {
                    Request fallback=req;
                    fallback.query.removeAllQueryItems("startDate");
                    fallback.query.removeAllQueryItems("endDate");
                    fallback.query.removeAllQueryItems("fallbackLatest");
                    const QString fallbackStart=lastDay.addDays(-29).toString("yyyy-MM-dd");
                    const QString fallbackEnd=lastDay.toString("yyyy-MM-dd");
                    fallback.query.addQueryItem("startDate",fallbackStart);
                    fallback.query.addQueryItem("endDate",fallbackEnd);
                    Response response=statistics(fallback,user,parts);
                    if(response.status==200 && response.body.value("code").toInt()==0) {
                        QJsonObject data=response.body.value("data").toObject();
                        data["fallbackRange"]=QJsonObject{{"startDate",fallbackStart},{"endDate",fallbackEnd}};
                        response.body["data"]=data;
                    }
                    return response;
                }
            }
        }
        QVector<QPair<QString,double>> ranked;
        for(auto it=byEmployee.cbegin();it!=byEmployee.cend();++it) ranked.append(it.value());
        std::sort(ranked.begin(),ranked.end(),[](const auto &left,const auto &right){ return left.second>right.second; });
        for(int i=0;i<qMin(10,ranked.size());++i) rank.append(QJsonObject{{"name",ranked[i].first},{"value",qRound64(ranked[i].second*100.0)/100.0}});
        for(auto it=byDate.cbegin();it!=byDate.cend();++it) trend.append(QJsonObject{{"date",it.key()},{"value",qRound64(it.value()*100.0)/100.0}});
        const int page=qMax(1,req.query.queryItemValue("page",QUrl::FullyDecoded).toInt());
        const int requestedSize=req.query.queryItemValue("pageSize",QUrl::FullyDecoded).toInt();
        const int size=qBound(1,requestedSize>0 ? requestedSize : 10,200);
        QJsonArray list;
        for(int i=(page-1)*size;i<qMin(page*size,all.size());++i) list.append(all[i]);
        totalSalary=qRound64(totalSalary*100.0)/100.0;
        const double average=byEmployee.isEmpty() ? 0 : qRound64(totalSalary/byEmployee.size()*100.0)/100.0;
        const QJsonObject summary{{"totalSalary",totalSalary},{"totalPieces",totalPieces},{"averageSalary",average}};
        return ok(QJsonObject{{"summary",summary},{"list",list},{"total",all.size()},
            {"salaryRank",rank},{"salaryTrend",trend},{"totalSalary",totalSalary},
            {"totalPieces",totalPieces},{"averageSalary",average},{"fallbackRange",QJsonObject{}}});
    }
    if(action=="salary" && parts.size()==2 && parts[1]=="detail") {
        QString where="pr.deleted_at IS NULL AND "+scope;
        QVariantList binds;
        if(!startText.isEmpty()) { where+=" AND substr(pr.record_date,1,10)>=?"; binds << start.toString("yyyy-MM-dd"); }
        if(!endText.isEmpty()) { where+=" AND substr(pr.record_date,1,10)<=?"; binds << end.toString("yyyy-MM-dd"); }
        const QString date=req.query.queryItemValue("date",QUrl::FullyDecoded).trimmed();
        if(!date.isEmpty()) { where+=" AND substr(pr.record_date,1,10)=?"; binds << date; }
        const QString employeeText=req.query.queryItemValue("employeeId",QUrl::FullyDecoded).trimmed();
        if(!employeeText.isEmpty()) {
            bool valid=false; const qint64 id=employeeText.toLongLong(&valid);
            if(!valid || id<1) return fail(400,"员工ID不合法");
            where+=" AND pr.employee_id=?"; binds << id;
        }
        const QString deviceText=req.query.queryItemValue("deviceId",QUrl::FullyDecoded).trimmed();
        if(!deviceText.isEmpty()) {
            bool valid=false; const qint64 id=deviceText.toLongLong(&valid);
            if(!valid || id<1) return fail(400,"设备ID不合法");
            where+=" AND pr.device_id=?"; binds << id;
        }
        QSqlQuery records(db_->connection());
        records.prepare(QString("SELECT pr.id,COALESCE(d.name,%1),")
                        .arg(db_->isMySql() ? "CONCAT('设备-',pr.device_id)" : "'设备-'||pr.device_id")+
                        "COALESCE(NULLIF(pr.pattern_name,''),p.name,'未命名花型'),"
                        "COALESCE(NULLIF(p.stitches,0),pr.stitches,0),pr.start_time,pr.end_time,"
                        "COALESCE(pr.pieces,0),COALESCE(NULLIF(p.unit_price,0),"
                        "NULLIF((SELECT p_live.unit_price FROM patterns p_live WHERE p_live.deleted_at IS NULL "
                        "AND TRIM(p_live.name)=TRIM(pr.pattern_name) ORDER BY p_live.updated_at DESC,p_live.id DESC LIMIT 1),0),"
                        "pr.unit_price,0),"
                        "COALESCE(NULLIF(pr.order_no,''),p.order_no,''),substr(pr.record_date,1,10),"
                        "COALESCE(pr.running_time,0) FROM production_records pr "
                        "LEFT JOIN devices d ON d.id=pr.device_id LEFT JOIN patterns p ON p.id=pr.pattern_id "
                        "WHERE "+where+" ORDER BY pr.record_date DESC,pr.id DESC");
        for(const auto &value:binds) records.addBindValue(value);
        if(!records.exec()) return fail(500,records.lastError().text());
        QJsonArray list;
        while(records.next()) {
            const QString startValue=sqlText(records.value(4)), endValue=sqlText(records.value(5));
            const QDateTime started=QDateTime::fromString(startValue,"yyyy-MM-dd HH:mm:ss");
            const QDateTime stopped=QDateTime::fromString(endValue,"yyyy-MM-dd HH:mm:ss");
            const double hours=started.isValid() && stopped.isValid() && stopped>started ?
                started.secsTo(stopped)/3600.0 : records.value(10).toDouble();
            const qint64 pieces=records.value(6).toLongLong();
            const double price=records.value(7).toDouble();
            list.append(QJsonObject{{"id",records.value(0).toLongLong()},{"deviceName",records.value(1).toString()},
                {"patternName",records.value(2).toString()},{"patternStitches",records.value(3).toLongLong()},
                {"startTime",startValue},{"endTime",endValue},{"sewCount",pieces},
                {"sewDuration",qRound64(hours*100.0)/100.0},
                {"avgSewDuration",pieces>0 ? qRound64(hours*60.0/pieces*100.0)/100.0 : 0.0},
                {"unitPrice",price},{"orderNo",records.value(8).toString()},
                {"totalAmount",qRound64(pieces*price*100.0)/100.0},{"date",records.value(9).toString()}});
        }
        return ok(list);
    }
    if(action=="alarm") {
        QString where="ar.deleted_at IS NULL AND "+scope;
        QVariantList binds;
        if(!startText.isEmpty()) { where+=" AND substr(ar.start_time,1,10)>=?"; binds << start.toString("yyyy-MM-dd"); }
        if(!endText.isEmpty()) { where+=" AND substr(ar.start_time,1,10)<=?"; binds << end.toString("yyyy-MM-dd"); }
        const QString selected=req.query.queryItemValue("deviceId",QUrl::FullyDecoded).trimmed();
        if(!selected.isEmpty()) {
            bool valid=false; const qint64 id=selected.toLongLong(&valid);
            if(!valid || id<1) return fail(400,"设备ID不合法");
            where+=" AND ar.device_id=?"; binds << id;
        }
        appendDeviceIds(req.query,"ar",&where,&binds);
        appendDeviceName(db_,req.query,"ar",&where);
        const QString alarmType=req.query.queryItemValue("alarmType",QUrl::FullyDecoded).trimmed();
        if(!alarmType.isEmpty()) {
            where+=" AND (ar.alarm_code=? OR ar.alarm_type LIKE ? OR ar.description LIKE ?)";
            binds << alarmType << "%"+alarmType+"%" << "%"+alarmType+"%";
        }
        QSqlQuery records(db_->connection());
        records.prepare("SELECT ar.id,ar.device_id,COALESCE(d.name,''),COALESCE(d.employee_code,''),"
                        "COALESCE(d.employee_name,''),ar.alarm_code,ar.alarm_type,ar.description,"
                        "ar.duration,ar.status,ar.start_time,ar.end_time FROM alarm_records ar "
                        "LEFT JOIN devices d ON d.id=ar.device_id WHERE "+where+
                        " ORDER BY ar.start_time DESC,ar.id DESC");
        for(const auto &value:binds) records.addBindValue(value);
        if(!records.exec()) return fail(500,records.lastError().text());
        QJsonArray all, pie, trend;
        const auto employeesByDay=historicalEmployees(db_,
            startText.isEmpty() ? QString() : start.toString("yyyy-MM-dd"),
            endText.isEmpty() ? QString() : end.toString("yyyy-MM-dd"));
        QSet<qint64> affected;
        QMap<QString,qint64> typeCount, dateCount;
        QMap<QString,double> dateDuration;
        qint64 totalDuration=0, resolvedCount=0;
        while(records.next()) {
            const qint64 deviceId=records.value(1).toLongLong();
            const int duration=records.value(8).toInt();
            const QString status=records.value(9).toString(), timestamp=sqlText(records.value(10));
            const QString alarmCode=records.value(5).toString(), alarmName=records.value(6).toString();
            const QString typeName=alarmMessage(records.value(7).toString(),alarmName,alarmCode);
            const auto employee=employeesByDay.value(QString::number(deviceId)+"|"+timestamp.left(10),
                QPair<QString,QString>{"-","-"});
            affected.insert(deviceId); totalDuration+=duration;
            if(status=="resolved") ++resolvedCount;
            typeCount[typeName]++;
            dateCount[timestamp.left(10)]++; dateDuration[timestamp.left(10)]+=duration;
            all.append(QJsonObject{{"id",records.value(0).toLongLong()},{"deviceName",records.value(2).toString()},
                {"employeeCode",employee.first},{"employeeName",employee.second},
                {"alarmTime",timestamp},{"alarmInfo",typeName},{"alarmType",typeName},
                {"alarmCode",alarmCode},{"description",records.value(7).toString()},
                {"duration",duration<60 ? QString::number(duration)+"s" : QString::number(duration/60)+"min"},
                {"status",status=="resolved" ? "已处理" : "处理中"},
                {"startTime",timestamp},{"endTime",records.value(11).isNull() ? "-" : sqlText(records.value(11))}});
        }
        QVector<QPair<QString,qint64>> ranked;
        for(auto it=typeCount.cbegin();it!=typeCount.cend();++it) ranked.append({it.key(),it.value()});
        std::sort(ranked.begin(),ranked.end(),[](const auto &a,const auto &b){ return a.second==b.second ? a.first<b.first : a.second>b.second; });
        for(const auto &item:ranked) pie.append(QJsonObject{{"name",item.first},{"value",item.second}});
        for(auto it=dateCount.cbegin();it!=dateCount.cend();++it) {
            const double average=it.value()>0 ? qRound64(dateDuration.value(it.key())/it.value()/60.0*100.0)/100.0 : 0;
            trend.append(QJsonObject{{"date",it.key()},{"count",it.value()},{"avgDuration",average},{"avgTime",average}});
        }
        const int page=qMax(1,req.query.queryItemValue("page",QUrl::FullyDecoded).toInt());
        const int requestedSize=req.query.queryItemValue("pageSize",QUrl::FullyDecoded).toInt();
        const int size=qBound(1,requestedSize>0 ? requestedSize : 10,200);
        QJsonArray list;
        for(int i=(page-1)*size;i<qMin(page*size,all.size());++i) list.append(all[i]);
        const double minutes=qRound64(totalDuration/60.0*100.0)/100.0;
        const double resolvedRate=all.isEmpty() ? 0 : qRound64(double(resolvedCount)/all.size()*10000.0)/100.0;
        const QJsonObject summary{{"totalAlarms",all.size()},{"totalDuration",minutes},
            {"affectedDevices",affected.size()},{"resolvedRate",resolvedRate}};
        return ok(QJsonObject{{"summary",summary},{"list",list},{"total",all.size()},
            {"alarmTypePie",pie},{"alarmTrend",trend},{"totalAlarms",all.size()},
            {"totalDuration",minutes},{"affectedDevices",affected.size()},{"resolvedRate",resolvedRate}});
    }
    if(action=="home") {
        qint64 total=0, online=0, working=0, offline=0, alarm=0;
        QSqlQuery devices(db_->connection());
        if(!devices.exec("SELECT d.status,d.alarm_code FROM devices d WHERE d.deleted_at IS NULL AND "+scope))
            return fail(500,devices.lastError().text());
        while(devices.next()) {
            ++total;
            const QString status=devices.value(0).toString();
            if(status=="working" || status=="idle") ++online;
            if(status=="working") ++working;
            if(status=="offline") ++offline;
            if(!devices.value(1).toString().isEmpty()) ++alarm;
        }
        const QDate today=QDate::currentDate(), first=today.addDays(-6);
        const auto runtimeByDay=sessionHours(db_,first,today);
        QMap<QString,qint64> daily;
        QJsonArray patternUsage, modelRatio, topProduction, deviceUsage, weeklyEfficiency, productionByDay, runningStatusByHour;
        QMap<qint64,QJsonObject> groupMap;
        for(const QJsonValue &groupValue:db_->rows("groups",{}, {},"id",10000))
            groupMap[groupValue.toObject().value("id").toInteger()]=groupValue.toObject();
        QSqlQuery production(db_->connection());
        production.prepare("SELECT substr(pr.record_date,1,10),COALESCE(SUM(pr.pieces),0) FROM production_records pr "
                           "JOIN devices d ON d.id=pr.device_id WHERE pr.deleted_at IS NULL AND d.deleted_at IS NULL AND "+scope+
                           " AND substr(pr.record_date,1,10)>=? GROUP BY substr(pr.record_date,1,10)");
        production.addBindValue(first.toString("yyyy-MM-dd"));
        if(!production.exec()) return fail(500,production.lastError().text());
        while(production.next()) daily[production.value(0).toString()]=production.value(1).toLongLong();
        QMap<QString,QMap<qint64,QPair<double,double>>> efficiencyDays;
        QSqlQuery efficiency(db_->connection());
        efficiency.prepare("SELECT substr(pr.record_date,1,10),pr.device_id,COALESCE(SUM(pr.running_time),0),"
                           "COALESCE(SUM(pr.idle_time),0) FROM production_records pr JOIN devices d ON d.id=pr.device_id "
                           "WHERE pr.deleted_at IS NULL AND d.deleted_at IS NULL AND "+scope+
                           " AND substr(pr.record_date,1,10)>=? GROUP BY 1,2");
        efficiency.addBindValue(first.toString("yyyy-MM-dd"));
        if(!efficiency.exec()) return fail(500,efficiency.lastError().text());
        while(efficiency.next()) efficiencyDays[efficiency.value(0).toString()][efficiency.value(1).toLongLong()]={efficiency.value(2).toDouble(),efficiency.value(3).toDouble()};
        for(QDate day=first;day<=today;day=day.addDays(1)) {
            const QString dayKey=day.toString("yyyy-MM-dd");
            productionByDay.append(QJsonObject{{"date",day.toString("MM-dd")},{"value",daily.value(dayKey)}});
            const QStringList weekdays={"周一","周二","周三","周四","周五","周六","周日"};
            double rateSum=0;
            const auto dayHours=efficiencyDays.value(dayKey);
            for(auto it=dayHours.cbegin();it!=dayHours.cend();++it) {
                const auto hours=it.value();
                const double sessionRuntime=runtimeByDay.value(dayKey).value(it.key());
                const double runtime=sessionRuntime>0 ? sessionRuntime : hours.first+hours.second;
                if(runtime>0) rateSum+=qBound(0.0,hours.first/runtime*100.0,100.0);
            }
            weeklyEfficiency.append(QJsonObject{{"date",weekdays.value(day.dayOfWeek()-1)},
                {"value",total>0 ? int(rateSum/double(total)) : 0}});
        }
        QSqlQuery models(db_->connection());
        if(!models.exec("SELECT COALESCE(NULLIF(d.model_name,''),'未设置型号'),COUNT(*) FROM devices d WHERE d.deleted_at IS NULL AND "+scope+
                        " GROUP BY 1 ORDER BY 2 DESC")) return fail(500,models.lastError().text());
        while(models.next()) modelRatio.append(QJsonObject{{"name",models.value(0).toString()},{"value",models.value(1).toLongLong()}});
        QSqlQuery patterns(db_->connection());
        if(!patterns.exec("SELECT COALESCE(NULLIF(TRIM(pr.pattern_name),''),NULLIF(TRIM(p.name),''),'未知花型'),COUNT(*) "
                          "FROM production_records pr JOIN devices d ON d.id=pr.device_id LEFT JOIN patterns p ON p.id=pr.pattern_id "
                          "WHERE pr.deleted_at IS NULL AND d.deleted_at IS NULL AND pr.pattern_id IS NOT NULL AND "+scope+
                          " GROUP BY pr.pattern_id,1 ORDER BY 2 DESC,1 ASC LIMIT 10")) return fail(500,patterns.lastError().text());
        while(patterns.next()) patternUsage.append(QJsonObject{{"name",patterns.value(0).toString()},{"value",patterns.value(1).toInt()}});
        QSqlQuery top(db_->connection());
        top.prepare("SELECT d.name,COALESCE(SUM(pr.pieces),0) FROM production_records pr JOIN devices d ON d.id=pr.device_id "
                    "WHERE pr.deleted_at IS NULL AND d.deleted_at IS NULL AND "+scope+
                    " AND substr(pr.record_date,1,10)>=? GROUP BY d.id,d.name ORDER BY 2 DESC LIMIT 3");
        top.addBindValue(first.toString("yyyy-MM-dd"));
        if(!top.exec()) return fail(500,top.lastError().text());
        while(top.next()) topProduction.append(QJsonObject{{"name",top.value(0).toString()},{"value",top.value(1).toLongLong()}});
        QSqlQuery usage(db_->connection());
        usage.prepare("SELECT d.id,d.name,d.group_id,g.name,COALESCE(SUM(pr.running_time),0),"
                      "COALESCE(SUM(pr.idle_time),0),d.status FROM devices d LEFT JOIN `groups` g ON g.id=d.group_id "
                      "LEFT JOIN production_records pr ON pr.device_id=d.id AND pr.deleted_at IS NULL AND substr(pr.record_date,1,10)=? "
                      "WHERE d.deleted_at IS NULL AND "+scope+" GROUP BY d.id ORDER BY d.sort_order,d.code,d.id");
        usage.addBindValue(today.toString("yyyy-MM-dd"));
        if(!usage.exec()) return fail(500,usage.lastError().text());
        while(usage.next()) {
            const double processing=usage.value(4).toDouble(), idleFallback=usage.value(5).toDouble();
            const double sessionRuntime=runtimeByDay.value(today.toString("yyyy-MM-dd")).value(usage.value(0).toLongLong());
            const double runtime=sessionRuntime>0 ? sessionRuntime : processing+idleFallback;
            const double idle=qMax(0.0,runtime-processing);
            const auto line=lineForGroup(groupMap,usage.value(2).toLongLong());
            deviceUsage.append(QJsonObject{{"deviceId",usage.value(0).toLongLong()},{"name",usage.value(1).toString()},
                {"status",usage.value(6).toString()},
                {"groupId",QJsonValue::fromVariant(usage.value(2))},{"groupName",usage.value(3).toString()},
                {"lineId",line.first},{"lineName",line.second},
                {"processingTime",processing},{"runningTime",runtime},{"idleTime",idle},
                {"efficiency",runtime>0 ? qMin(100.0,processing/runtime*100.0) : 0.0}});
        }
        QMap<int,QSet<qint64>> onlineByHour;
        QSqlQuery sessions(db_->connection());
        sessions.prepare("SELECT s.device_id,s.started_at,COALESCE(s.ended_at,s.last_seen_at) "
                         "FROM device_runtime_sessions s JOIN devices d ON d.id=s.device_id "
                         "WHERE s.deleted_at IS NULL AND d.deleted_at IS NULL AND "+scope+
                         " AND s.started_at<? AND (s.ended_at IS NULL OR s.ended_at>?)");
        const QDateTime dayStart(today,QTime(0,0)), tomorrow(dayStart.addDays(1));
        sessions.addBindValue(tomorrow.toUTC().toString("yyyy-MM-dd HH:mm:ss"));
        sessions.addBindValue(dayStart.toUTC().toString("yyyy-MM-dd HH:mm:ss"));
        if(!sessions.exec()) return fail(500,sessions.lastError().text());
        while(sessions.next()) {
            const QDateTime began=runtimeTimestamp(sqlText(sessions.value(1)));
            const QDateTime stopped=runtimeTimestamp(sqlText(sessions.value(2)));
            for(int hour=0;hour<=QTime::currentTime().hour();++hour) {
                const QDateTime hourStart=dayStart.addSecs(hour*3600), hourEnd=hourStart.addSecs(3600);
                if(began<hourEnd && stopped>hourStart) onlineByHour[hour].insert(sessions.value(0).toLongLong());
            }
        }
        for(int hour=0;hour<=QTime::currentTime().hour();++hour) {
            const qint64 count=hour==QTime::currentTime().hour() ? online : qMin(total,qint64(onlineByHour.value(hour).size()));
            runningStatusByHour.append(QJsonObject{{"hour",QString("%1:00").arg(hour,2,10,QChar('0'))},
                {"online",count},{"offline",total-count}});
        }
        return ok(QJsonObject{{"totalDevices",total},{"onlineDevices",online},{"workingDevices",working},
            {"offlineDevices",offline},{"alarmDevices",alarm},{"weeklyEfficiency",weeklyEfficiency},
            {"patternUsage",patternUsage},{"modelRatio",modelRatio},{"topProduction",topProduction},
            {"deviceUsage",deviceUsage},{"runningStatusByHour",runningStatusByHour},
            {"productionByDay",productionByDay},{"productionByHour",productionByDay}});
    }
    if(action=="dashboard") {
        QString deviceScope=scope;
        const QString selected=req.query.queryItemValue("deviceId",QUrl::FullyDecoded).trimmed();
        if(!selected.isEmpty()) {
            bool valid=false; const qint64 id=selected.toLongLong(&valid);
            if(!valid || id<1) return fail(400,"设备ID不合法");
            deviceScope+=" AND d.id="+QString::number(id);
        }
        const QString selections=req.query.queryItemValue("deviceIds",QUrl::FullyDecoded).trimmed();
        if(!selections.isEmpty()) {
            if(selections=="0") deviceScope+=" AND d.id=0";
            else {
            QStringList ids;
            for(const QString &raw:selections.split(',',Qt::SkipEmptyParts)) {
                bool valid=false; const qint64 id=raw.trimmed().toLongLong(&valid);
                if(!valid || id<1) return fail(400,"设备ID不合法");
                ids << QString::number(id);
            }
            if(!ids.isEmpty()) deviceScope+=" AND d.id IN ("+ids.join(',')+")";
            }
        }
        qint64 deviceCount=0, onlineCount=0;
        QSet<qint64> dashboardDeviceIds;
        QSqlQuery devices(db_->connection());
        if(!devices.exec("SELECT d.id,d.status FROM devices d WHERE d.deleted_at IS NULL AND "+deviceScope))
            return fail(500,devices.lastError().text());
        while(devices.next()) {
            ++deviceCount;
            dashboardDeviceIds.insert(devices.value(0).toLongLong());
            const QString state=devices.value(1).toString();
            if(state=="idle" || state=="working") ++onlineCount;
        }
        const QDate today=QDate::currentDate(), first=today.addDays(-6);
        const auto runtimeByDay=sessionHours(db_,first,today);
        const QString todayKey=today.toString("yyyy-MM-dd");
        QMap<QString,qint64> piecesByDay;
        QMap<QString,QMap<qint64,QPair<double,double>>> hoursByDay;
        qint64 totalPieces=0, todayPieces=0;
        double usedThread=0;
        QSqlQuery production(db_->connection());
        if(!production.exec("SELECT substr(pr.record_date,1,10),pr.device_id,COALESCE(SUM(pr.pieces),0),"
                            "COALESCE(SUM(pr.thread_length),0),COALESCE(SUM(pr.running_time),0),"
                            "COALESCE(SUM(pr.idle_time),0) FROM production_records pr "
                            "JOIN devices d ON d.id=pr.device_id WHERE pr.deleted_at IS NULL AND d.deleted_at IS NULL AND "+
                            deviceScope+" GROUP BY 1,2")) return fail(500,production.lastError().text());
        while(production.next()) {
            const QString date=production.value(0).toString();
            const qint64 pieces=production.value(2).toLongLong();
            totalPieces+=pieces; usedThread+=production.value(3).toDouble();
            if(date==todayKey) todayPieces+=pieces;
            if(date>=first.toString("yyyy-MM-dd")) {
                piecesByDay[date]+=pieces;
                hoursByDay[date][production.value(1).toLongLong()]={production.value(4).toDouble(),production.value(5).toDouble()};
            }
        }
        for(auto day=runtimeByDay.cbegin();day!=runtimeByDay.cend();++day)
            for(auto device=day.value().cbegin();device!=day.value().cend();++device)
                if(dashboardDeviceIds.contains(device.key()) && !hoursByDay[day.key()].contains(device.key()))
                    hoursByDay[day.key()][device.key()]={0,0};
        qint64 todayAlarms=0, totalAlarms=0;
        QSqlQuery alarms(db_->connection());
        if(!alarms.exec("SELECT substr(ar.start_time,1,10),COUNT(*) FROM alarm_records ar "
                        "JOIN devices d ON d.id=ar.device_id WHERE ar.deleted_at IS NULL AND d.deleted_at IS NULL AND "+
                        deviceScope+" GROUP BY 1")) return fail(500,alarms.lastError().text());
        while(alarms.next()) {
            const qint64 count=alarms.value(1).toLongLong();
            totalAlarms+=count; if(alarms.value(0).toString()==todayKey) todayAlarms+=count;
        }
        QJsonArray hourlyProduction, runningProcessingTrend, utilizationTrend;
        double todayProcessing=0, todayRuntime=0;
        for(QDate day=first;day<=today;day=day.addDays(1)) {
            const QString key=day.toString("yyyy-MM-dd"), label=day.toString("MM-dd");
            hourlyProduction.append(QJsonObject{{"date",label},{"value",piecesByDay.value(key)}});
            double processing=0,runtime=0,rateSum=0; int rateCount=0;
            const auto dayHours=hoursByDay.value(key);
            for(auto it=dayHours.cbegin();it!=dayHours.cend();++it) {
                const auto value=it.value();
                const double sessionRuntime=runtimeByDay.value(key).value(it.key());
                const double deviceRuntime=sessionRuntime>0 ? sessionRuntime : value.first+value.second;
                processing+=value.first; runtime+=deviceRuntime;
                if(deviceRuntime>0) { rateSum+=qBound(0.0,value.first/deviceRuntime*100.0,100.0); ++rateCount; }
            }
            if(day==today) { todayProcessing=processing; todayRuntime=runtime; }
            runningProcessingTrend.append(QJsonObject{{"date",label},{"runningTime",runtime},{"processingTime",processing}});
            utilizationTrend.append(QJsonObject{{"date",label},{"value",rateCount>0 ? rateSum/rateCount : 0.0}});
        }
        QSet<qint64> speedDevices; double speedTotal=0; int speedCount=0;
        QSqlQuery speed(db_->connection());
        if(!speed.exec("SELECT pr.device_id,pr.stitches,pr.running_time FROM production_records pr "
                       "JOIN devices d ON d.id=pr.device_id WHERE pr.deleted_at IS NULL AND d.deleted_at IS NULL AND "+
                       deviceScope+" ORDER BY pr.record_date DESC,pr.id DESC LIMIT 500")) return fail(500,speed.lastError().text());
        while(speed.next()) {
            const qint64 id=speed.value(0).toLongLong();
            const double hours=speed.value(2).toDouble();
            if(speedDevices.contains(id) || hours<=0) continue;
            speedDevices.insert(id);
            speedTotal+=speed.value(1).toDouble()/(hours*60.0); ++speedCount;
        }
        const double utilization=todayRuntime>0 ? qBound(0.0,todayProcessing/todayRuntime*100.0,100.0) : 0;
        return ok(QJsonObject{{"totalPieces",totalPieces},{"todayPieces",todayPieces},
            {"threadLength",usedThread},{"usedThreadLength",usedThread},{"totalThreadLength",usedThread*1.25},
            {"avgUsedThreadLength",deviceCount>0 ? usedThread/deviceCount : 0.0},
            {"spindleSpeed",speedCount>0 ? qRound(speedTotal/speedCount) : 0},
            {"runningTime",todayRuntime},{"processingTime",todayProcessing},{"utilizationRate",utilization},
            {"todayAlarmCount",todayAlarms},{"totalAlarmCount",totalAlarms},
            {"onlineDeviceCount",onlineCount},{"scopeDeviceCount",deviceCount},
            {"hourlyProduction",hourlyProduction},{"runningProcessingTrend",runningProcessingTrend},
            {"utilizationTrend",utilizationTrend}});
    }
    return fail(404,"接口尚未实现");
}

ApiServer::Response ApiServer::collection(const Request &req,const QJsonObject &user,const QString &section,const QStringList &parts) {
    QString table;
    if(section=="group") table="groups";
    else if(section=="role") table="roles";
    else if(section=="user") table="users";
    else if(section=="operator") table="operators";
    else if(section=="device") table="devices";
    else if(section=="employee") table="employees";
    else if(section=="pattern") table="patterns";
    else return fail(404,"接口不存在");
    if(!canUse(user,section,req.method!="GET")) return fail(403,"没有访问权限");
    const QString action=parts.isEmpty() ? QString() : parts.first();
    if(section=="device" && req.method=="GET" &&
       !QStringList{"tree","list","groups"}.contains(action) &&
       !hasPermission(user,{"deviceManagement","deviceInfo"}))
        return fail(403,"没有访问权限");
    if(section=="pattern" && !adminRole(user)) {
        const bool queueAction=action=="queue" || action=="log";
        const bool deviceFileAction=action=="device-files" || action=="upload-queue";
        const bool allowed=queueAction ? hasPermission(user,{"fileManagement","downloadLog"}) :
            deviceFileAction ? hasPermission(user,{"fileManagement","devicePatternFiles"}) :
            hasPermission(user,{"fileManagement","patternFiles"});
        if(!allowed) return fail(403,"没有访问权限");
    }
    if(section=="pattern" && action=="direct-transfer" && parts.size()==2) {
        if(!deviceServer_)return fail(503,"设备服务未启动");
        const auto job=deviceServer_->directTransfer(parts[1],user.value("id").toInteger());
        if(job.isEmpty())return fail(404,"临时传输已结束或不存在");
        if(req.method=="GET")return ok(job);
        if(req.method=="DELETE"){deviceServer_->cancelDirectTransfer(parts[1],user.value("id").toInteger());return ok();}
        return fail(405,"不支持的请求方法");
    }
    if(section=="pattern" && action=="direct-upload" && parts.size()==1 && req.method=="POST") {
        if(!deviceServer_)return fail(503,"设备服务未启动");
        if(req.rawBody.size()>17*1024*1024)return fail(400,"花型文件超过设备协议大小限制");
        const Multipart upload=parseMultipart(req.rawBody,req.headers.value("content-type"));
        if(upload.fileName.isEmpty() || upload.file.isEmpty())return fail(400,"请选择非空花型文件");
        if(upload.file.size()>255*65536)return fail(400,"花型文件超过设备协议大小限制");
        if(!QStringList{"slw","xdg","nsp","ntp","mtp","sdt","vdt","sdg","dst"}.contains(QFileInfo(upload.fileName).suffix().toLower()))return fail(400,"不支持的花型文件格式");
        bool valid=false;const qint64 deviceId=QString::fromUtf8(upload.fields.value("deviceId")).toLongLong(&valid);
        if(!valid || deviceId<=0)return fail(400,"请选择目标设备");
        const QJsonObject device=db_->row("devices",deviceId);
        if(device.isEmpty() || !inScope(device,user))return fail(403,"无权操作该设备");
        const QString name=QString::fromUtf8(upload.fields.value("name")).trimmed();
        QString error;const QString id=deviceServer_->startDirectTransfer(deviceId,user.value("id").toInteger(),name,upload.file,&error);
        if(id.isEmpty())return fail(409,error);
        return ok(deviceServer_->directTransfer(id,user.value("id").toInteger()));
    }
    if(section=="pattern" && (action=="download" || action=="batch-download") && req.method=="POST") {
        QJsonArray patternIds=action=="download" ? QJsonArray{req.body.value("patternId")} : req.body.value("patternIds").toArray();
        const QJsonArray deviceIds=req.body.value("deviceIds").toArray();
        if(patternIds.isEmpty() || deviceIds.isEmpty()) return fail(400,"请选择花型和目标设备");
        QSet<qint64> patterns, devices;
        for(const QJsonValue &value:patternIds) {
            const qint64 id=value.toInteger();
            if(id<1) return fail(400,"花型不存在");
            const QJsonObject item=db_->row("patterns",id);
            if(item.isEmpty()) return fail(400,"花型不存在");
            if(scopedPatterns(QJsonArray{item},user,db_).isEmpty()) return fail(403,"无权下发该花型");
            if(item.value("fileSize").toInteger()<1 || !QFileInfo::exists(item.value("filePath").toString()))
                return fail(400,"花型文件为空或不存在");
            const QString name=item.value("name").toString().trimmed();
            if(name.isEmpty() || name.size()*2>44) return fail(400,"花型名称超过设备协议44字节限制，请缩短名称后下发");
            patterns.insert(id);
        }
        for(const QJsonValue &value:deviceIds) {
            const qint64 id=value.toInteger();
            const QJsonObject item=db_->row("devices",id);
            if(id<1 || item.isEmpty()) return fail(400,"包含不存在的目标设备");
            if(!inScope(item,user)) return fail(403,"包含无权下发的目标设备");
            devices.insert(id);
        }
        QSqlDatabase connection=db_->connection();
        if(!connection.transaction()) return fail(500,connection.lastError().text());
        for(qint64 patternId:patterns) for(qint64 deviceId:devices) {
            QString error;
            if(!db_->insert("download_tasks",{{"patternId",patternId},{"deviceId",deviceId},{"status","waiting"},
                {"progress",0},{"message","等待下发"},{"operatorId",user.value("id")}},nullptr,&error)) {
                connection.rollback(); return fail(500,error);
            }
        }
        if(!connection.commit()) return fail(500,connection.lastError().text());
        return ok();
    }
    if(section=="pattern" && action=="batch-update" && req.method=="POST") {
        const QJsonArray ids=req.body.value("ids").toArray();
        if(ids.isEmpty()) return fail(400,"请选择至少一条花型记录");
        QString invalid;
        const QJsonObject updates=patternUpdates(req.body,&invalid);
        if(!invalid.isEmpty()) return fail(400,invalid);
        if(updates.isEmpty()) return fail(400,"请至少填写一个需要批量修改的字段");
        QSet<qint64> unique;
        for(const QJsonValue &value:ids) {
            const qint64 id=value.toInteger();
            const QJsonObject item=db_->row("patterns",id);
            if(id<1 || item.isEmpty()) return fail(400,"包含不存在的花型");
            if(scopedPatterns(QJsonArray{item},user,db_).isEmpty()) return fail(403,"包含无权修改的花型");
            unique.insert(id);
        }
        QSqlDatabase connection=db_->connection();
        if(!connection.transaction()) return fail(500,connection.lastError().text());
        for(qint64 id:unique) {
            QString error;
            if(!db_->update("patterns",id,updates,&error)) { connection.rollback(); return fail(500,error); }
        }
        if(!connection.commit()) return fail(500,connection.lastError().text());
        for(const auto &catalog:{QPair<QString,QString>{"pattern_type_catalogs",updates.value("patternType").toString()},
                                 QPair<QString,QString>{"order_no_catalogs",updates.value("orderNo").toString()}})
            if(!catalog.second.isEmpty() && db_->rows(catalog.first,"value=?",{catalog.second},"id",1).isEmpty())
                db_->insert(catalog.first,{{"value",catalog.second}},nullptr,nullptr);
        return ok(QJsonObject{{"affected",unique.size()}});
    }
    if(section=="pattern" && (action=="queue" || action=="log") && req.method=="GET") {
        QJsonArray all=scopedDeviceRecords(db_->rows("download_tasks",{}, {},"id DESC",10000),user,db_), filtered;
        const QString status=req.query.queryItemValue("status",QUrl::FullyDecoded).trimmed();
        const QString patternName=req.query.queryItemValue("patternName",QUrl::FullyDecoded).trimmed();
        const QString deviceName=req.query.queryItemValue("deviceName",QUrl::FullyDecoded).trimmed();
        const QString patternType=req.query.queryItemValue("patternType",QUrl::FullyDecoded).trimmed();
        const QString orderNo=req.query.queryItemValue("orderNo",QUrl::FullyDecoded).trimmed();
        const QString startDate=req.query.queryItemValue("startDate",QUrl::FullyDecoded).trimmed();
        const QString endDate=req.query.queryItemValue("endDate",QUrl::FullyDecoded).trimmed();
        for(const QJsonValue &value:all) {
            QJsonObject item=value.toObject();
            const QString state=item.value("status").toString();
            if(action=="queue" && status.isEmpty() && !QStringList{"waiting","downloading","paused","completed","failed"}.contains(state)) continue;
            if(action=="log" && !QStringList{"completed","failed","canceled"}.contains(state)) continue;
            if(!status.isEmpty() && state!=status) continue;
            const QString created=item.value("createdAt").toString().left(10);
            if(!startDate.isEmpty() && created<startDate) continue;
            if(!endDate.isEmpty() && created>endDate) continue;
            const QJsonObject pattern=db_->row("patterns",item.value("patternId").toInteger());
            const QJsonObject device=db_->row("devices",item.value("deviceId").toInteger());
            if(!patternName.isEmpty() && !pattern.value("name").toString().contains(patternName,Qt::CaseInsensitive)) continue;
            if(!deviceName.isEmpty() && !device.value("name").toString().contains(deviceName,Qt::CaseInsensitive)) continue;
            if(!patternType.isEmpty() && pattern.value("patternType").toString()!=patternType) continue;
            if(!orderNo.isEmpty() && !pattern.value("orderNo").toString().contains(orderNo)) continue;
            item["patternName"]=pattern.value("name"); item["patternType"]=pattern.value("patternType");
            item["stitches"]=pattern.value("stitches"); item["fileSize"]=pattern.value("fileSize");
            item["size"]=QString::number(pattern.value("fileSize").toInteger());
            item["unitPrice"]=pattern.value("unitPrice"); item["orderNo"]=pattern.value("orderNo");
            item["deviceName"]=device.value("name"); item["updateTime"]=item.value("updatedAt");
            filtered.append(item);
        }
        if(action=="log") {
            const int page=qMax(1,req.query.queryItemValue("page",QUrl::FullyDecoded).toInt());
            const int rawSize=req.query.queryItemValue("pageSize",QUrl::FullyDecoded).toInt();
            const int size=qBound(1,rawSize>0 ? rawSize : 10,200);
            QJsonArray pageItems;
            for(int i=(page-1)*size;i<qMin(page*size,filtered.size());++i) pageItems.append(filtered[i]);
            return ok(QJsonObject{{"list",pageItems},{"total",filtered.size()}});
        }
        return ok(QJsonObject{{"list",filtered},{"total",filtered.size()}});
    }
    if(section=="pattern" && action=="queue" && parts.size()>=2) {
        const QString operation=parts[1];
        if((operation=="pause-all" || operation=="resume-all" || operation=="completed") && parts.size()==2) {
            const bool pause=operation=="pause-all", resume=operation=="resume-all";
            if((pause || resume) && req.method!="POST") return fail(404,"接口不存在");
            if(!pause && !resume && req.method!="DELETE") return fail(404,"接口不存在");
            const QJsonArray tasks=scopedDeviceRecords(db_->rows("download_tasks",{}, {},"id",10000),user,db_);
            int affected=0;
            for(const QJsonValue &value:tasks) {
                const QJsonObject task=value.toObject(); const QString state=task.value("status").toString();
                if(pause && state!="waiting" && state!="downloading") continue;
                if(resume && state!="paused") continue;
                if(!pause && !resume && state!="completed") continue;
                const qint64 id=task.value("id").toInteger(); QString error;
                const bool saved=pause ? db_->update("download_tasks",id,{{"status","paused"},{"message","任务已暂停"}},&error) :
                    resume ? db_->update("download_tasks",id,{{"status","waiting"},{"progress",0},{"message","等待下发"}},&error) :
                    db_->remove("download_tasks",id,&error);
                if(!saved) return fail(500,error);
                ++affected;
            }
            return ok(QJsonObject{{"affected",affected}});
        }
        bool idValid=false; const qint64 taskId=operation.toLongLong(&idValid);
        if(!idValid || taskId<1) return fail(404,"任务不存在");
        const QJsonObject task=db_->row("download_tasks",taskId);
        if(task.isEmpty()) return fail(404,"任务不存在");
        const QJsonObject device=db_->row("devices",task.value("deviceId").toInteger());
        if(!inScope(device,user)) return fail(403,"无权操作该任务");
        if(parts.size()==3 && req.method=="POST") {
            const QString state=task.value("status").toString();
            if(parts[2]=="pause" && (state=="waiting" || state=="downloading")) {
                QString error;
                return db_->update("download_tasks",taskId,{{"status","paused"},{"message","任务已暂停"}},&error) ? ok() : fail(500,error);
            }
            if(parts[2]=="resume" && (state=="paused" || state=="failed")) {
                QString error;
                return db_->update("download_tasks",taskId,{{"status","waiting"},{"progress",0},{"message","等待下发"}},&error) ? ok() : fail(500,error);
            }
            return fail(400,"当前状态不支持此操作");
        }
        if(parts.size()==2 && req.method=="DELETE") {
            QString error;
            return db_->remove("download_tasks",taskId,&error) ? ok() : fail(500,error);
        }
        return fail(404,"接口不存在");
    }
    if(section=="pattern" && action=="device-files") {
        if(parts.size()==2 && parts[1]=="upload" && req.method=="POST") {
            const qint64 deviceId=req.body.value("deviceId").toInteger();
            const QJsonArray fileIds=req.body.value("fileIds").toArray();
            if(fileIds.isEmpty()) return fail(400,"请至少选择一个设备文件");
            const QJsonObject device=db_->row("devices",deviceId);
            if(device.isEmpty()) return fail(404,"设备不存在");
            if(!inScope(device,user)) return fail(403,"无权回传该设备文件");
            if(!deviceServer_ || !deviceServer_->deviceConnected(deviceId))
                return fail(400,"设备未开机连接，无法回传文件");
            QString mode=req.body.value("conflictMode").toString().trimmed().toLower();
            if(mode!="overwrite" && mode!="rename") mode="ask";
            QJsonArray files, conflicts;
            QSet<qint64> seen;
            for(const QJsonValue &value:fileIds) {
                const qint64 id=value.toInteger();
                if(id<1 || seen.contains(id)) continue;
                seen.insert(id);
                QJsonObject file=db_->row("device_pattern_files",id);
                if(file.isEmpty() || file.value("deviceId").toInteger()!=deviceId) {
                    for(const QJsonValue &snapshotValue:req.body.value("selectedFiles").toArray()) {
                        const QJsonObject snapshot=snapshotValue.toObject();
                        if(snapshot.value("id").toInteger()!=id) continue;
                        const qint64 patternNo=snapshot.value("patternNo").toInteger();
                        const QString fileName=snapshot.value("fileName").toString().trimmed();
                        const QJsonArray candidates=patternNo>0 ?
                            db_->rows("device_pattern_files","device_id=? AND pattern_no=?",
                                      {deviceId,patternNo},"id DESC",1) :
                            db_->rows("device_pattern_files","device_id=? AND file_name=?",
                                      {deviceId,fileName},"id DESC",1);
                        if(!candidates.isEmpty()) {
                            file=candidates.first().toObject();
                            if(!fileName.isEmpty() && file.value("fileName").toString()!=fileName) file=QJsonObject{};
                        }
                        break;
                    }
                }
                if(file.isEmpty() || file.value("deviceId").toInteger()!=deviceId)
                    return fail(400,"包含不存在的设备文件");
                file["selectedFileId"]=id;
                const QString selectedKey=QString::number(id);
                const QJsonObject requestedNames=req.body.value("renameNames").toObject();
                if(mode=="rename" && requestedNames.contains(selectedKey)) {
                    const QString target=requestedNames.value(selectedKey).toString().trimmed();
                    if(target.isEmpty() || target.size()>255) return fail(400,"花型名称不能为空且不能超过255个字符");
                }
                files.append(file);
                QString name=QFileInfo(file.value("fileName").toString()).completeBaseName();
                if(name.isEmpty()) name=file.value("fileName").toString();
                if(name.isEmpty()) name=QString("device_%1_pattern_%2").arg(deviceId).arg(file.value("patternNo").toInt());
                const QJsonArray existing=db_->rows("patterns","name=?",{name},"id",1);
                if(!existing.isEmpty()) conflicts.append(QJsonObject{{"fileId",id},{"fileName",file.value("fileName")},
                    {"patternName",name},{"patternNo",file.value("patternNo")},{"fileSize",file.value("fileSize")},
                    {"existingPatternId",existing.first().toObject().value("id")}});
            }
            if(files.isEmpty()) return fail(400,"未找到可回传的设备文件");
            QSet<QString> reservedNames;QSqlQuery patternNames(db_->connection());if(!patternNames.exec("SELECT name FROM patterns WHERE deleted_at IS NULL"))return fail(500,"读取花型目录失败");while(patternNames.next())reservedNames.insert(patternNames.value(0).toString().toCaseFolded());
            if(mode=="ask" && !conflicts.isEmpty()) {
                for(const auto &value:files)reservedNames.insert(QFileInfo(value.toObject().value("fileName").toString()).completeBaseName().toCaseFolded());
                QJsonArray suggested;for(const auto &value:conflicts){auto entry=value.toObject();const QString base=entry.value("patternName").toString();int suffix=1;QString name;do{name=base+QString("(%1)").arg(suffix++);}while(reservedNames.contains(name.toCaseFolded()));reservedNames.insert(name.toCaseFolded());entry["suggestedName"]=name;suggested.append(entry);}
                return {409,{{"code",409},{"message","检测到同名花型，请选择覆盖或重命名"},{"data",QJsonObject{{"duplicates",suggested}}}}};
            }
            QJsonObject targetNames=req.body.value("renameNames").toObject();
            if(mode=="rename"){QSet<QString> targets;for(const auto &value:files){const auto file=value.toObject();const QString key=QString::number(file.value("selectedFileId").toInteger(file.value("id").toInteger()));const bool supplied=targetNames.contains(key);const QString base=QFileInfo(file.value("fileName").toString()).completeBaseName();QString name=targetNames.value(key).toString(base);if(supplied && (reservedNames.contains(name.toCaseFolded()) || targets.contains(name.toCaseFolded())))return fail(400,"新花型名称已存在，请修改后重试");if(!supplied){int suffix=1;while(reservedNames.contains(name.toCaseFolded()) || targets.contains(name.toCaseFolded()))name=base+QString("(%1)").arg(suffix++);targetNames[key]=name;}targets.insert(name.toCaseFolded());}}

            QSqlDatabase connection=db_->connection();
            if(!connection.transaction()) return fail(500,connection.lastError().text());
            for(const QJsonValue &value:files) {
                const QJsonObject file=value.toObject();
                const QString fileId=QString::number(file.value("selectedFileId").toInteger(file.value("id").toInteger()));
                QString error;
                if(!db_->insert("upload_tasks",{{"deviceFileId",file.value("id")},{"deviceId",deviceId},
                    {"status","waiting"},{"progress",0},{"message","等待回传"},{"operatorId",user.value("id")},
                    {"conflictMode",mode},{"targetName",targetNames.value(fileId)},
                    {"fileSnapshot",QString::fromUtf8(QJsonDocument(file).toJson(QJsonDocument::Compact))}},nullptr,&error)) {
                    connection.rollback(); return fail(500,error);
                }
            }
            if(!connection.commit()) return fail(500,connection.lastError().text());
            return ok(QJsonObject{{"total",files.size()},{"queued",files.size()}},"已加入上传队列");
        }
        if(parts.size()==2 && parts[1]=="refresh" && req.method=="POST") {
            const qint64 deviceId=req.query.queryItemValue("deviceId",QUrl::FullyDecoded).isEmpty() ?
                req.body.value("deviceId").toInteger() : req.query.queryItemValue("deviceId",QUrl::FullyDecoded).toLongLong();
            const QJsonObject device=db_->row("devices",deviceId);
            if(deviceId<1 || device.isEmpty()) return fail(404,"设备不存在");
            if(!inScope(device,user)) return fail(403,"无权读取该设备");
            if(!deviceServer_) return fail(503,"设备传输服务未初始化");
            QString error;
            if(!deviceServer_->refreshPatternList(deviceId,&error))
                return fail(error=="设备未开机连接" ? 400 : error=="设备当前忙碌" ? 409 : 500,"实时读取设备文件失败: "+error);
            return ok(QJsonValue(),"设备花型列表已刷新");
        }
        if(parts.size()==1 && req.method=="GET") {
            bool valid=false; const qint64 deviceId=req.query.queryItemValue("deviceId",QUrl::FullyDecoded).toLongLong(&valid);
            if(!valid || deviceId<1) return fail(400,"请选择设备");
            const QJsonObject device=db_->row("devices",deviceId);
            if(device.isEmpty()) return fail(404,"设备不存在");
            if(!inScope(device,user)) return fail(403,"无权读取该设备");
            if(!deviceServer_ || !deviceServer_->deviceConnected(deviceId))
                return ok(QJsonObject{{"list",QJsonArray{}},{"total",0},{"offline",true}},"设备未连接");
            QJsonArray all, filtered;
            const QString keyword=req.query.queryItemValue("keyword",QUrl::FullyDecoded).trimmed();
            const QString type=req.query.queryItemValue("patternType",QUrl::FullyDecoded).trimmed();
            for(const QJsonValue &value:db_->rows("device_pattern_files","device_id=?",{deviceId},"id DESC",10000)) {
                QJsonObject item=value.toObject();
                QString patternName=QFileInfo(item.value("fileName").toString()).completeBaseName();
                if(patternName.isEmpty()) patternName=item.value("fileName").toString();
                const QJsonArray matching=db_->rows("patterns","lower(name)=lower(?)",{patternName},"id DESC",1);
                if(!matching.isEmpty()) {
                    const QJsonObject pattern=matching.first().toObject();
                    item["serverPatternId"]=pattern.value("id");
                    for(const QString &key:{"patternType","stitches","unitPrice","orderNo","fileSize"})
                        item[key]=pattern.value(key);
                }
                item["patternName"]=patternName;
                if(!keyword.isEmpty() && !patternName.contains(keyword,Qt::CaseInsensitive) &&
                   !item.value("fileName").toString().contains(keyword,Qt::CaseInsensitive)) continue;
                if(!type.isEmpty() && item.value("patternType").toString()!=type) continue;
                filtered.append(item);
            }
            const int page=qMax(1,req.query.queryItemValue("page",QUrl::FullyDecoded).toInt());
            const int rawSize=req.query.queryItemValue("pageSize",QUrl::FullyDecoded).toInt();
            const int size=qBound(1,rawSize>0 ? rawSize : 10,200);
            for(int i=(page-1)*size;i<qMin(page*size,filtered.size());++i) all.append(filtered[i]);
            return ok(QJsonObject{{"list",all},{"total",filtered.size()}});
        }
        if(parts.size()==2 && req.method=="DELETE") {
            bool valid=false; const qint64 fileId=parts[1].toLongLong(&valid);
            if(!valid || fileId<1) return fail(404,"设备文件不存在");
            QJsonObject file=db_->row("device_pattern_files",fileId);
            if(file.isEmpty()) {
                const QJsonObject snapshot=req.body.value("selectedFile").toObject();
                const qint64 selectedDevice=snapshot.value("deviceId").toInteger();
                const qint64 patternNo=snapshot.value("patternNo").toInteger();
                const QString name=snapshot.value("fileName").toString();
                if(snapshot.value("id").toInteger()==fileId && selectedDevice>0 && !name.isEmpty()) {
                    const QJsonArray candidates=patternNo>0 ?
                        db_->rows("device_pattern_files","device_id=? AND pattern_no=? AND file_name=?",{selectedDevice,patternNo,name},"id DESC",1) :
                        db_->rows("device_pattern_files","device_id=? AND file_name=?",{selectedDevice,name},"id DESC",2);
                    if(candidates.size()==1) file=candidates.first().toObject();
                }
            }
            if(file.isEmpty()) return fail(404,"设备文件不存在");
            const qint64 deviceId=file.value("deviceId").toInteger();
            const QJsonObject device=db_->row("devices",deviceId);
            if(device.isEmpty()) return fail(404,"设备不存在");
            if(!inScope(device,user)) return fail(403,"无权删除该设备文件");
            if(!deviceServer_ || !deviceServer_->deviceConnected(deviceId)) return fail(400,"设备未开机连接，无法删除文件");
            {
                QString error;
                if(!deviceServer_->deletePatternFile(deviceId,file.value("patternNo").toInt(),file.value("fileName").toString(),&error))
                    return fail(500,"删除设备文件失败: "+error);
            }
            QString error;
            return db_->remove("device_pattern_files",file.value("id").toInteger(),&error) ? ok() : fail(500,error);
        }
    }
    if(section=="pattern" && action=="upload-queue") {
        if(parts.size()==1 && req.method=="GET") {
            QJsonArray all=scopedDeviceRecords(db_->rows("upload_tasks",{}, {},"id DESC",10000),user,db_), filtered;
            const QString status=req.query.queryItemValue("status",QUrl::FullyDecoded), deviceFilter=req.query.queryItemValue("deviceId",QUrl::FullyDecoded);
            for(const QJsonValue &value:all) {
                QJsonObject item=value.toObject();
                if(status.isEmpty() && !QStringList{"waiting","uploading","paused","completed","failed"}.contains(item.value("status").toString())) continue;
                if(!status.isEmpty() && item.value("status").toString()!=status) continue;
                if(!deviceFilter.isEmpty() && item.value("deviceId").toVariant().toString()!=deviceFilter) continue;
                const QJsonObject file=db_->row("device_pattern_files",item.value("deviceFileId").toInteger());
                const QJsonObject snapshot=QJsonDocument::fromJson(item.value("fileSnapshot").toString().toUtf8()).object();
                const QJsonObject details=file.isEmpty() ? snapshot : file;
                const QJsonObject device=db_->row("devices",item.value("deviceId").toInteger());
                const QJsonObject pattern=db_->row("patterns",item.value("patternId").toInteger());
                for(const QString &key:{"fileName","patternType","fileSize","stitches","unitPrice","orderNo"})
                    item[key]=details.value(key);
                item["deviceName"]=device.value("name");QString target=pattern.value("name").toString();if(target.isEmpty() && item.value("conflictMode").toString()=="rename")target=item.value("targetName").toString();if(target.isEmpty())target=QFileInfo(details.value("fileName").toString()).completeBaseName();item["patternName"]=target;
                item["size"]=QString::number(details.value("fileSize").toInteger());
                item["updateTime"]=item.value("updatedAt");
                filtered.append(item);
            }
            const int page=qMax(1,req.query.queryItemValue("page",QUrl::FullyDecoded).toInt());
            const int rawSize=req.query.queryItemValue("pageSize",QUrl::FullyDecoded).toInt();
            const int size=qBound(1,rawSize>0 ? rawSize : 10,200);
            QJsonArray list;
            for(int i=(page-1)*size;i<qMin(page*size,filtered.size());++i) list.append(filtered[i]);
            return ok(QJsonObject{{"list",list},{"total",filtered.size()},{"summary",uploadTaskSummary(filtered)}});
        }
        if(parts.size()==2 && parts[1]=="completed" && req.method=="DELETE") {
            int affected=0;
            for(const QJsonValue &value:scopedDeviceRecords(db_->rows("upload_tasks",{}, {},"id",10000),user,db_)) {
                const QJsonObject task=value.toObject();
                if(!QStringList{"completed","failed","canceled"}.contains(task.value("status").toString())) continue;
                QString error;
                if(!db_->remove("upload_tasks",task.value("id").toInteger(),&error)) return fail(500,error);
                ++affected;
            }
            return ok(QJsonObject{{"affected",affected}});
        }
        if(parts.size()>=2) {
            bool valid=false; const qint64 id=parts[1].toLongLong(&valid);
            const QJsonObject task=valid ? db_->row("upload_tasks",id) : QJsonObject{};
            if(task.isEmpty()) return fail(404,"上传任务不存在");
            if(!inScope(db_->row("devices",task.value("deviceId").toInteger()),user)) return fail(403,"无权操作该任务");
            const QString state=task.value("status").toString(); QString error;
            if(parts.size()==3 && req.method=="POST" && parts[2]=="pause" && state=="waiting")
                return db_->update("upload_tasks",id,{{"status","paused"},{"message","任务已暂停"}},&error) ? ok() : fail(500,error);
            if(parts.size()==3 && req.method=="POST" && parts[2]=="resume" && state=="paused")
                return db_->update("upload_tasks",id,{{"status","waiting"},{"progress",0},{"message","等待回传"}},&error) ? ok() : fail(500,error);
            if(parts.size()==2 && req.method=="DELETE" && (state=="waiting" || state=="paused"))
                return db_->update("upload_tasks",id,{{"status","canceled"},{"message","任务已取消"}},&error) ? ok() : fail(500,error);
            return fail(400,"当前状态不支持此操作");
        }
    }
    if((section=="employee" || section=="operator") && action=="import" && req.method=="POST") {
        const bool employeeImport=section=="employee";
        const QJsonArray items=req.body.value(employeeImport ? "employees" : "operators").toArray();
        if(items.isEmpty()) return fail(400,"导入列表不能为空");
        qint64 groupId=req.body.value("groupId").toInteger();
        if(employeeImport) {
            if(groupId<1) return fail(400,"请选择所属分组");
            if(db_->row("groups",groupId).isEmpty()) return fail(400,"所属分组不存在");
        }
        int successCount=0,updatedCount=0,restoredCount=0; QJsonArray errors;
        QSqlDatabase connection=db_->connection();
        if(!connection.transaction()) return fail(500,connection.lastError().text());
        for(const auto &value:items) {
            const QJsonObject item=value.toObject();
            if(employeeImport) {
                const QString code=item.value("code").toString().trimmed(), name=item.value("name").toString().trimmed();
                QString phone=item.value("phone").toString(); phone.remove(QRegularExpression("\\s+"));
                if(!QRegularExpression("^[0-9]{1,11}$").match(code).hasMatch() || name.isEmpty() || name.size()>8) {
                    errors.append("工号仅支持1-11位数字，姓名不能为空且不能超过8个字符"); continue;
                }
                if(!QRegularExpression("^1[3-9][0-9]{9}$").match(phone).hasMatch()) {
                    errors.append(code+" 手机号不能为空且格式需正确"); continue;
                }
                QSqlQuery existing(db_->connection());
                existing.prepare("SELECT id,deleted_at FROM employees WHERE code=?"); existing.addBindValue(code);
                if(!existing.exec()) { errors.append(code+" 导入失败: "+existing.lastError().text()); continue; }
                if(existing.next()) {
                    const bool restoring=!existing.value(1).isNull();
                    if(!restoring && !req.body.value("overwriteExisting").toBool()) { errors.append(code+" 工号已存在"); continue; }
                    if(!db_->employeePhoneAvailable(phone,existing.value(0).toLongLong())) {errors.append(code+" 手机号已被其他员工使用");continue;}
                    QString statement="UPDATE employees SET name=?,phone=?,group_id=?,deleted_at=NULL,updated_at=CURRENT_TIMESTAMP";
                    QVariantList values{name,phone,groupId};
                    for(const QString &key:{"department","position","remark"}) if(item.contains(key)) {
                        statement+=",`"+key+"`=?"; values.append(item.value(key).toString());
                    }
                    statement+=" WHERE id=?"; values.append(existing.value(0));
                    QSqlQuery restore(db_->connection());
                    restore.prepare(statement); for(const QVariant &value:values) restore.addBindValue(value);
                    if(!restore.exec()) { errors.append(code+" 导入失败: "+restore.lastError().text()); continue; }
                    ++successCount; if(restoring) ++restoredCount; else ++updatedCount;
                    continue;
                }
                QString error;
                QJsonObject employee{{"code",code},{"name",name},{"phone",phone},{"groupId",groupId}};
                for(const QString &key:{"department","position","remark"}) if(item.contains(key)) employee[key]=item.value(key);
                if(!db_->insert("employees",employee,nullptr,&error)) {
                    errors.append(code+" 导入失败: "+error); continue;
                }
            } else {
                const QString username=item.value("username").toString().trimmed();
                const QString password=item.value("password").toString().trimmed();
                QString nickname=item.value("nickname").toString().trimmed();
                if(nickname.isEmpty()) nickname=username;
                if(!QRegularExpression("^[A-Za-z0-9_]{1,11}$").match(username).hasMatch()) {
                    errors.append(username+" 账号格式错误"); continue;
                }
                if(password.size()<6 || password.size()>32) { errors.append(username+" 密码长度需在6-32位"); continue; }
                const qint64 itemGroupId=item.value("groupId").toInteger();
                if(itemGroupId>0 && db_->row("groups",itemGroupId).isEmpty()) { errors.append(username+" 分组参数错误"); continue; }
                if(db_->count("operators","username=?",{username})>0) { errors.append(username+" 账号已存在"); continue; }
                QString error;
                if(!db_->insert("operators",{{"username",username},{"password",Database::hashPassword(password)},
                    {"nickname",nickname},{"groupId",item.value("groupId")}},nullptr,&error)) {
                    errors.append(username+" 创建失败: "+error); continue;
                }
            }
            ++successCount;
        }
        if(!connection.commit()) return fail(500,connection.lastError().text());
        return ok(QJsonObject{{"successCount",successCount},{"updatedCount",updatedCount},{"restoredCount",restoredCount},{"errors",errors}});
    }
    if((section=="employee" || section=="operator") && action=="export" && req.method=="GET") {
        QJsonArray rows=scoped(db_->rows(table,{}, {},"id DESC",10000),user), result;
        const QString group=req.query.queryItemValue("groupId",QUrl::FullyDecoded), keyword=req.query.queryItemValue("keyword",QUrl::FullyDecoded).trimmed();
        const QString start=req.query.queryItemValue("startDate",QUrl::FullyDecoded), end=req.query.queryItemValue("endDate",QUrl::FullyDecoded);
        for(const auto &value:rows) {
            const QJsonObject item=value.toObject();
            if(!group.isEmpty() && item.value("groupId").toVariant().toString()!=group) continue;
            const QString created=item.value("createdAt").toString().left(10);
            if(!start.isEmpty() && created<start) continue;
            if(!end.isEmpty() && created>end) continue;
            if(!keyword.isEmpty() && !item.value(section=="employee" ? "name" : "username").toString().contains(keyword,Qt::CaseInsensitive) &&
               !item.value(section=="employee" ? "remark" : "nickname").toString().contains(keyword,Qt::CaseInsensitive)) continue;
            if(section=="employee") {
                const QString code=req.query.queryItemValue("code",QUrl::FullyDecoded), phone=req.query.queryItemValue("phone",QUrl::FullyDecoded), department=req.query.queryItemValue("department",QUrl::FullyDecoded);
                if(!code.isEmpty() && !item.value("code").toString().contains(code)) continue;
                if(!phone.isEmpty() && !item.value("phone").toString().contains(phone)) continue;
                if(!department.isEmpty() && item.value("department").toString()!=department) continue;
            }
            QJsonObject output=item;
            const QJsonObject groupRow=db_->row("groups",item.value("groupId").toInteger());
            if(!groupRow.isEmpty()) output["groupName"]=groupRow.value("name");
            result.append(output);
        }
        return ok(result);
    }
    if(section=="pattern" && action=="upload" && req.method=="POST") {
        const DiskMultipart disk=req.uploadTempPath.isEmpty() ? DiskMultipart{} :
            parseMultipartFile(req.uploadTempPath,req.headers.value("content-type"));
        const Multipart memory=req.uploadTempPath.isEmpty() ?
            parseMultipart(req.rawBody,req.headers.value("content-type")) : Multipart{};
        const QString originalName=req.uploadTempPath.isEmpty() ? memory.fileName : disk.fileName;
        const QHash<QString,QByteArray> fields=req.uploadTempPath.isEmpty() ? memory.fields : disk.fields;
        const qint64 fileLength=req.uploadTempPath.isEmpty() ? memory.file.size() : disk.fileLength;
        if(originalName.isEmpty()) return fail(400,"请选择文件");
        const QString suffix=QFileInfo(originalName).suffix().toLower();
        if(!QStringList{"slw","xdg","nsp","ntp","mtp","sdt","vdt","sdg","dst"}.contains(suffix))
            return fail(400,"不支持的花型文件格式");
        if(fileLength<1) return fail(400,"花型文件不能为空");
        const QString requestedName=QString::fromUtf8(fields.value("name")).trimmed();
        if(fields.contains("name") && requestedName.isEmpty()) return fail(400,"花型名称不能为空");
        const QString name=requestedName.isEmpty() ? originalName : requestedName;
        const QString patternType=QString::fromUtf8(fields.value("patternType")).trimmed();
        const QString orderNo=QString::fromUtf8(fields.value("orderNo"));
        if(!orderNo.isEmpty() && !QRegularExpression("^[A-Za-z0-9]+$").match(orderNo).hasMatch())return fail(400,"订单编号只允许字母和数字");
        if(name.size()>255 || patternType.size()>100 || orderNo.size()>100) return fail(400,"花型名称、类型或订单编号过长");
        bool stitchesValid=true, priceValid=true;
        const QString stitchesText=QString::fromUtf8(fields.value("stitches")).trimmed();
        const QString priceText=QString::fromUtf8(fields.value("unitPrice")).trimmed();
        const int stitches=stitchesText.isEmpty() ? 0 : stitchesText.toInt(&stitchesValid);
        const double price=priceText.isEmpty() ? 0 : priceText.toDouble(&priceValid);
        if(!stitchesValid || stitches<0) return fail(400,"针数必须是非负整数");
        if(!priceValid || !qIsFinite(price) || price<0 || price>999999999.999) return fail(400,"工价必须是非负数字");
        const QString directory=QDir(db_->dataDirectory()).filePath("uploads/patterns");
        if(!QDir().mkpath(directory)) return fail(500,"创建上传目录失败");
        const QString fileName=QDateTime::currentDateTime().toString("yyyyMMddHHmmsszzz")+"_"+
            QString::number(QRandomGenerator::global()->generate())+"_"+originalName;
        const QString path=QDir(directory).filePath(fileName);
        QFile file(path);
        if(!file.open(QIODevice::WriteOnly)) return fail(500,"文件保存失败");
        if(req.uploadTempPath.isEmpty()) {
            if(file.write(memory.file)!=fileLength) { file.close(); file.remove(); return fail(500,"文件保存失败"); }
        } else {
            QFile input(req.uploadTempPath);
            if(!input.open(QIODevice::ReadOnly) || !input.seek(disk.fileOffset)) {
                file.close(); file.remove(); return fail(500,"读取上传文件失败");
            }
            qint64 remaining=fileLength;
            while(remaining>0) {
                const QByteArray chunk=input.read(qMin<qint64>(65536,remaining));
                if(chunk.isEmpty() || file.write(chunk)!=chunk.size()) {
                    file.close(); file.remove(); return fail(500,"文件保存失败");
                }
                remaining-=chunk.size();
            }
        }
        file.close();
        qint64 id=0; QString error;
        if(!db_->insert("patterns",{{"name",name},{"patternType",patternType},{"fileName",fileName},
            {"filePath",path},{"fileSize",fileLength},{"stitches",stitches},{"unitPrice",price},
            {"orderNo",orderNo},{"uploadedBy",user.value("id")},
            {"uploadedAt",QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")}},&id,&error)) {
            file.remove(); return fail(500,error);
        }
        for(const auto &item:{QPair<QString,QString>{"pattern_type_catalogs",patternType},
                              QPair<QString,QString>{"order_no_catalogs",orderNo}}) {
            if(!item.second.isEmpty() && db_->rows(item.first,"value=?",{item.second},"id",1).isEmpty())
                db_->insert(item.first,{{"value",item.second}},nullptr,nullptr);
        }
        QJsonObject result=db_->row("patterns",id);
        result["size"]=QString::number(fileLength);
        result["uploadTime"]=result.value("uploadedAt");
        return ok(result);
    }
    if(section=="pattern" && parts.size()==2 && parts[0]=="files" && parts[1]=="download" && req.method=="GET") {
        const QJsonArray patterns=scopedPatterns(db_->rows("patterns",{}, {},"id DESC",10000),user,db_);
        QVector<QPair<QString,QString>> files;
        QSet<QString> names;
        const QString keyword=req.query.queryItemValue("keyword",QUrl::FullyDecoded).trimmed();
        const QString type=req.query.queryItemValue("patternType",QUrl::FullyDecoded).trimmed();
        const QString order=req.query.queryItemValue("orderNo",QUrl::FullyDecoded).trimmed();
        const QString start=req.query.queryItemValue("startDate",QUrl::FullyDecoded).trimmed(), end=req.query.queryItemValue("endDate",QUrl::FullyDecoded).trimmed();
        for(const QJsonValue &value:patterns) {
            const QJsonObject item=value.toObject();
            if(!keyword.isEmpty() && !item.value("name").toString().contains(keyword,Qt::CaseInsensitive) &&
                !item.value("fileName").toString().contains(keyword,Qt::CaseInsensitive)) continue;
            if(!type.isEmpty() && item.value("patternType").toString()!=type) continue;
            if(!order.isEmpty() && !item.value("orderNo").toString().contains(order)) continue;
            const QString uploaded=item.value("uploadedAt").toString().isEmpty() ?
                item.value("createdAt").toString() : item.value("uploadedAt").toString();
            if(!start.isEmpty() && uploaded.left(10)<start) continue;
            if(!end.isEmpty() && uploaded.left(10)>end) continue;
            QFile file(item.value("filePath").toString());
            if(!file.open(QIODevice::ReadOnly)) continue;
            QString logical=item.value("name").toString().trimmed();if(logical.isEmpty())logical=QFileInfo(item.value("fileName").toString()).completeBaseName();const QString extension=QFileInfo(item.value("fileName").toString()).suffix();QString fileName=portablePatternFilename(logical,extension);
            for(int duplicate=1;names.contains(fileName.toCaseFolded());++duplicate)fileName=portablePatternFilename(logical,extension,QString("(%1)").arg(duplicate));names.insert(fileName.toCaseFolded());
            files.append({fileName,file.fileName()});
        }
        if(files.isEmpty()) return fail(404,"没有可下载的花型文件");
        QTemporaryFile archive(QDir(db_->dataDirectory()).filePath("pattern-export-XXXXXX.zip"));
        archive.setAutoRemove(false);
        if(!archive.open()) return fail(500,"创建导出文件失败");
        const QString archivePath=archive.fileName(); archive.close();
        if(!makeStoredZipFile(files,archivePath)) {
            QFile::remove(archivePath); return fail(500,"生成花型压缩包失败");
        }
        Response response; response.filePath=archivePath; response.removeFile=true;
        response.contentType="application/zip";
        const QString archiveName=req.headers.value("accept-language").startsWith("en",Qt::CaseInsensitive) ?
            "server-pattern-files.zip" : "服务器花型文件.zip";
        response.disposition="attachment; filename*=UTF-8''"+QUrl::toPercentEncoding(archiveName);
        return response;
    }
    if(section=="pattern" && parts.size()==2 && parts[1]=="file" && req.method=="GET") {
        bool valid=false; const qint64 patternId=parts[0].toLongLong(&valid);
        if(!valid) return fail(404,"花型文件不存在");
        const QJsonObject pattern=db_->row("patterns",patternId);
        if(pattern.isEmpty()) return fail(404,"花型文件不存在");
        if(scopedPatterns(QJsonArray{pattern},user,db_).isEmpty()) return fail(403,"花型超出授权范围");
        QFile file(pattern.value("filePath").toString());
        if(!file.open(QIODevice::ReadOnly)) return fail(404,"花型文件不存在");
        const QString fileName=portablePatternFilename(pattern.value("name").toString().trimmed(),QFileInfo(pattern.value("fileName").toString()).suffix());
        Response response; response.filePath=file.fileName(); response.contentType="application/octet-stream";
        response.disposition="attachment; filename*=UTF-8''"+QUrl::toPercentEncoding(fileName);
        return response;
    }
    if(section=="group" && action=="sort" && req.method=="POST") {
        if(req.array.isEmpty()) return fail(400,"排序列表不能为空");
        for(const auto &value:req.array) {
            const QJsonObject item=value.toObject();
            if(item.value("id").toInteger()<2 || db_->row("groups",item.value("id").toInteger()).isEmpty()) return fail(400,"分组不存在");
        }
        for(const auto &value:req.array) {
            const QJsonObject item=value.toObject();
            db_->update("groups",item.value("id").toInteger(),{{"sortOrder",item.value("sortOrder")}},nullptr);
        }
        return ok();
    }
    if((section=="device" || section=="user" || section=="operator") && action=="move" && req.method=="POST") {
        const QString key=section=="device" ? "deviceIds" : section=="user" ? "userIds" : "operatorIds";
        const QJsonArray ids=req.body.value(key).toArray();
        if(ids.isEmpty()) return fail(400,"请选择记录");
        const QJsonValue group=req.body.value("groupId");
        if(!group.isNull() && !group.isUndefined() && db_->row("groups",group.toInteger()).isEmpty()) return fail(400,"目标分组不存在");
        QSet<qint64> selected;
        for(const auto &value:ids) {
            const qint64 id=value.toInteger(); const QJsonObject record=db_->row(table,id);
            if(id<1 || record.isEmpty()) return fail(404,"记录不存在");
            if(!inScope(record,user)) return fail(403,"记录超出授权范围");
            if(selected.contains(id)) continue;
            selected.insert(id);
        }
        if(section=="device") {
            QString error;
            return db_->moveDevices(selected.values(),group,&error) ? ok() : fail(400,error);
        }
        QSqlDatabase connection=db_->connection(); QString error;
        if(!connection.transaction()) return fail(500,connection.lastError().text());
        for(qint64 id:selected) if(!db_->update(table,id,{{"groupId",group}},&error)) { connection.rollback(); return fail(500,error); }
        if(!connection.commit()) { connection.rollback(); return fail(500,connection.lastError().text()); }
        return ok();
    }
    if(section=="device" && action=="group" && parts.size()>1) {
        const qint64 id=parts[1].toLongLong();
        if(req.method=="PUT") { QString error; return db_->update("groups",id,req.body,&error) ? ok(db_->row("groups",id)) : fail(400,error); }
        if(req.method=="DELETE") { QString error; return db_->removeGroup(id,&error) ? ok() : fail(400,error); }
    }
    if(section=="device" && action=="group" && req.method=="POST") {
        QJsonObject body=req.body; if(!body.contains("parentId")) body["parentId"]=1;
        qint64 id=0; QString error;
        return db_->insert("groups",body,&id,&error) ? ok(db_->row("groups",id)) : fail(400,error);
    }
    QString catalogTable, catalogField;
    if(section=="device" && (action=="types" || action=="type-summary")) { catalogTable="device_type_catalogs"; catalogField="type"; }
    else if(section=="device" && (action=="electric-control-types" || action=="electric-control-type-summary")) { catalogTable="electric_control_type_catalogs"; catalogField="electricControlType"; }
    else if(section=="pattern" && (action=="types" || action=="type-summary")) { catalogTable="pattern_type_catalogs"; catalogField="patternType"; }
    else if(section=="pattern" && action=="order-summary") { catalogTable="order_no_catalogs"; catalogField="orderNo"; }
    if(!catalogTable.isEmpty()) {
        const bool summary=action.endsWith("summary");
        if(req.method=="GET") {
            QMap<QString,QJsonObject> entries;
            QSet<QString> catalogValues;
            for(const auto &value:db_->rows(catalogTable)) {
                const QString key=value.toObject().value("value").toString();
                catalogValues.insert(key);
                if(!key.isEmpty()) entries[key]={{"value",key},{"deviceCount",0},{"patternCount",0},
                    {"updateTime",value.toObject().value("updatedAt")}};
            }
            for(const auto &value:scoped(db_->rows(table,{}, {},"id DESC",10000),user)) {
                const QJsonObject item=value.toObject(); const QString key=item.value(catalogField).toString();
                if(key.isEmpty()) continue;
                if(!entries.contains(key)) entries[key]={{"value",key},{"deviceCount",0},{"patternCount",0},{"updateTime","-"}};
                QJsonObject row=entries.value(key); const QString countKey=section=="device" ? "deviceCount" : "patternCount";
                row[countKey]=row.value(countKey).toInt()+1;
                if(!catalogValues.contains(key) && (row.value("updateTime").toString()=="-" || item.value("updatedAt").toString()>row.value("updateTime").toString()))row["updateTime"]=item.value("updatedAt");
                entries[key]=row;
            }
            QList<QJsonObject> ordered;
            for(auto it=entries.cbegin();it!=entries.cend();++it)
                if(req.query.queryItemValue("keyword",QUrl::FullyDecoded).isEmpty() || it.key().contains(req.query.queryItemValue("keyword",QUrl::FullyDecoded),Qt::CaseInsensitive)) ordered.append(it.value());
            if(summary) std::stable_sort(ordered.begin(),ordered.end(),[](const QJsonObject &a,const QJsonObject &b) {
                return a.value("updateTime").toString()>b.value("updateTime").toString();
            });
            QJsonArray values;
            for(const QJsonObject &entry:ordered) values.append(summary ? QJsonValue(entry) : entry.value("value"));
            return ok(values);
        }
        if(!summary) return fail(404,"接口不存在");
        if(req.method=="PUT" && req.body.contains("renames")) {
            const auto values=req.body.value("renames");
            if(!values.isArray() || values.toArray().isEmpty() || values.toArray().size()>1000)return fail(400,"请选择需要修改的条目");
            struct Rename {QString oldValue,newValue,temporary;qint64 id=0;};QList<Rename> renames;QSet<QString> oldNames,newNames;
            const QString nonce=QUuid::createUuid().toString(QUuid::WithoutBraces);
            for(const auto &value:values.toArray()) {
                const auto entry=value.toObject();const QString old=entry.value("oldValue").toString().trimmed(),replacement=entry.value("newValue").toString().trimmed();
                if(catalogField=="orderNo" && (!entry.value("newValue").isString() || !QRegularExpression("^[A-Za-z0-9]+$").match(entry.value("newValue").toString()).hasMatch()))return fail(400,"订单编号只允许字母和数字");
                if(old.isEmpty() || replacement.isEmpty())return fail(400,"名称不能为空");
                if(old.size()>100 || replacement.size()>100)return fail(400,"名称不能超过100个字符");
                if(oldNames.contains(old.toCaseFolded()) || newNames.contains(replacement.toCaseFolded()))return fail(400,"批量修改中存在重复名称");
                oldNames.insert(old.toCaseFolded());newNames.insert(replacement.toCaseFolded());
                renames.append({old,replacement,QString("__rename_%1_%2").arg(nonce).arg(renames.size()),0});
            }
            QSqlDatabase connection=db_->connection();if(!connection.transaction())return fail(500,"批量修改失败，请重试");
            auto abort=[&](int code,const QString &message){connection.rollback();return fail(code,message);};
            const QString field=Database::column(catalogField);
            for(auto &rename:renames) {
                QSqlQuery find(connection);find.prepare("SELECT id,deleted_at FROM "+catalogTable+" WHERE value=? LIMIT 1");find.addBindValue(rename.oldValue);
                if(!find.exec())return abort(500,"批量修改失败，请重试");
                const bool found=find.next(),active=found && find.value(1).isNull();
                if(found)rename.id=find.value(0).toLongLong();
                if(!active && db_->count(table,field+"=?",{rename.oldValue})==0)return abort(404,"原名称不存在");
                if(!oldNames.contains(rename.newValue.toCaseFolded())) {
                    QSqlQuery target(connection);target.prepare("SELECT id FROM "+catalogTable+" WHERE value=? LIMIT 1");target.addBindValue(rename.newValue);
                    if(!target.exec())return abort(500,"批量修改失败，请重试");
                    if(target.next() || db_->count(table,field+"=?",{rename.newValue})>0)return abort(400,"名称已存在，整批未修改");
                }
            }
            auto replaceReferences=[&](const QString &from,const QString &to) {
                QSqlQuery query(connection);query.prepare("UPDATE "+table+" SET "+field+"=?,updated_at=CURRENT_TIMESTAMP WHERE "+field+"=? AND deleted_at IS NULL");query.addBindValue(to);query.addBindValue(from);return query.exec();
            };
            // Temporary names free all selected names before assigning replacements, including swaps.
            for(auto &rename:renames) {
                bool saved=false;
                if(rename.id>0) {QSqlQuery query(connection);query.prepare("UPDATE "+catalogTable+" SET value=?,deleted_at=NULL,updated_at=CURRENT_TIMESTAMP WHERE id=?");query.addBindValue(rename.temporary);query.addBindValue(rename.id);saved=query.exec();}
                else {QString error;saved=db_->insert(catalogTable,{{"value",rename.temporary}},&rename.id,&error);}
                if(!saved || !replaceReferences(rename.oldValue,rename.temporary))return abort(400,"名称冲突或保存失败，整批未修改");
            }
            for(const auto &rename:renames) {
                QString error;if(!db_->update(catalogTable,rename.id,{{"value",rename.newValue}},&error) || !replaceReferences(rename.temporary,rename.newValue))return abort(400,"名称冲突或保存失败，整批未修改");
            }
            if(!connection.commit())return abort(500,"批量修改失败，请重试");
            QJsonArray items;for(const auto &rename:renames)items.append(QJsonObject{{"id",rename.id},{"oldValue",rename.oldValue},{"newValue",rename.newValue}});
            return ok(QJsonObject{{"affected",renames.size()},{"items",items}});
        }
        const QString value=req.body.value("value").toString().trimmed();
        if((req.method=="POST" || req.method=="DELETE") && value.isEmpty()) return fail(400,"名称不能为空");
        if(catalogField=="orderNo" && req.method=="POST" && (!req.body.value("value").isString() || !QRegularExpression("^[A-Za-z0-9]+$").match(req.body.value("value").toString()).hasMatch()))return fail(400,"订单编号只允许字母和数字");
        if(req.method=="POST" && value.size()>100) return fail(400,"名称不能超过100个字符");
        if(req.method=="POST") {
            QSqlQuery existing(db_->connection());
            existing.prepare("SELECT id,deleted_at FROM "+catalogTable+" WHERE value=? LIMIT 1"); existing.addBindValue(value);
            if(existing.exec() && existing.next()) {
                if(existing.value(1).isNull()) return fail(400,"名称已存在");
                QSqlQuery restore(db_->connection()); restore.prepare("UPDATE "+catalogTable+" SET deleted_at=NULL,updated_at=CURRENT_TIMESTAMP WHERE id=?");
                restore.addBindValue(existing.value(0));
                return restore.exec() ? ok(QJsonObject{{"value",value}}) : fail(500,restore.lastError().text());
            }
            qint64 id=0; QString error;
            if(!db_->insert(catalogTable,{{"value",value}},&id,&error)) return fail(400,error);
            return ok(db_->row(catalogTable,id));
        }
        const QString old=req.body.value(req.method=="DELETE" ? "value" : "oldValue").toString().trimmed();
        if(req.method=="PUT" || req.method=="DELETE") {
            if(old.isEmpty()) return fail(400,"原名称不能为空");
            const QString replacement=req.body.value("newValue").toString().trimmed();
            if(catalogField=="orderNo" && req.method=="PUT" && (!req.body.value("newValue").isString() || !QRegularExpression("^[A-Za-z0-9]+$").match(req.body.value("newValue").toString()).hasMatch()))return fail(400,"订单编号只允许字母和数字");
            if(req.method=="PUT" && replacement.isEmpty()) return fail(400,"新名称不能为空");
            if(req.method=="PUT" && replacement.size()>100) return fail(400,"名称不能超过100个字符");
            if(req.method=="PUT" && replacement!=old &&
               db_->count(catalogTable,"value=?",{replacement})>0) return fail(400,"名称已存在");
            QSqlDatabase connection=db_->connection();
            if(!connection.transaction()) return fail(500,connection.lastError().text());
            QSqlQuery q(db_->connection());
            q.prepare("UPDATE "+table+" SET "+Database::column(catalogField)+"=? WHERE "+Database::column(catalogField)+"=? AND deleted_at IS NULL");
            q.addBindValue(req.method=="DELETE" ? "" : replacement); q.addBindValue(old);
            if(!q.exec()) { connection.rollback(); return fail(500,q.lastError().text()); }
            QJsonArray found=db_->rows(catalogTable,"value=?",{old});
            if(!found.isEmpty()) {
                const qint64 id=found.first().toObject().value("id").toInteger();
                QString error;
                const bool saved=req.method=="DELETE" ? db_->remove(catalogTable,id,&error) :
                    db_->update(catalogTable,id,{{"value",replacement}},&error);
                if(!saved) { connection.rollback(); return fail(400,error); }
            }
            if(!connection.commit()) { connection.rollback(); return fail(500,connection.lastError().text()); }
            return ok();
        }
    }
    if(req.method=="GET") {
        if(section=="device" && action=="groups") return ok(visibleGroups(db_->rowsAll("groups",{}, {},"sort_order,id"),user));
        if(section=="employee" && action=="groups") return ok(visibleGroups(db_->rowsAll("groups",{}, {},"sort_order,id"),user));
        if(section=="pattern" && action=="queue") return ok(scopedDeviceRecords(db_->rows("download_tasks"),user,db_));
        if(section=="pattern" && action=="log") return ok(scopedDeviceRecords(db_->rows("download_tasks","status IN ('completed','failed','canceled')"),user,db_));
        if(section=="pattern" && action=="device-files") return ok(scopedDeviceRecords(db_->rows("device_pattern_files"),user,db_));
        if(section=="pattern" && action=="upload-queue") return ok(scopedDeviceRecords(db_->rows("upload_tasks"),user,db_));
        if(action=="tree" && section=="group") return ok(tree(db_->rowsAll("groups",{}, {},"sort_order,id"),0));
        if(action=="tree" && section=="device") {
            QJsonArray groups=db_->rowsAll("groups",{}, {},"sort_order,id"), devices=scoped(db_->rowsAll("devices"),user);
            if(!adminRole(user)) groups=visibleGroups(groups,user);
            QJsonArray ungrouped;
            for(const auto &dv:devices) if(dv.toObject().value("groupId").isNull()) {
                QJsonObject node=dv.toObject(); node["label"]=node.value("name"); node["type"]="device"; ungrouped.append(node);
            }
            return ok(QJsonArray{QJsonObject{{"id","ungrouped"},{"label","未分组设备"},{"type","group"},{"isVirtual",true},{"children",ungrouped}},
                QJsonObject{{"id","all"},{"label","总分组"},{"type","group"},{"isVirtual",true},{"children",deviceTree(groups,devices,0)}}});
        }
        if(action=="all" || action=="list" || action.isEmpty()) {
            const QJsonArray source=section=="device" ? db_->rowsAll(table) : db_->rows(table,{}, {},"id DESC",10000);
            QJsonArray all=section=="pattern" ? scopedPatterns(source,user,db_) : scoped(source,user);
            for(const QString &key:{"groupId","role","status"}) all=selected(all,key,req.query.queryItemValue(key,QUrl::FullyDecoded));
            const QString keyword=req.query.queryItemValue("keyword",QUrl::FullyDecoded).trimmed();
            const QString startDate=req.query.queryItemValue("startDate",QUrl::FullyDecoded).trimmed();
            const QString endDate=req.query.queryItemValue("endDate",QUrl::FullyDecoded).trimmed();
            QHash<qint64,QJsonObject> groupsById;
            if(section=="device" || section=="employee" || section=="operator" || section=="user")
                for(const QJsonValue &value:db_->rowsAll("groups")) {
                    const QJsonObject group=value.toObject();groupsById.insert(group.value("id").toInteger(),group);
                }
            QJsonArray filtered;
            for(const QJsonValue &value:all) {
                QJsonObject item=value.toObject();
                QStringList searchFields;
                if(section=="device") {
                    for(const QString &key:{"name","initialName","code","type","electricControlType",
                                            "modelName","ip","employeeCode","employeeName","mainboardSn",
                                            "remark","currentPatternName","alarmCode"}) searchFields << item.value(key).toString();
                } else if(section=="employee") {
                    for(const QString &key:{"name","remark"}) searchFields << item.value(key).toString();
                } else if(section=="pattern") {
                    for(const QString &key:{"name","fileName","patternType","orderNo"}) searchFields << item.value(key).toString();
                } else if(section=="user") {
                    for(const QString &key:{"username","nickname","phone"}) searchFields << item.value(key).toString();
                } else if(section=="operator") {
                    for(const QString &key:{"username","nickname"}) searchFields << item.value(key).toString();
                } else if(section=="role") {
                    for(const QString &key:{"name","remark"}) searchFields << item.value(key).toString();
                } else searchFields << item.value("name").toString();
                const QString deviceName=item.value("name").toString().trimmed();
                const bool reverseName=section=="device" && !deviceName.isEmpty() && keyword.contains(deviceName,Qt::CaseInsensitive);
                if(!keyword.isEmpty() && !searchFields.join(' ').contains(keyword,Qt::CaseInsensitive) && !reverseName) continue;
                const QString created=(section=="pattern" && !item.value("uploadedAt").toString().isEmpty() ?
                    item.value("uploadedAt") : item.value("createdAt")).toString().left(10);
                if(!startDate.isEmpty() && created<startDate) continue;
                if(!endDate.isEmpty() && created>endDate) continue;
                if(section=="device") {
                    const QString name=req.query.queryItemValue("name",QUrl::FullyDecoded).trimmed();
                    if(!name.isEmpty() && !item.value("name").toString().contains(name,Qt::CaseInsensitive) &&
                       (item.value("name").toString().isEmpty() || !name.contains(item.value("name").toString(),Qt::CaseInsensitive)) &&
                       !item.value("initialName").toString().contains(name,Qt::CaseInsensitive)) continue;
                    const QString type=req.query.queryItemValue("type",QUrl::FullyDecoded), control=req.query.queryItemValue("electricControlType",QUrl::FullyDecoded);
                    if(!type.isEmpty() && item.value("type").toString()!=type) continue;
                    if(!control.isEmpty() && item.value("electricControlType").toString()!=control) continue;
                    QString displayName=item.value("name").toString();
                    if(displayName.isEmpty()) displayName=item.value("initialName").toString();
                    if(displayName.isEmpty()) displayName=item.value("code").toString();
                    item["displayName"]=displayName;
                }
                if(section=="employee") {
                    const QString code=req.query.queryItemValue("code",QUrl::FullyDecoded),phone=req.query.queryItemValue("phone",QUrl::FullyDecoded);
                    const QString department=req.query.queryItemValue("department",QUrl::FullyDecoded);
                    if(!code.isEmpty() && !item.value("code").toString().contains(code)) continue;
                    if(!phone.isEmpty() && !item.value("phone").toString().contains(phone)) continue;
                    if(!department.isEmpty() && item.value("department").toString()!=department) continue;
                }
                if(section=="pattern") {
                    const QString type=req.query.queryItemValue("patternType",QUrl::FullyDecoded),order=req.query.queryItemValue("orderNo",QUrl::FullyDecoded);
                    if(!type.isEmpty() && item.value("patternType").toString()!=type) continue;
                    if(!order.isEmpty() && !item.value("orderNo").toString().contains(order)) continue;
                    item["size"]=QString::number(item.value("fileSize").toInteger());
                    item["uploadTime"]=item.value("uploadedAt").toString().isEmpty() ? item.value("createdAt") : item.value("uploadedAt");
                    const QString downloadName=portablePatternFilename(item.value("name").toString(),QFileInfo(item.value("fileName").toString()).suffix());
                    item["downloadFileName"]=downloadName;
                }
                if(section=="device" || section=="employee" || section=="operator" || section=="user") {
                    const QJsonObject group=groupsById.value(item.value("groupId").toInteger());
                    if(!group.isEmpty()) { item["groupName"]=group.value("name"); item["group"]=group; }
                }
                if(section=="user" && action=="all") {
                    QJsonArray ids=QJsonDocument::fromJson(item.value("groupIds").toString().toUtf8()).array();
                    if(item.value("groupId").toInteger()>0 && !ids.contains(item.value("groupId"))) ids.append(item.value("groupId"));
                    item["groupIds"]=ids;
                    QJsonArray groups;
                    for(const QJsonValue &idValue:ids) {
                        const QJsonObject group=groupsById.value(idValue.toInteger());
                        if(!group.isEmpty()) groups.append(group);
                    }
                    item["groups"]=groups;
                    const QJsonArray roles=db_->rows("roles","name=?",{item.value("role").toString()},"id",1);
                    if(!roles.isEmpty()) item["permissions"]=roles.first().toObject().value("permissions");
                }
                filtered.append(item);
            }
            all=filtered;
            // Local tree filtering/reordering needs the complete authorized
            // device model. Ordinary list calls remain bounded paged responses.
            if(section=="device" && req.query.queryItemValue("fullModel",QUrl::FullyDecoded)=="1")return ok(all);
            if(section=="employee" || section=="device" || section=="pattern") {
                const int page=qMax(1,req.query.queryItemValue("page",QUrl::FullyDecoded).toInt());
                const int rawSize=req.query.queryItemValue("pageSize",QUrl::FullyDecoded).toInt();
                const int size=qBound(1,rawSize>0 ? rawSize : 10,section=="device" ? 2000 : 200);
                QJsonArray list; for(int i=(page-1)*size;i<qMin(page*size,all.size());++i) list.append(all[i]);
                return ok(QJsonObject{{"list",list},{"total",all.size()},{"page",page},{"pageSize",size}});
            }
            return ok(all);
        }
        bool idOk=false; const qint64 id=action.toLongLong(&idOk);
        if(idOk) {
            QJsonObject item=db_->row(table,id);
            const bool visible=section=="pattern" ? !scopedPatterns(QJsonArray{item},user,db_).isEmpty() : inScope(item,user);
            return item.isEmpty() || !visible ? fail(404,"记录不存在") : ok(item);
        }
    }
    if(req.method=="POST" && parts.isEmpty()) {
        if(section=="pattern") return fail(404,"请使用花型文件上传接口");
        QJsonObject body=req.body;
        if(section=="group") {
            const QString name=body.value("name").toString().trimmed();
            if(name.isEmpty() || name.size()>50) return fail(400,"分组名称不能为空且不能超过50个字符");
            body["name"]=name;
            if(!body.contains("parentId")) body["parentId"]=1;
            const qint64 parent=body.value("parentId").toInteger();
            if(!body.value("parentId").isNull() && (!body.value("parentId").isDouble() || parent<1 || db_->row("groups",parent).isEmpty())) return fail(400,"父分组不存在");
            if(db_->count("groups","COALESCE(parent_id,0)=? AND name=?",{parent,name})>0) return fail(400,"同级分组名称已存在");
            QSqlQuery sibling(db_->connection()); sibling.prepare(
                "SELECT COALESCE(MAX(sort_order),0)+1 FROM `groups` WHERE COALESCE(parent_id,0)=? AND deleted_at IS NULL");
            sibling.addBindValue(parent);
            if(sibling.exec() && sibling.next()) body["sortOrder"]=sibling.value(0).toInt();
        }
        if(section=="role") {
            const QString name=body.value("name").toString().trimmed();
            if(name.isEmpty() || name.toUcs4().size()>10) return fail(400,"角色名称不能为空且不超过10个字");
            body["name"]=name;
            if(db_->count("roles","name=?",{name})>0) return fail(400,"角色名称已存在");
            const QString permissions=body.value("permissions").toString();
            if(!permissions.isEmpty() && !QJsonDocument::fromJson(permissions.toUtf8()).isObject())
                return fail(400,"权限配置格式不正确");
            if(permissions.isEmpty())
                body["permissions"]=R"({"home":true,"dashboard":true,"deviceManagement":true,"fileManagement":true,"statistics":true,"employeeManagement":true,"remoteMonitoring":true})";
        }
        if(section=="user" || section=="operator") {
            const QString username=body.value("username").toString().trimmed();
            const QString password=body.value("password").toString().trimmed();
            const QRegularExpression pattern(section=="user" ? "^[A-Za-z0-9]{1,11}$" : "^[A-Za-z0-9_]{1,11}$");
            if(!pattern.match(username).hasMatch())
                return fail(400,section=="user" ? "账号仅支持字母数字，且不超过11位" : "账号仅支持字母数字下划线，且不超过11位");
            if(password.size()<6 || password.size()>32) return fail(400,"密码长度需在6-32位");
            if(db_->count(table,"username=?",{username})>0) return fail(400,"账号已存在");
            body["username"]=username;
            if(section=="user") {
                const QString nickname=body.value("nickname").toString().trimmed();
                if(nickname.isEmpty() || nickname.size()>50) return fail(400,"账号姓名不能为空且不能超过50个字符");
                body["nickname"]=nickname;
                QString phone=body.value("phone").toString(); phone.remove(QRegularExpression("\\s+"));
                if(!QRegularExpression("^1[3-9][0-9]{9}$").match(phone).hasMatch())
                    return fail(400,"手机号不能为空且格式需正确");
                body["phone"]=phone;
                const QString email=body.value("email").toString().trimmed();
                if(email.size()>100 || (!email.isEmpty() && !QRegularExpression("^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$").match(email).hasMatch()))
                    return fail(400,"邮箱格式不正确或超过100个字符");
                body["email"]=email;
                const QString role=body.value("role").toString().trimmed();
                const QJsonArray roles=db_->rows("roles","name=?",{role},"id",1);
                if(role.isEmpty() || roles.isEmpty()) return fail(400,"角色不存在，请先在权限角色中创建");
                QJsonArray ids=body.value("groupIds").toArray();
                if(body.value("groupId").toInteger()>0 && !ids.contains(body.value("groupId"))) ids.append(body.value("groupId"));
                QJsonArray normalized; QSet<qint64> seen;
                for(const QJsonValue &value:ids) {
                    const qint64 id=value.toInteger();
                    if(id<1 || db_->row("groups",id).isEmpty()) return fail(400,"分组参数错误");
                    if(!seen.contains(id)) { seen.insert(id); normalized.append(id); }
                }
                if(role.compare("admin",Qt::CaseInsensitive)!=0 && normalized.size()>1) return fail(400,"非管理员仅允许选择一个分组");
                body["groupIds"]=normalized;
                body["groupId"]=normalized.isEmpty() ? QJsonValue(QJsonValue::Null) : normalized.first();
                if(body.value("permissions").toString().isEmpty())
                    body["permissions"]=roles.first().toObject().value("permissions");
            } else {
                const qint64 groupId=body.value("groupId").toInteger();
                if(groupId>0 && db_->row("groups",groupId).isEmpty()) return fail(400,"分组参数错误");
            }
            body["password"]=Database::hashPassword(password);
        }
        if(section=="employee") {
            const QString code=body.value("code").toString().trimmed(), name=body.value("name").toString().trimmed();
            QString phone=body.value("phone").toString(); phone.remove(QRegularExpression("\\s+"));
            if(!QRegularExpression("^[0-9]{1,11}$").match(code).hasMatch() || name.isEmpty() || name.size()>8)
                return fail(400,"工号仅支持1-11位数字，姓名不能为空且不能超过8个字符");
            if(!QRegularExpression("^1[3-9][0-9]{9}$").match(phone).hasMatch())
                return fail(400,"手机号不能为空且格式需正确");
            if(db_->count("employees","code=?",{code})>0) return fail(400,"员工工号已存在");
            qint64 groupId=body.value("groupId").toInteger();
            if(groupId<1) return fail(400,"请选择所属分组");
            if(db_->row("groups",groupId).isEmpty()) return fail(400,"所属分组不存在");
            body["code"]=code; body["name"]=name; body["phone"]=phone; body["groupId"]=groupId;
        }
        if(section=="device") {
            const QString code=body.value("code").toString().trimmed(), name=body.value("name").toString().trimmed();
            if(code.isEmpty() || name.isEmpty() || name.size()>100 || body.value("remark").toString().size()>255)
                return fail(400,"设备编码和名称不能为空");
            body["code"]=code; body["name"]=name;
            if(body.value("initialName").toString().trimmed().isEmpty()) body["initialName"]=name;
            const qint64 groupId=body.value("groupId").toInteger();
            if(groupId>0 && db_->row("groups",groupId).isEmpty()) return fail(400,"分组不存在");
            if(db_->count("devices","name=? AND COALESCE(group_id,0)=?",{name,groupId})>0)
                return fail(400,"同组设备名称已存在");
        }
        if(!adminRole(user) && !inScope(body,user)) return fail(403,"分组超出授权范围");
        qint64 id=0; QString error;
        if(!db_->insert(table,body,&id,&error)) return fail(400,error);
        return ok(db_->row(table,id));
    }
    bool idOk=false; qint64 id=action.toLongLong(&idOk);
    if(idOk && req.method=="PUT") {
        QJsonObject existing=db_->row(table,id);
        const bool visible=section=="pattern" ? !scopedPatterns(QJsonArray{existing},user,db_).isEmpty() : inScope(existing,user);
        if(existing.isEmpty() || !visible) return fail(404,"记录不存在");
        QJsonObject body=req.body;
        if(section=="group") {
            if(body.contains("parentId")) return fail(400,"分组不支持修改父分组");
            if(body.contains("name")) {
                const QString name=body.value("name").toString().trimmed();
                if(name.isEmpty() || name.size()>50) return fail(400,"分组名称不能为空且不能超过50个字符");
                if(db_->count("groups","COALESCE(parent_id,0)=? AND name=? AND id<>?",
                              {existing.value("parentId").toInteger(),name,id})>0) return fail(400,"同级分组名称已存在");
                body["name"]=name;
            }
        }
        if(section=="role") {
            if(body.contains("name")) {
                const QString name=body.value("name").toString().trimmed();
                if(name.isEmpty() || name.toUcs4().size()>10) return fail(400,"角色名称不能为空且不超过10个字");
                if(db_->count("roles","name=? AND id<>?",{name,id})>0) return fail(400,"角色名称已存在");
                body["name"]=name;
            }
            if(body.contains("permissions") &&
               !QJsonDocument::fromJson(body.value("permissions").toString().toUtf8()).isObject())
                return fail(400,"权限数据格式错误");
        }
        if(section=="user" || section=="operator") {
            if(body.contains("username") && body.value("username").toString().trimmed()!=existing.value("username").toString())
                return fail(400,"账号不允许修改");
            body.remove("username");
            if(body.contains("password") && !body.value("password").toString().isEmpty()) {
                const int length=body.value("password").toString().trimmed().size();
                if(length<6 || length>32) return fail(400,"密码长度需在6-32位");
            }
            if(body.contains("groupId") && !body.value("groupId").isNull() &&
               db_->row("groups",body.value("groupId").toInteger()).isEmpty()) return fail(400,"分组参数错误");
            if(section=="user") {
                if(body.contains("nickname")) {
                    const QString name=body.value("nickname").toString().trimmed();
                    if(name.isEmpty() || name.size()>50) return fail(400,"账号姓名不能为空且不能超过50个字符");
                    body["nickname"]=name;
                }
                if(body.contains("phone")) {
                    QString phone=body.value("phone").toString(); phone.remove(QRegularExpression("\\s+"));
                    if(!QRegularExpression("^1[3-9][0-9]{9}$").match(phone).hasMatch())
                        return fail(400,"手机号不能为空且格式需正确");
                    body["phone"]=phone;
                }
                if(body.contains("email")) {
                    const QString email=body.value("email").toString().trimmed();
                    if(email.size()>100 || (!email.isEmpty() &&
                        !QRegularExpression("^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$").match(email).hasMatch()))
                        return fail(400,"邮箱格式不正确或超过100个字符");
                    body["email"]=email;
                }
                const QString role=body.value("role").toString().isEmpty() ?
                    existing.value("role").toString() : body.value("role").toString().trimmed();
                const QJsonArray roles=db_->rows("roles","name=?",{role},"id",1);
                if(roles.isEmpty()) return fail(400,"角色不存在");
                if(body.contains("role") && !body.contains("permissions"))
                    body["permissions"]=roles.first().toObject().value("permissions");
                if(body.contains("permissions") &&
                   !QJsonDocument::fromJson(body.value("permissions").toString().toUtf8()).isObject())
                    return fail(400,"权限数据格式错误");
                if(body.contains("groupIds") || body.contains("groupId")) {
                    QJsonArray ids=body.contains("groupIds") ? body.value("groupIds").toArray() :
                        QJsonArray{};
                    if(body.contains("groupId") && !body.value("groupId").isNull() &&
                       !ids.contains(body.value("groupId"))) ids.append(body.value("groupId"));
                    QJsonArray normalized; QSet<qint64> seen;
                    for(const QJsonValue &value:ids) {
                        const qint64 groupId=value.toInteger();
                        if(groupId<1 || db_->row("groups",groupId).isEmpty()) return fail(400,"分组参数错误");
                        if(!seen.contains(groupId)) { seen.insert(groupId); normalized.append(groupId); }
                    }
                    if(role.compare("admin",Qt::CaseInsensitive)!=0 && normalized.size()>1) return fail(400,"非管理员仅允许选择一个分组");
                    body["groupIds"]=normalized;
                    body["groupId"]=normalized.isEmpty() ? QJsonValue(QJsonValue::Null) : normalized.first();
                }
            }
        }
        if(section=="employee") {
            for(const QString &key:{"code","department","position"})
                if(body.contains(key)) return fail(400,"仅支持修改员工姓名、手机号、所属分组和备注");
            if(body.contains("name")) {
                const QString name=body.value("name").toString().trimmed();
                if(name.isEmpty() || name.size()>8) return fail(400,"员工姓名不能为空且不能超过8个字符");
                body["name"]=name;
            }
            if(body.contains("phone")) {
                QString phone=body.value("phone").toString(); phone.remove(QRegularExpression("\\s+"));
                if(!QRegularExpression("^1[3-9][0-9]{9}$").match(phone).hasMatch())
                    return fail(400,"手机号不能为空且格式需正确");
                body["phone"]=phone;
            }
            if(body.contains("groupId") &&
               (body.value("groupId").isNull() || db_->row("groups",body.value("groupId").toInteger()).isEmpty()))
                return fail(400,"所属分组不合法");
        }
        if(section=="device") {
            if(body.contains("code") && body.value("code").toString().trimmed().isEmpty()) return fail(400,"设备编码不能为空");
            if(body.contains("name")) {
                const QString name=body.value("name").toString().trimmed();
                if(name.isEmpty() || name.size()>100) return fail(400,"设备名称不能为空且不能超过100个字符");
                body["name"]=name;
            }
            if(body.contains("remark") && body.value("remark").toString().size()>255) return fail(400,"备注不能超过255个字符");
            if(body.contains("status") &&
               !QStringList{"offline","idle","working"}.contains(body.value("status").toString()))
                return fail(400,"设备状态只能为空闲、缝纫或关机");
            if(body.contains("groupId") && !body.value("groupId").isNull() &&
               db_->row("groups",body.value("groupId").toInteger()).isEmpty()) return fail(400,"目标分组不存在");
            if(body.contains("name") || body.contains("groupId")) {
                const QString name=body.value("name").toString(existing.value("name").toString());
                const qint64 group=body.contains("groupId") ? body.value("groupId").toInteger() : existing.value("groupId").toInteger();
                if(db_->count("devices","name=? AND COALESCE(group_id,0)=? AND id<>?",{name,group,id})>0)
                    return fail(400,"同组设备名称已存在");
            }
        }
        if(section=="pattern") {
            QString invalid; body=patternUpdates(req.body,&invalid);
            if(!invalid.isEmpty()) return fail(400,invalid);
            if(body.isEmpty()) return fail(400,"没有可更新的字段");
        }
        if(!adminRole(user) && body.contains("groupId") && !inScope(body,user)) return fail(403,"分组超出授权范围");
        if((section=="user" || section=="operator") && body.contains("password")) {
            if(body.value("password").toString().isEmpty()) body.remove("password");
            else body["password"]=Database::hashPassword(body.value("password").toString());
        }
        QString error; if(!db_->update(table,id,body,&error)) return fail(400,error);
        if(section=="device" && deviceServer_ &&
           (body.contains("employeeCode") || body.contains("employeeName"))) {
            const QJsonObject updated=db_->row("devices",id);
            deviceServer_->sendCurrentUser(id,updated.value("employeeCode").toString(),
                updated.value("employeeName").toString());
        }
        if(section=="pattern") for(const auto &catalog:{
            QPair<QString,QString>{"pattern_type_catalogs",body.value("patternType").toString()},
            QPair<QString,QString>{"order_no_catalogs",body.value("orderNo").toString()}})
            if(!catalog.second.isEmpty() && db_->rows(catalog.first,"value=?",{catalog.second},"id",1).isEmpty())
                db_->insert(catalog.first,{{"value",catalog.second}},nullptr,nullptr);
        if(section=="role" && body.contains("name") && body.value("name")!=existing.value("name")) {
            QSqlQuery q(db_->connection()); q.prepare("UPDATE users SET role=? WHERE role=? AND deleted_at IS NULL");
            q.addBindValue(body.value("name").toString()); q.addBindValue(existing.value("name").toString()); q.exec();
        }
        if(section=="role" && body.contains("permissions")) {
            QSqlQuery q(db_->connection()); q.prepare("UPDATE users SET permissions=? WHERE role=? AND deleted_at IS NULL");
            q.addBindValue(body.value("permissions").toString()); q.addBindValue(body.contains("name") ? body.value("name").toString() : existing.value("name").toString()); q.exec();
        }
        return ok(db_->row(table,id));
    }
    if(req.method=="DELETE") {
        QJsonArray ids=req.body.value("ids").toArray();
        if(ids.isEmpty()) ids.append(idOk ? QJsonValue(id) : req.body.value("id"));
        const bool deviceBatch=section=="device" && action=="batch";
        const bool deviceHard=section=="device" && (parts.contains("hard"));
        if(section=="device" && parts.size()>1 && (parts.size()!=2 || parts[1]!="hard")) return fail(404,"接口不存在");
        if(section=="device" && (deviceBatch || deviceHard) && req.body.value("ids").toArray().isEmpty() && !idOk)
            return fail(400,"设备ID不能为空");
        if(section=="device" && deviceBatch && parts.size()>1 && parts[1]!="hard") return fail(404,"接口不存在");
        for(const auto &value:ids) {
            const qint64 selectedId=value.toInteger();
            if(selectedId<1) return fail(400,"缺少 ID");
            const QJsonObject item=db_->row(table,selectedId);
            const bool visible=section=="pattern" ? !scopedPatterns(QJsonArray{item},user,db_).isEmpty() : inScope(item,user);
            if(item.isEmpty() || !visible) return fail(404,"记录不存在");
            if(section=="user" && selectedId==user.value("id").toInteger()) return fail(400,"不能删除当前账号");
            if(section=="group" && selectedId==1) return fail(400,"不能删除总分组");
            if(section=="group" && (db_->count("groups","parent_id=?",{selectedId})>0 || db_->count("users","group_id=?",{selectedId})>0 || db_->count("operators","group_id=?",{selectedId})>0 || db_->count("employees","group_id=?",{selectedId})>0)) return fail(400,"分组仍包含子分组、账号或员工");
            if(section=="role") {
                const QString name=db_->row("roles",selectedId).value("name").toString();
                if(name=="admin" || name=="user") return fail(400,"系统默认角色不能删除");
                if(db_->count("users","role=?",{name})>0) return fail(400,"该角色下仍有关联账号");
            }
        }
        QString error;
        if(section=="group") {
            for(const auto &value:ids) if(!db_->removeGroup(value.toInteger(),&error)) return fail(400,error);
            return ok();
        }
        if(section=="device") {
            QSqlDatabase connection=db_->connection();
            if(!connection.transaction()) return fail(500,connection.lastError().text());
            for(const auto &value:ids) {
                const qint64 deviceId=value.toInteger();
                if(deviceHard) {
                    for(const QString &dependent:{"employee_devices","device_pattern_files","upload_tasks","download_tasks"}) {
                        QSqlQuery q(connection);
                        q.prepare("UPDATE "+dependent+" SET deleted_at=CURRENT_TIMESTAMP WHERE device_id=? AND deleted_at IS NULL");
                        q.addBindValue(deviceId);
                        if(!q.exec()) { connection.rollback(); return fail(500,q.lastError().text()); }
                    }
                    if(!db_->remove("devices",deviceId,&error)) { connection.rollback(); return fail(500,error); }
                } else if(!db_->update("devices",deviceId,{{"groupId",QJsonValue::Null},{"sortOrder",0}},&error)) {
                    connection.rollback(); return fail(500,error);
                }
            }
            if(!connection.commit()) return fail(500,connection.lastError().text());
            if(deviceHard) for(const auto &value:ids) emit deviceDeleted(value.toInteger());
            return ok(QJsonValue(),deviceHard ? "设备已删除" : "设备已移出分组");
        }
        QStringList patternPaths;
        if(section=="pattern") for(const QJsonValue &value:ids)
            patternPaths << db_->row("patterns",value.toInteger()).value("filePath").toString();
        for(const auto &value:ids) if(!db_->remove(table,value.toInteger(),&error)) return fail(400,error);
        for(const QString &path:patternPaths) if(!path.isEmpty()) QFile::remove(path);
        return ok();
    }
    return fail(404,"接口尚未实现");
}

ApiServer::Response ApiServer::system(const Request &req,const QJsonObject &user,const QStringList &parts) {
    if(!adminRole(user)) return fail(403,"需要管理员权限");
    const QString action=parts.isEmpty() ? QString() : parts.first();
    if(action=="database" && parts.size()>=2) {
        const QString sub=parts[1];
        const QJsonObject config=ExternalDb::normalize(
            QJsonDocument::fromJson(db_->config("external_db_config").toUtf8()).object());
        if(sub=="config" && req.method=="GET") return ok(config);
        if(sub=="config" && req.method=="POST") {
            QJsonObject updated=ExternalDb::normalize(req.body);
            const QString invalid=ExternalDb::validate(updated,updated.value("enabled").toBool());
            if(!invalid.isEmpty()) return fail(400,invalid);
            updated["updatedAt"]=QDateTime::currentSecsSinceEpoch();
            if(!db_->setConfig("external_db_config",QString::fromUtf8(QJsonDocument(updated).toJson(QJsonDocument::Compact))))
                return fail(500,"保存数据库配置失败");
            return ok(updated,"配置已保存");
        }
        if(sub=="test" && req.method=="POST") {
            const QJsonObject proposed=ExternalDb::normalize(req.body);
            const QString invalid=ExternalDb::validate(proposed,true);
            if(!invalid.isEmpty()) return fail(400,invalid);
            QString error,writeError; bool writable=false;
            if(!ExternalDb::test(proposed,&error,&writable,&writeError))
                return {200,{{"code",1},{"message",proposed.value("dbType").toString().toUpper()+"连接失败: "+error},
                    {"data",QJsonObject{{"success",false},{"writable",false}}}}};
            return ok(QJsonObject{{"success",true},{"writable",writable},{"writeError",writeError}},
                writable ? "数据库连接成功，可进行同步" : "数据库连接成功，但缺少同步写入权限");
        }
        if(sub=="sync-status" && req.method=="GET") {
            const QString last=db_->config("external_db_last_sync_at");
            QDateTime lastTime=QDateTime::fromString(last,"yyyy-MM-dd HH:mm:ss");
            if(!lastTime.isValid()) lastTime=QDateTime::fromString(last,Qt::ISODate);
            const qint64 lastUnix=lastTime.isValid() ? lastTime.toSecsSinceEpoch() : last.toLongLong();
            const qint64 nextUnix=lastUnix>0 ? lastUnix+config.value("syncIntervalMinutes").toInt()*60 : 0;
            QString status="disabled";
            if(config.value("enabled").toBool())
                status=lastUnix==0 ? "waiting_first_sync" : QDateTime::currentSecsSinceEpoch()>=nextUnix ? "due" : "scheduled";
            return ok(QJsonObject{{"enabled",config.value("enabled")},{"dbType",config.value("dbType")},
                {"syncIntervalMinutes",config.value("syncIntervalMinutes")},
                {"lastSyncAt",lastUnix},{"nextSyncAt",nextUnix},{"status",status}});
        }
        if(sub=="sync-now" && req.method=="POST") {
            if(syncing_.exchange(true)) return fail(409,"同步任务正在执行");
            QString error;
            const bool saved=ExternalDb::sync(db_->connectionOptions(),config,true,&error);
            syncing_=false;
            if(!saved) return {200,{{"code",1},{"message","同步失败: "+error}}};
            return ok(QJsonValue(),"同步任务已执行");
        }
        return fail(404,"接口不存在");
    }
    if(action=="info" && req.method=="GET") {
        QJsonArray ips;
        for(const QHostAddress &addr:QNetworkInterface::allAddresses()) if(addr.protocol()==QAbstractSocket::IPv4Protocol && !addr.isLoopback()) ips.append(addr.toString());
        return ok(QJsonObject{{"ips",ips},{"port",int(server_.serverPort())},{"tcpPort",int(devicePort_)},{"workDir",QDir::currentPath()},
            {"dataDir",db_->dataDirectory()},{"os",QSysInfo::productType()},{"arch",QSysInfo::currentCpuArchitecture()},
            {"version",("qt-"+QCoreApplication::applicationVersion())},{"uptime",QDateTime::currentSecsSinceEpoch()}});
    }
    if(action=="stats" && req.method=="GET") return ok(QJsonObject{
        {"deviceCount",db_->count("devices")},{"onlineDeviceCount",db_->count("devices","status IN ('working','idle')")},
        {"userCount",db_->count("users")},{"employeeCount",db_->count("employees")},
        {"operatorCount",db_->count("operators")},{"groupCount",db_->count("groups")}});
    if(action=="network" && req.method=="GET") {
        QJsonArray interfaces;
        for(const QNetworkInterface &iface:QNetworkInterface::allInterfaces()) {
            QJsonArray addresses;
            for(const QNetworkAddressEntry &entry:iface.addressEntries())
                addresses.append(entry.ip().toString()+"/"+QString::number(entry.prefixLength()));
            QStringList flags;
            const auto state=iface.flags();
            if(state & QNetworkInterface::IsUp) flags << "up";
            if(state & QNetworkInterface::IsRunning) flags << "running";
            if(state & QNetworkInterface::CanBroadcast) flags << "broadcast";
            if(state & QNetworkInterface::IsLoopBack) flags << "loopback";
            if(state & QNetworkInterface::CanMulticast) flags << "multicast";
            if(state & QNetworkInterface::IsPointToPoint) flags << "pointtopoint";
            interfaces.append(QJsonObject{{"name",iface.name()},{"mac",iface.hardwareAddress().toLower()},
                {"flags",flags.join('|')},{"mtu",iface.maximumTransmissionUnit()},{"addresses",addresses}});
        }
        return ok(interfaces);
    }
    if(action=="ping" && req.method=="POST") {
        const QString address=req.query.queryItemValue("ip",QUrl::FullyDecoded).trimmed();
        if(address.isEmpty()) return fail(400,"IP地址不能为空");
        static const QRegularExpression validAddress("^[A-Za-z0-9.:-]+$");
        if(!validAddress.match(address).hasMatch()) return fail(400,"IP地址不合法");
        QProcess process;
#ifdef Q_OS_WIN
        process.start("ping",{"-n","4",address});
#else
        process.start("ping",{"-c","4",address});
#endif
        const bool started=process.waitForStarted(2000);
        const bool finished=started && process.waitForFinished(10000);
        if(started && !finished) { process.kill(); process.waitForFinished(1000); }
        const QString output=QString::fromLocal8Bit(process.readAllStandardOutput()+process.readAllStandardError());
        return ok(QJsonObject{{"output",output},{"success",finished && process.exitCode()==0}});
    }
    if(action=="command" && req.method=="POST") {
        const QString command=req.body.value("command").toString().trimmed();
        static const QSet<QString> allowed={"ipconfig","ifconfig","arp","ping","netstat","hostname","control","tasklist"};
        if(command.isEmpty()) return fail(400,"命令不能为空");
        if(!allowed.contains(command)) return fail(403,"不允许执行该命令");
        QStringList args; for(const QJsonValue &value:req.body.value("args").toArray()) args << value.toString();
        QProcess process; process.start(command,args);
        const bool started=process.waitForStarted(2000);
        const bool finished=started && process.waitForFinished(10000);
        if(started && !finished) { process.kill(); process.waitForFinished(1000); }
        const QString output=QString::fromLocal8Bit(process.readAllStandardOutput()+process.readAllStandardError());
        if(!finished || process.exitCode()!=0) return {200,{{"code",1},{"data",QJsonObject{{"output",output},{"error",process.errorString()}}}}};
        return ok(QJsonObject{{"output",output}});
    }
    if(action=="logs") {
        if(req.method=="GET") {
            const int rawLimit=req.query.queryItemValue("limit",QUrl::FullyDecoded).toInt();
            const int limit=qBound(1,rawLimit>0 ? rawLimit : 100,10000);
            const QString level=req.query.queryItemValue("level",QUrl::FullyDecoded).trimmed();
            return ok(level.isEmpty() ? db_->rows("debug_logs",{}, {},"created_at DESC,id DESC",limit) :
                db_->rows("debug_logs","level=?",{level},"created_at DESC,id DESC",limit));
        }
        if(req.method=="POST") {
            if(req.body.value("level").toString().trimmed().isEmpty() ||
               req.body.value("message").toString().trimmed().isEmpty()) return fail(400,"级别和日志内容不能为空");
            const QString enabled=db_->config("debug_output_enabled","true").trimmed().toLower();
            if(QStringList{"0","false","no","off"}.contains(enabled)) return ok(QJsonValue(),"调试输出已关闭");
            qint64 id=0; QString error;
            if(!db_->insert("debug_logs",req.body,&id,&error)) return fail(400,error);
            return ok(db_->row("debug_logs",id));
        }
        if(req.method=="DELETE") { QSqlQuery q(db_->connection()); return q.exec("DELETE FROM debug_logs") ? ok() : fail(500,q.lastError().text()); }
    }
    if(action=="config") {
        if(req.method=="GET") {
            const QString key=req.query.queryItemValue("key",QUrl::FullyDecoded);
            if(key.isEmpty()) return ok(db_->rows("server_configs"));
            const QJsonArray found=db_->rows("server_configs","`key`=?",{key},"id",1);
            return found.isEmpty() ? fail(404,"配置项不存在") : ok(found.first());
        }
        if(req.method=="POST") {
            const QString key=req.body.value("key").toString().trimmed();
            QString value=req.body.value("value").toString();
            if(key.isEmpty()) return fail(400,"配置项不能为空");
            if(key=="shared_folder") {
                value=value.trimmed();
                const QString invalid=sharedFolderError(value);
                if(!invalid.isEmpty()) return fail(400,invalid);
            }
            if(!db_->setConfig(key,value)) return fail(500,"保存配置失败");
            const QJsonArray found=db_->rows("server_configs","`key`=?",{key},"id",1);
            if(!found.isEmpty()) db_->update("server_configs",found.first().toObject().value("id").toInteger(),{{"desc",req.body.value("desc")}},nullptr);
            return ok(QJsonValue(),"配置已保存");
        }
    }
    return fail(404,"接口尚未实现");
}
