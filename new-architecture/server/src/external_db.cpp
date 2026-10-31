#include "external_db.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlField>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

namespace {
const QStringList syncTables={"groups","devices","employees","patterns",
    "production_records","alarm_records","salary_records"};
QMutex syncMutex;

QString uniqueName(const QString &prefix) {
    return prefix+QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QString quote(const QString &name,bool mysql) { return mysql ? "`"+name+"`" : "["+name+"]"; }
QString fieldType(const QString &name,bool mysql) {
    if(name=="id") return "BIGINT PRIMARY KEY";
    static const QSet<QString> realFields={"width","height","unit_price","thread_length",
        "running_time","idle_time","salary","bonus","total_amount"};
    static const QSet<QString> integerFields={"parent_id","sort_order","group_id","employee_id",
        "device_id","pattern_id","pattern_no","file_size","stitches","colors","uploaded_by",
        "pieces","duration","source_production_id","start_needle","end_needle","stop_reason"};
    if(realFields.contains(name)) return mysql ? "DOUBLE" : "FLOAT";
    if(integerFields.contains(name) || name.endsWith("_count") || name.endsWith("_total"))
        return "BIGINT";
    return mysql ? "LONGTEXT" : "NVARCHAR(MAX)";
}
bool openRemote(const QJsonObject &config,const QString &name,QSqlDatabase *out,QString *error) {
    const bool mysql=config.value("dbType").toString()=="mysql";
    const QString driver=mysql ? "QMYSQL" : "QODBC";
    if(!QSqlDatabase::drivers().contains(driver)) {
        if(error) *error=driver+" 驱动未安装；请在 Qt sqldrivers 目录部署该驱动及其依赖库";
        return false;
    }
    QSqlDatabase remote=QSqlDatabase::addDatabase(driver,name);
    remote.setHostName(config.value("host").toString());
    remote.setPort(config.value("port").toInt());
    remote.setUserName(config.value("username").toString());
    remote.setPassword(config.value("password").toString());
    if(mysql) {
        remote.setDatabaseName(config.value("database").toString());
        remote.setConnectOptions("MYSQL_OPT_CONNECT_TIMEOUT=5");
    } else {
#ifdef Q_OS_WIN
        const QString driverName="ODBC Driver 18 for SQL Server";
        remote.setDatabaseName(QString("Driver={%1};Server=tcp:%2,%3;Database=%4;Encrypt=no;")
            .arg(driverName,config.value("host").toString())
            .arg(config.value("port").toInt())
            .arg(config.value("database").toString()));
#else
        const QString driverName="FreeTDS";
        remote.setDatabaseName(QString("Driver={%1};Server=%2;Port=%3;TDS_Version=7.4;Database=%4;")
            .arg(driverName,config.value("host").toString())
            .arg(config.value("port").toInt())
            .arg(config.value("database").toString()));
#endif
        remote.setConnectOptions("SQL_ATTR_LOGIN_TIMEOUT=5");
    }
    if(!remote.open()) {
        const auto failure=remote.lastError();const QString details=failure.nativeErrorCode()+" "+failure.databaseText();
        if(error) {
            if(details.contains("1045") || details.contains("1044") || details.contains("28000"))
                *error="数据库账号或密码错误，或账号没有访问该数据库的权限";
            else if(details.contains("1049")) *error="数据库不存在，请检查数据库名称";
            else if(details.contains("2002") || details.contains("2003") || details.contains("2005") || details.contains("08001"))
                *error="无法连接数据库，请检查地址、端口、数据库服务和网络";
            else *error="数据库连接失败，请检查连接配置、驱动和数据库访问权限";
        }
        remote={}; QSqlDatabase::removeDatabase(name);
        return false;
    }
    *out=remote;
    return true;
}
bool ensureRemoteTable(QSqlDatabase &remote,const QSqlRecord &localRecord,const QString &table,bool mysql,QString *error) {
    QStringList fields;
    for(int i=0;i<localRecord.count();++i)
        fields << quote(localRecord.fieldName(i),mysql)+" "+fieldType(localRecord.fieldName(i),mysql);
    const QString tableName=quote(table,mysql);
    QString sql=mysql ? "CREATE TABLE IF NOT EXISTS "+tableName+"("+fields.join(',')+")" :
        "IF OBJECT_ID(N'dbo."+table+"',N'U') IS NULL CREATE TABLE "+tableName+"("+fields.join(',')+")";
    QSqlQuery create(remote);
    if(!create.exec(sql)) { if(error) *error=create.lastError().text(); return false; }
    QSqlRecord existing=remote.record(table);
    if(existing.isEmpty()) return true;
    for(int i=0;i<localRecord.count();++i) {
        const QString name=localRecord.fieldName(i);
        if(existing.contains(name)) continue;
        QSqlQuery add(remote);
        if(!add.exec("ALTER TABLE "+tableName+" ADD "+quote(name,mysql)+" "+fieldType(name,mysql).section(" PRIMARY KEY",0,0))) {
            if(error) *error=add.lastError().text(); return false;
        }
    }
    return true;
}
bool copyTable(QSqlDatabase &local,QSqlDatabase &remote,const QString &table,bool mysql,
               const QString &watermark,bool full,QString *error) {
    QSqlQuery source(local);
    QString sourceSql="SELECT * FROM `"+table+"`";
    if(!full && !watermark.isEmpty())
        sourceSql+=" WHERE updated_at>=? OR deleted_at>=?";
    sourceSql+=" ORDER BY id";
    source.prepare(sourceSql);
    if(!full && !watermark.isEmpty()) { source.addBindValue(watermark); source.addBindValue(watermark); }
    if(!source.exec()) { if(error) *error=source.lastError().text(); return false; }
    const QSqlRecord columns=source.record();
    if(!ensureRemoteTable(remote,columns,table,mysql,error)) return false;
    QStringList names,marks,updates;
    for(int i=0;i<columns.count();++i) {
        const QString field=columns.fieldName(i);
        names << quote(field,mysql); marks << "?";
        if(field!="id") updates << quote(field,mysql)+(mysql ? "=VALUES("+quote(field,mysql)+")" : "=?");
    }
    const QString insertSql="INSERT INTO "+quote(table,mysql)+"("+names.join(',')+") VALUES("+marks.join(',')+")";
    while(source.next()) {
        if(!mysql) {
            QSqlQuery exists(remote);
            exists.prepare("SELECT id FROM "+quote(table,false)+" WHERE id=?");
            exists.addBindValue(source.value("id"));
            if(!exists.exec()) { if(error) *error=exists.lastError().text(); return false; }
            if(exists.next()) {
                QSqlQuery update(remote);
                update.prepare("UPDATE "+quote(table,false)+" SET "+updates.join(',')+" WHERE id=?");
                for(int i=0;i<columns.count();++i) if(columns.fieldName(i)!="id") update.addBindValue(source.value(i));
                update.addBindValue(source.value("id"));
                if(!update.exec()) { if(error) *error=update.lastError().text(); return false; }
                continue;
            }
        }
        QSqlQuery insert(remote);
        insert.prepare(insertSql+(mysql ? " ON DUPLICATE KEY UPDATE "+updates.join(',') : ""));
        for(int i=0;i<columns.count();++i) insert.addBindValue(source.value(i));
        if(!insert.exec()) { if(error) *error=insert.lastError().text(); return false; }
    }
    return true;
}
bool probeWritable(QSqlDatabase &remote,bool mysql,QString *error) {
    const QString name=uniqueName("_boer_sync_probe_");
    const QString table=quote(name,mysql);
    QSqlQuery query(remote);
    // Check DROP permission before creating anything on MySQL.
    if(mysql && !query.exec("DROP TABLE IF EXISTS "+table)) { if(error) *error=query.lastError().text(); return false; }
    if(!query.exec("CREATE TABLE "+table+"(id INT PRIMARY KEY,probe_value INT)")) { if(error) *error=query.lastError().text(); return false; }
    bool okay=query.exec("INSERT INTO "+table+"(id,probe_value) VALUES(1,1)") &&
        query.exec("UPDATE "+table+" SET probe_value=2 WHERE id=1");
    const QString failure=okay ? QString() : query.lastError().text();
    QSqlQuery cleanup(remote);
    if(!cleanup.exec("DROP TABLE "+table)) { if(error) *error=cleanup.lastError().text(); return false; }
    if(!okay && error) *error=failure;
    return okay;
}
}

QJsonObject ExternalDb::defaults() {
    return {{"dbType","mysql"},{"host","127.0.0.1"},{"port",3306},{"username",""},
        {"password",""},{"database",""},{"charset","utf8mb4"},{"syncIntervalMinutes",30},
        {"enabled",false},{"updatedAt",0}};
}

QJsonObject ExternalDb::normalize(QJsonObject config) {
    QJsonObject result=defaults();
    for(auto it=config.begin();it!=config.end();++it) result[it.key()]=it.value();
    QString type=result.value("dbType").toString().trimmed().toLower();
    if(type.isEmpty()) type="mysql";
    result["dbType"]=type;
    for(const QString &key:{"host","username","database","charset"})
        result[key]=result.value(key).toString().trimmed();
    if(result.value("port").toInt()<1) result["port"]=type=="mssql" ? 1433 : 3306;
    if(result.value("charset").toString().isEmpty()) result["charset"]="utf8mb4";
    if(result.value("syncIntervalMinutes").toInt()<1) result["syncIntervalMinutes"]=30;
    return result;
}

QByteArray ExternalDb::connectionFingerprint(const QJsonObject &config) {
    const QJsonObject normalized=normalize(config); QJsonObject connection;
    for(const QString &key:{"dbType","host","port","username","password","database","charset"}) connection[key]=normalized.value(key);
    return QCryptographicHash::hash(QJsonDocument(connection).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex();
}

QString ExternalDb::validate(const QJsonObject &config,bool credentialsRequired) {
    const QString type=config.value("dbType").toString();
    if(type!="mysql" && type!="mssql") return "数据库类型仅支持 mysql 或 mssql";
    const QString host=config.value("host").toString();
    if(credentialsRequired || !host.isEmpty()) {
        const QString invalid=validateHost(host); if(!invalid.isEmpty()) return invalid;
    }
    if(config.value("port").toInt()<1 || config.value("port").toInt()>65535) return "数据库端口不合法";
    if(credentialsRequired && config.value("username").toString().isEmpty()) return "登录名不能为空";
    if(credentialsRequired && config.value("database").toString().isEmpty()) return "数据库名不能为空";
    return {};
}

QString ExternalDb::validateHost(const QString &input) {
    const QString host=input.trimmed(); QHostAddress address;
    if(host.isEmpty() || host.size()>253) return "请输入有效的数据库IP或主机名";
    if(QRegularExpression("^[0-9.]+$").match(host).hasMatch()) {
        if(host.split('.').size()!=4 || !address.setAddress(host) || address.protocol()!=QAbstractSocket::IPv4Protocol)
            return "请输入有效的数据库IP或主机名";
        return {};
    }
    if(address.setAddress(host)) return {};
    QString domain=host; if(domain.endsWith('.')) domain.chop(1);
    for(const QString &label:domain.split('.'))
        if(!QRegularExpression("^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?$").match(label).hasMatch())
            return "请输入有效的数据库IP或主机名";
    return {};
}

bool ExternalDb::test(const QJsonObject &config,QString *error,bool *writable,QString *writeError) {
    if(writable) *writable=false;
    const QJsonObject normalized=normalize(config);
    const QString invalid=validate(normalized,true);
    if(!invalid.isEmpty()) { if(error) *error=invalid; return false; }
    const QString name=uniqueName("boer-external-test-");
    QSqlDatabase remote;
    if(!openRemote(normalized,name,&remote,error)) return false;
    bool okay=false;
    {
        QSqlQuery ping(remote);
        okay=ping.exec("SELECT 1");
        if(!okay && error) *error=ping.lastError().text();
    }
    if(okay && writable) *writable=probeWritable(remote,normalized.value("dbType").toString()=="mysql",writeError);
    remote.close(); remote={}; QSqlDatabase::removeDatabase(name);
    return okay;
}

bool ExternalDb::sync(const QJsonObject &source,const QJsonObject &config,bool full,QString *error) {
    QMutexLocker lock(&syncMutex);
    const QString startedUtc=QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd HH:mm:ss");
    const QJsonObject normalized=normalize(config);
    const QString invalid=validate(normalized,true);
    if(!invalid.isEmpty()) { if(error) *error=invalid; return false; }
    if(!normalized.value("enabled").toBool()) { if(error) *error="外部数据库连接未启用"; return false; }
    const QString localName=uniqueName("boer-sync-local-"),remoteName=uniqueName("boer-sync-remote-");
    const bool sourceMySql=source.value("type").toString()=="mysql";
    QSqlDatabase local=QSqlDatabase::addDatabase(sourceMySql ? "QMYSQL" : "QSQLITE",localName);
    if(sourceMySql) {
        local.setHostName(source.value("host").toString()); local.setPort(source.value("port").toInt());
        local.setUserName(source.value("username").toString()); local.setPassword(source.value("password").toString());
        local.setDatabaseName(source.value("database").toString());
    } else local.setDatabaseName(source.value("path").toString());
    if(!local.open()) {
        if(error) *error=local.lastError().text();
        local={}; QSqlDatabase::removeDatabase(localName); return false;
    }
    if(sourceMySql) {
        const QString charset=source.value("charset").toString("utf8mb4");
        if(!QRegularExpression("^[A-Za-z0-9_]+$").match(charset).hasMatch()) {
            if(error) *error="MySQL 字符集名称不合法";
            local.close(); local={}; QSqlDatabase::removeDatabase(localName); return false;
        }
        QSqlQuery setCharset(local);
        if(!setCharset.exec("SET NAMES "+charset)) {
            if(error) *error=setCharset.lastError().text();
            local.close(); local={}; QSqlDatabase::removeDatabase(localName); return false;
        }
    }
    QString watermark;
    if(!full) {
        QSqlQuery read(local);
        // The previous display timestamp was local time, not a reliable UTC cursor.
        // Missing the new key intentionally replays all rows on the first upgraded sync.
        read.prepare("SELECT value FROM server_configs WHERE `key`='external_db_last_sync_utc' AND deleted_at IS NULL");
        if(read.exec() && read.next()) watermark=read.value(0).toString();
        QSqlQuery target(local);
        target.prepare("SELECT value FROM server_configs WHERE `key`='external_db_last_sync_target' AND deleted_at IS NULL");
        if(!target.exec() || !target.next() || target.value(0).toByteArray()!=connectionFingerprint(normalized)) watermark.clear();
    }
    QSqlDatabase remote;
    if(!openRemote(normalized,remoteName,&remote,error)) {
        local.close(); local={}; QSqlDatabase::removeDatabase(localName); return false;
    }
    const bool mysql=normalized.value("dbType").toString()=="mysql";
    bool okay=true;
    for(const QString &table:syncTables) {
        if(!remote.transaction()) { okay=false; if(error) *error=remote.lastError().text(); break; }
        if(!copyTable(local,remote,table,mysql,watermark,full,error)) {
            remote.rollback(); okay=false; break;
        }
        if(!remote.commit()) { okay=false; if(error) *error=remote.lastError().text(); break; }
    }
    if(okay) {
        okay=local.transaction();
        if(!okay && error) *error=local.lastError().text();
        for(const auto &entry:{QPair<QString,QString>{"external_db_last_sync_utc",startedUtc},
            {"external_db_last_sync_target",QString::fromLatin1(connectionFingerprint(normalized))},
            {"external_db_last_sync_at",QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")}}) {
            if(!okay) break;
            QSqlQuery save(local);
            save.prepare(sourceMySql ?
                "INSERT INTO server_configs(`key`,value,`desc`) VALUES(?,?,'外部数据库同步时间') "
                "ON DUPLICATE KEY UPDATE value=VALUES(value),updated_at=CURRENT_TIMESTAMP" :
                "INSERT INTO server_configs(`key`,value,`desc`) VALUES(?,?,'外部数据库同步时间') "
                "ON CONFLICT(`key`) DO UPDATE SET value=excluded.value,updated_at=CURRENT_TIMESTAMP");
            save.addBindValue(entry.first); save.addBindValue(entry.second);
            okay=save.exec(); if(!okay && error) *error=save.lastError().text();
        }
        if(okay) { okay=local.commit(); if(!okay && error) *error=local.lastError().text(); }
        if(!okay) local.rollback();
    }
    remote.close(); remote={}; QSqlDatabase::removeDatabase(remoteName);
    local.close(); local={}; QSqlDatabase::removeDatabase(localName);
    return okay;
}
