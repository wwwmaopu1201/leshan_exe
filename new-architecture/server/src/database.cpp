#include "database.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

namespace {
QString databaseFailure(const QSqlError &error,const QSqlDatabase &connection) {
    if(connection.driverName()=="QSQLITE") {
        bool valid=false;const int code=error.nativeErrorCode().toInt(&valid);
        if(valid && (code&255)==13)return "数据库存储空间不足，请释放空间后重试";
        if(valid && (code&255)==14)return "数据库无法打开，请检查路径、目录权限和可用空间";
        if(valid && (code&255)==8)return "数据库为只读，请检查目录权限";
    }
    return error.text();
}
const QStringList tables = {"groups", "roles", "users", "operators", "devices",
    "employees", "patterns", "download_tasks", "device_pattern_files", "upload_tasks",
    "production_records", "alarm_records", "salary_records", "device_runtime_sessions",
    "employee_devices", "login_logs",
    "server_configs", "debug_logs", "device_type_catalogs", "electric_control_type_catalogs",
    "pattern_type_catalogs", "order_no_catalogs"};

QVariant valueOf(const QJsonValue &v) {
    if (v.isNull() || v.isUndefined()) return QVariant();
    if (v.isArray() || v.isObject()) return QString::fromUtf8(QJsonDocument(v.isArray() ? QJsonDocument(v.toArray()) : QJsonDocument(v.toObject())).toJson(QJsonDocument::Compact));
    return v.toVariant();
}
QString mysqlDefinition(const QString &sqlite) {
    QString sql=sqlite;
    sql.replace("id INTEGER PRIMARY KEY","id BIGINT PRIMARY KEY AUTO_INCREMENT");
    sql.replace(QRegularExpression("\\bINTEGER\\b"),"BIGINT");
    sql.replace(QRegularExpression("\\bREAL\\b"),"DOUBLE");
    const QRegularExpression field(R"((`?[a-z_]+`?) TEXT( UNIQUE)?( NOT NULL)?( DEFAULT (?:'[^']*'|CURRENT_TIMESTAMP))?)");
    static const QSet<QString> indexed={"username","name","code","key","value","source_key"};
    QString result; qsizetype offset=0;
    auto match=field.globalMatch(sql);
    while(match.hasNext()) {
        const auto found=match.next(); const QString identifier=found.captured(1);
        QString column=identifier; column.remove('`');
        result+=sql.mid(offset,found.capturedStart()-offset);
        if(column=="value" && sql.contains("server_configs(")) {
            result+=identifier+" LONGTEXT"+found.captured(3);
        } else if(indexed.contains(column)) {
            result+=identifier+" VARCHAR(255)"+found.captured(2)+found.captured(3);
            if(column=="source_key") result+=" DEFAULT NULL";
            else result+=found.captured(4);
        } else if(column=="permissions" || column=="group_ids") {
            result+=identifier+" VARCHAR(4096)"+found.captured(2)+found.captured(3)+found.captured(4);
        } else if(column.endsWith("_at") || column.endsWith("_time") ||
                  column=="record_date" || column=="last_online") {
            result+=identifier+" DATETIME"+found.captured(2)+found.captured(3)+found.captured(4);
        } else if(!found.captured(4).isEmpty() &&
                  column!="file_path" && column!="file_snapshot" && column!="message") {
            result+=identifier+" VARCHAR(255)"+found.captured(3)+found.captured(4);
        } else result+=identifier+" LONGTEXT"+found.captured(3);
        offset=found.capturedEnd();
    }
    result+=sql.mid(offset);
    result.replace("CREATE TABLE IF NOT EXISTS groups(","CREATE TABLE IF NOT EXISTS `groups`(");
    result.replace(QRegularExpression(R"(\bkey VARCHAR\(255\))"),"`key` VARCHAR(255)");
    return result;
}
}

bool Database::validTable(const QString &table) { return tables.contains(table); }

QString Database::column(const QString &key) {
    QString result;
    for (QChar c : key) {
        if (c.isUpper()) result += '_';
        result += c.toLower();
    }
    return result;
}

bool Database::execute(const QString &sql, const QVariantList &args, QString *error) const {
    QSqlQuery query(db_);
    if (!query.prepare(sql)) { if (error) *error = databaseFailure(query.lastError(),db_); return false; }
    for (const auto &arg : args) query.addBindValue(arg);
    if (!query.exec()) { if (error) *error = databaseFailure(query.lastError(),db_); return false; }
    return true;
}

bool Database::open(const QString &path, QString *error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    dataDirectory_=QFileInfo(path).absolutePath(); mysql_=false;
    mysqlPassword_.clear();
    mysqlCharset_.clear();
    db_ = QSqlDatabase::addDatabase("QSQLITE", "boer-qt");
    db_.setDatabaseName(path);
    if (!db_.open()) { if (error) *error = databaseFailure(db_.lastError(),db_); return false; }
    return initializeSchema(error);
}

bool Database::openMySql(const QString &host,int port,const QString &user,const QString &password,
                         const QString &name,const QString &charset,const QString &storageDirectory,QString *error) {
    if(host.trimmed().isEmpty() || port<1 || port>65535 || user.trimmed().isEmpty() ||
       name.trimmed().isEmpty()) {
        if(error) *error="MySQL 主库地址、端口、账号和数据库名不能为空";
        return false;
    }
    if(!QSqlDatabase::isDriverAvailable("QMYSQL")) {
        if(error) *error="缺少 Qt QMYSQL 驱动插件";
        return false;
    }
    dataDirectory_=storageDirectory; mysql_=true; mysqlPassword_=password; mysqlCharset_=charset;
    if(!QDir().mkpath(dataDirectory_)) { if(error) *error="无法创建数据目录"; return false; }
    db_=QSqlDatabase::addDatabase("QMYSQL","boer-qt");
    db_.setHostName(host); db_.setPort(port); db_.setUserName(user);
    db_.setPassword(password); db_.setDatabaseName(name);
    db_.setConnectOptions("MYSQL_OPT_CONNECT_TIMEOUT=5");
    if(!db_.open()) { if(error) *error=databaseFailure(db_.lastError(),db_); return false; }
    QSqlQuery charsetQuery(db_);
    if(!QRegularExpression("^[A-Za-z0-9_]+$").match(charset).hasMatch()) {
        if(error) *error="MySQL 字符集名称不合法"; return false;
    }
    if(!charsetQuery.exec("SET NAMES "+charset)) { if(error) *error=charsetQuery.lastError().text(); return false; }
    // Match SQLite CURRENT_TIMESTAMP and timestamp-based queue/sync cursors.
    // Set only this connection, independently of the database host's timezone.
    if(!charsetQuery.exec("SET time_zone = '+00:00'")) { if(error)*error=charsetQuery.lastError().text();return false; }
    return initializeSchema(error);
}

QJsonObject Database::connectionOptions() const {
    if(!mysql_) return {{"type","sqlite"},{"path",db_.databaseName()}};
    return {{"type","mysql"},{"host",db_.hostName()},{"port",db_.port()},
        {"username",db_.userName()},{"password",mysqlPassword_},{"database",db_.databaseName()},
        {"charset",mysqlCharset_}};
}

bool Database::initializeSchema(QString *error) {
    const QStringList schema = {
        "CREATE TABLE IF NOT EXISTS groups(id INTEGER PRIMARY KEY, name TEXT NOT NULL, parent_id INTEGER, sort_order INTEGER DEFAULT 0, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS roles(id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL, remark TEXT DEFAULT '', permissions TEXT NOT NULL DEFAULT '{}', parent_child_link INTEGER DEFAULT 1, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY, username TEXT UNIQUE NOT NULL, password TEXT NOT NULL, nickname TEXT DEFAULT '', email TEXT DEFAULT '', phone TEXT DEFAULT '', role TEXT DEFAULT 'user', avatar TEXT DEFAULT '', disabled INTEGER DEFAULT 0, permissions TEXT DEFAULT '{}', group_id INTEGER, group_ids TEXT DEFAULT '[]', created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS operators(id INTEGER PRIMARY KEY, username TEXT UNIQUE NOT NULL, password TEXT NOT NULL, nickname TEXT DEFAULT '', disabled INTEGER DEFAULT 0, group_id INTEGER, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS devices(id INTEGER PRIMARY KEY, code TEXT NOT NULL, name TEXT NOT NULL, initial_name TEXT DEFAULT '', type TEXT DEFAULT '', electric_control_type TEXT DEFAULT '', model_name TEXT DEFAULT '', identified_by TEXT DEFAULT '', employee_code TEXT DEFAULT '', employee_name TEXT DEFAULT '', mainboard_sn TEXT DEFAULT '', remark TEXT DEFAULT '', ip TEXT DEFAULT '', status TEXT DEFAULT 'offline', current_pattern_no INTEGER DEFAULT 0, current_pattern_name TEXT DEFAULT '', alarm_code TEXT DEFAULT '', current_speed INTEGER DEFAULT 0, max_speed_value INTEGER DEFAULT 0, production_total INTEGER DEFAULT 0, production_current INTEGER DEFAULT 0, bottom_thread_total INTEGER DEFAULT 0, bottom_thread_remain INTEGER DEFAULT 0, needle_count_value INTEGER DEFAULT 0, sewing_range_x INTEGER DEFAULT 0, sewing_range_y INTEGER DEFAULT 0, high_point_value INTEGER DEFAULT 0, low_point_value INTEGER DEFAULT 0, last_protocol_at TEXT, group_id INTEGER, sort_order INTEGER DEFAULT 0, last_online TEXT, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS employees(id INTEGER PRIMARY KEY, code TEXT UNIQUE NOT NULL, name TEXT NOT NULL, department TEXT DEFAULT '', position TEXT DEFAULT '', phone TEXT DEFAULT '', remark TEXT DEFAULT '', group_id INTEGER, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS patterns(id INTEGER PRIMARY KEY, name TEXT NOT NULL, pattern_type TEXT DEFAULT '', file_name TEXT NOT NULL, file_path TEXT DEFAULT '', file_size INTEGER DEFAULT 0, stitches INTEGER DEFAULT 0, colors INTEGER DEFAULT 0, width REAL DEFAULT 0, height REAL DEFAULT 0, unit_price REAL DEFAULT 0, order_no TEXT DEFAULT '', uploaded_by INTEGER DEFAULT 0, uploaded_at TEXT, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS download_tasks(id INTEGER PRIMARY KEY, pattern_id INTEGER, device_id INTEGER, status TEXT DEFAULT 'waiting', progress INTEGER DEFAULT 0, message TEXT DEFAULT '', operator_id INTEGER, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS device_pattern_files(id INTEGER PRIMARY KEY, device_id INTEGER, pattern_no INTEGER DEFAULT 0, file_name TEXT NOT NULL, pattern_type TEXT DEFAULT '', file_size INTEGER DEFAULT 0, stitches INTEGER DEFAULT 0, unit_price REAL DEFAULT 0, order_no TEXT DEFAULT '', file_path TEXT DEFAULT '', created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS upload_tasks(id INTEGER PRIMARY KEY, device_file_id INTEGER, pattern_id INTEGER, device_id INTEGER, status TEXT DEFAULT 'waiting', progress INTEGER DEFAULT 0, message TEXT DEFAULT '', operator_id INTEGER, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS production_records(id INTEGER PRIMARY KEY, device_id INTEGER, employee_id INTEGER, pattern_id INTEGER, pattern_no INTEGER, pattern_name TEXT DEFAULT '', pieces INTEGER DEFAULT 0, stitches INTEGER DEFAULT 0, thread_length REAL DEFAULT 0, running_time REAL DEFAULT 0, idle_time REAL DEFAULT 0, start_time TEXT, end_time TEXT, unit_price REAL DEFAULT 0, order_no TEXT DEFAULT '', record_date TEXT, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS alarm_records(id INTEGER PRIMARY KEY, device_id INTEGER, alarm_type TEXT DEFAULT '', alarm_code TEXT DEFAULT '', description TEXT DEFAULT '', duration INTEGER DEFAULT 0, status TEXT DEFAULT 'pending', start_time TEXT, end_time TEXT, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS salary_records(id INTEGER PRIMARY KEY, employee_id INTEGER, device_id INTEGER, pattern_id INTEGER, pattern_name TEXT DEFAULT '', order_no TEXT DEFAULT '', pieces INTEGER DEFAULT 0, unit_price REAL DEFAULT 0, salary REAL DEFAULT 0, bonus REAL DEFAULT 0, total_amount REAL DEFAULT 0, record_date TEXT, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS device_runtime_sessions(id INTEGER PRIMARY KEY, device_id INTEGER NOT NULL, started_at TEXT NOT NULL, last_seen_at TEXT, ended_at TEXT, duration_seconds INTEGER DEFAULT 0, end_reason TEXT DEFAULT '', created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS employee_devices(id INTEGER PRIMARY KEY, employee_id INTEGER NOT NULL, device_id INTEGER NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS login_logs(id INTEGER PRIMARY KEY, user_id INTEGER DEFAULT 0, username TEXT DEFAULT '', ip TEXT DEFAULT '', device TEXT DEFAULT '', client_version TEXT DEFAULT '', status TEXT DEFAULT '', login_time TEXT DEFAULT CURRENT_TIMESTAMP)",
        "CREATE TABLE IF NOT EXISTS server_configs(id INTEGER PRIMARY KEY, key TEXT UNIQUE NOT NULL, value TEXT DEFAULT '', `desc` TEXT DEFAULT '', created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS debug_logs(id INTEGER PRIMARY KEY, level TEXT DEFAULT 'info', message TEXT DEFAULT '', source TEXT DEFAULT '', created_at TEXT DEFAULT CURRENT_TIMESTAMP)",
        "CREATE TABLE IF NOT EXISTS device_type_catalogs(id INTEGER PRIMARY KEY, value TEXT UNIQUE NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS electric_control_type_catalogs(id INTEGER PRIMARY KEY, value TEXT UNIQUE NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS pattern_type_catalogs(id INTEGER PRIMARY KEY, value TEXT UNIQUE NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)",
        "CREATE TABLE IF NOT EXISTS order_no_catalogs(id INTEGER PRIMARY KEY, value TEXT UNIQUE NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, deleted_at TEXT)"
    };
    for (const QString &sql : schema) if (!execute(mysql_ ? mysqlDefinition(sql) : sql, {}, error)) return false;
    const QList<QPair<QString,QString>> additions={
        {"upload_tasks","conflict_mode TEXT DEFAULT ''"},
        {"upload_tasks","target_name TEXT DEFAULT ''"},
        {"upload_tasks","file_snapshot TEXT DEFAULT ''"},
        {"production_records","protocol_user_id TEXT DEFAULT ''"},
        {"production_records","start_needle INTEGER DEFAULT 0"},
        {"production_records","end_needle INTEGER DEFAULT 0"},
        {"production_records","stop_reason INTEGER DEFAULT 0"},
        {"production_records","source_key TEXT DEFAULT ''"},
        {"salary_records","source_production_id INTEGER"}
    };
    for(const auto &addition:additions) {
        const QString table=addition.first, definition=addition.second;
        const QString field=definition.section(' ',0,0);
        bool found=false;
        if(mysql_) found=db_.record(table).contains(field);
        else {
            QSqlQuery columns(db_);
            if(!columns.exec("PRAGMA table_info("+table+")")) { if(error) *error=columns.lastError().text(); return false; }
            while(columns.next()) if(columns.value(1).toString()==field) { found=true; break; }
        }
        if(!found && !execute("ALTER TABLE "+table+" ADD COLUMN "+
            (mysql_ ? mysqlDefinition(definition) : definition),{},error)) return false;
    }
    const QStringList indexes={
        "CREATE INDEX IF NOT EXISTS idx_runtime_device_started ON device_runtime_sessions(device_id,started_at)",
        "CREATE INDEX IF NOT EXISTS idx_employee_devices_device ON employee_devices(device_id)",
        "CREATE INDEX IF NOT EXISTS idx_production_device_date ON production_records(device_id,record_date)",
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_production_source_key ON production_records(source_key) WHERE source_key IS NOT NULL AND source_key<>''",
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_salary_production ON salary_records(source_production_id) WHERE source_production_id IS NOT NULL"
    };
    for(QString sql:indexes) {
        if(mysql_) {
            const QRegularExpression indexName(R"(\bEXISTS\s+(\w+)\s+ON\b)");
            const QString name=indexName.match(sql).captured(1);
            const QString table=sql.section(" ON ",1,1).section('(',0,0).trimmed();
            QSqlQuery found(db_);
            found.prepare("SELECT COUNT(*) FROM information_schema.statistics "
                          "WHERE table_schema=DATABASE() AND table_name=? AND index_name=?");
            found.addBindValue(table); found.addBindValue(name);
            if(!found.exec() || !found.next()) { if(error) *error=found.lastError().text(); return false; }
            if(found.value(0).toInt()>0) continue;
            sql.replace(" IF NOT EXISTS","");
            sql=sql.section(" WHERE ",0,0);
        }
        if(!execute(sql,{},error)) return false;
    }
    if (count("groups") == 0) execute("INSERT INTO `groups`(name,sort_order) VALUES('总分组',1)", {});
    if (count("roles") == 0) {
        const QString p = R"({"home":true,"dashboard":true,"deviceManagement":true,"fileManagement":true,"statistics":true,"employeeManagement":true,"remoteMonitoring":true})";
        execute("INSERT INTO roles(name,remark,permissions) VALUES('admin','系统管理员',?)", {p});
        execute("INSERT INTO roles(name,remark,permissions) VALUES('user','普通用户',?)", {p});
    }
    if (count("users") == 0) execute("INSERT INTO users(username,password,nickname,role,permissions,group_id) VALUES('admin',?,'管理员','admin',(SELECT permissions FROM roles WHERE name='admin'),1)", {hashPassword("admin123")});
    return true;
}

QJsonObject Database::recordObject(const QSqlRecord &record) {
    QJsonObject object;
    for (int i=0; i<record.count(); ++i) {
        const QString key = record.fieldName(i);
        if (key == "password" || key == "deleted_at") continue;
        QString camel;
        bool upper = false;
        for (const QChar c : key) {
            if (c == '_') { upper = true; continue; }
            camel += upper ? c.toUpper() : c;
            upper = false;
        }
        if (record.value(i).isNull()) object[camel] = QJsonValue(QJsonValue::Null);
        else if (key == "disabled" || key == "parent_child_link") object[camel] = record.value(i).toBool();
        else if(record.value(i).metaType().id()==QMetaType::QDateTime)
            object[camel]=record.value(i).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
        else object[camel] = QJsonValue::fromVariant(record.value(i));
    }
    if (object.contains("createdAt")) object["CreatedAt"] = object.value("createdAt");
    if (object.contains("updatedAt")) object["UpdatedAt"] = object.value("updatedAt");
    if (object.contains("id")) object["ID"] = object.value("id");
    if (object.contains("createdAt")) object["createTime"] = object.value("createdAt");
    if (object.contains("modelName")) object["model"] = object.value("modelName");
    return object;
}

QJsonArray Database::rows(const QString &table, const QString &where, const QVariantList &args,
                          const QString &order, int limit) const {
    return selectRows(table,where,args,order,qBound(1,limit,10000));
}

QJsonArray Database::rowsAll(const QString &table,const QString &where,const QVariantList &args,const QString &order) const {
    return selectRows(table,where,args,order,0);
}

QJsonArray Database::selectRows(const QString &table,const QString &where,const QVariantList &args,const QString &order,int limit) const {
    QJsonArray result;
    if (!validTable(table)) return result;
    QSqlQuery query(db_);
    QString sql = "SELECT * FROM `" + table + "`";
    if (table != "login_logs" && table != "debug_logs") sql += " WHERE deleted_at IS NULL" + (where.isEmpty() ? "" : " AND (" + where + ")");
    else if (!where.isEmpty()) sql += " WHERE " + where;
    sql += " ORDER BY " + order;
    if(limit>0)sql+=" LIMIT ?";
    if (!query.prepare(sql)) return result;
    for (auto &arg : args) query.addBindValue(arg);
    if(limit>0)query.addBindValue(limit);
    if (!query.exec()) return result;
    while (query.next()) result.append(recordObject(query.record()));
    return result;
}

QJsonObject Database::row(const QString &table, qint64 id) const {
    QJsonArray found = rows(table, "id = ?", {id}, "id", 1);
    return found.isEmpty() ? QJsonObject{} : found.first().toObject();
}

qint64 Database::count(const QString &table, const QString &where, const QVariantList &args) const {
    if (!validTable(table)) return 0;
    QString sql = "SELECT COUNT(*) FROM `" + table + "`";
    if (table != "login_logs" && table != "debug_logs") sql += " WHERE deleted_at IS NULL" + (where.isEmpty() ? "" : " AND (" + where + ")");
    else if (!where.isEmpty()) sql += " WHERE " + where;
    QSqlQuery query(db_);
    if (!query.prepare(sql)) return 0;
    for (auto &arg : args) query.addBindValue(arg);
    return query.exec() && query.next() ? query.value(0).toLongLong() : 0;
}

bool Database::normalizeGroup(QJsonObject *body,qint64 existingId,QString *error) const {
    auto reject=[error](const QString &message){if(error)*error=message;return false;};
    const auto existing=existingId>0 ? row("groups",existingId) : QJsonObject{};
    if(existingId>0 && existing.isEmpty())return reject("分组不存在");
    const QString name=body->value("name").toString(existing.value("name").toString()).trimmed();
    if(name.isEmpty() || name.size()>50)return reject("分组名称不能为空且不能超过50个字符");
    const QJsonValue parent=body->contains("parentId") ? body->value("parentId") : existingId>0 ? existing.value("parentId") : QJsonValue(1);
    if(!parent.isNull() && (!parent.isDouble() || parent.toInteger()<1 || row("groups",parent.toInteger()).isEmpty()))return reject("父分组不存在");
    if(existingId>0 && parent.toInteger()!=existing.value("parentId").toInteger())return reject("分组不支持修改父分组");
    if(count("groups","COALESCE(parent_id,0)=? AND name=? AND id<>?",{parent.toInteger(),name,existingId})>0)return reject("同级分组名称已存在");
    if(body->contains("name") || existingId==0)(*body)["name"]=name;
    if(existingId==0)(*body)["parentId"]=parent;
    return true;
}

bool Database::employeePhoneAvailable(const QString &phone,qint64 existingId) const {
    QSqlQuery query(db_);query.prepare("SELECT id,phone FROM employees WHERE deleted_at IS NULL AND id<>?");query.addBindValue(existingId);
    if(!query.exec())return false;
    while(query.next()) {QString value=query.value(1).toString();value.remove(QRegularExpression("\\s+"));if(value==phone)return false;}
    return true;
}
bool Database::normalizeEmployeePhone(QJsonObject *body,qint64 existingId,QString *error) const {
    if(!body->contains("phone") && existingId>0)return true;
    QString phone=body->value("phone").toString();phone.remove(QRegularExpression("\\s+"));
    if(!QRegularExpression("^1[3-9][0-9]{9}$").match(phone).hasMatch()){if(error)*error="手机号不能为空且格式需正确";return false;}
    if(!employeePhoneAvailable(phone,existingId)){if(error)*error="手机号已被其他员工使用";return false;}
    (*body)["phone"]=phone;return true;
}

bool Database::insert(const QString &table, const QJsonObject &body, qint64 *id, QString *error) {
    if (!validTable(table)) { if(error) *error="Unknown table"; return false; }
    QJsonObject normalized=body;if(table=="groups" && !normalizeGroup(&normalized,0,error))return false;
    if(table=="employees" && !normalizeEmployeePhone(&normalized,0,error))return false;
    if(table=="patterns" && normalized.contains("orderNo")) {
        const auto value=normalized.value("orderNo");const QString number=value.toString();
        if(!value.isString() || number.size()>100 || (!number.isEmpty() && !QRegularExpression("^[A-Za-z0-9]+$").match(number).hasMatch())){if(error)*error="订单编号只允许字母和数字，且不能超过100个字符";return false;}
    }
    QSqlRecord record = db_.record(table);
    QStringList columns, marks;
    QVariantList values;
    for (auto it=normalized.begin(); it!=normalized.end(); ++it) {
        const QString col = it.key()=="model" ? "model_name" : column(it.key());
        if (col == "id" || col == "created_at" || col == "updated_at" || col == "deleted_at" || !record.contains(col)) continue;
        columns << (col == "desc" ? "`desc`" : col); marks << "?"; values << valueOf(it.value());
    }
    QSqlQuery query(db_);
    if (!query.prepare("INSERT INTO `" + table + "`(" + columns.join(',') + ") VALUES(" + marks.join(',') + ")")) { if(error) *error=databaseFailure(query.lastError(),db_); return false; }
    for (const auto &v : values) query.addBindValue(v);
    if (!query.exec()) { if(error) *error=databaseFailure(query.lastError(),db_); return false; }
    if (id) *id = query.lastInsertId().toLongLong();
    return true;
}

bool Database::update(const QString &table, qint64 id, const QJsonObject &body, QString *error) {
    if (!validTable(table)) { if(error) *error="Unknown table"; return false; }
    QJsonObject normalized=body;if(table=="groups" && !normalizeGroup(&normalized,id,error))return false;
    if(table=="employees" && !normalizeEmployeePhone(&normalized,id,error))return false;
    if(table=="patterns" && normalized.contains("orderNo")) {
        const auto value=normalized.value("orderNo");const QString number=value.toString();
        if(!value.isString() || number.size()>100 || (!number.isEmpty() && !QRegularExpression("^[A-Za-z0-9]+$").match(number).hasMatch())){if(error)*error="订单编号只允许字母和数字，且不能超过100个字符";return false;}
    }
    QSqlRecord record = db_.record(table);
    QStringList pairs;
    QVariantList values;
    for (auto it=normalized.begin(); it!=normalized.end(); ++it) {
        const QString col = it.key()=="model" ? "model_name" : column(it.key());
        if (col == "id" || col == "created_at" || col == "updated_at" || col == "deleted_at" || !record.contains(col)) continue;
        pairs << (col == "desc" ? "`desc` = ?" : col + " = ?"); values << valueOf(it.value());
    }
    if (pairs.isEmpty()) return true;
    if (record.contains("updated_at")) pairs << "updated_at = CURRENT_TIMESTAMP";
    values << id;
    return execute("UPDATE `" + table + "` SET " + pairs.join(',') + " WHERE id = ? AND deleted_at IS NULL", values, error);
}

bool Database::remove(const QString &table, qint64 id, QString *error) {
    if (!validTable(table) || table == "login_logs" || table == "debug_logs") { if(error) *error="Unknown table"; return false; }
    return execute("UPDATE `" + table + "` SET deleted_at = CURRENT_TIMESTAMP,updated_at = CURRENT_TIMESTAMP WHERE id = ?", {id}, error);
}

bool Database::removeGroup(qint64 id,QString *error) {
    auto reject=[error](const QString &message) { if(error) *error=message; return false; };
    if(id<=1) return reject("不能删除总分组");
    if(row("groups",id).isEmpty()) return reject("分组不存在");
    if(count("groups","parent_id=?",{id}) || count("users","group_id=?",{id}) ||
       count("operators","group_id=?",{id}) || count("employees","group_id=?",{id}))
        return reject("分组仍包含子分组、账号或员工");
    if(!db_.transaction()) return reject(databaseFailure(db_.lastError(),db_));
    if(!execute("UPDATE devices SET group_id=NULL,sort_order=0,updated_at=CURRENT_TIMESTAMP WHERE group_id=? AND deleted_at IS NULL",{id},error) ||
       !remove("groups",id,error)) { db_.rollback(); return false; }
    if(!db_.commit()) { db_.rollback(); return reject(databaseFailure(db_.lastError(),db_)); }
    return true;
}

bool Database::moveDevices(const QList<qint64> &ids,const QJsonValue &group,QString *error) {
    auto reject=[error](const QString &message) { if(error) *error=message; return false; };
    if(ids.isEmpty()) return reject("请选择记录");
    if(!group.isNull() && !group.isUndefined() && row("groups",group.toInteger()).isEmpty()) return reject("目标分组不存在");
    QSet<qint64> selected; QSet<QString> names;
    for(qint64 id:ids) {
        const QJsonObject device=row("devices",id);
        if(id<1 || device.isEmpty()) return reject("记录不存在");
        if(selected.contains(id)) continue;
        selected.insert(id);
        const QString name=device.value("name").toString();
        if(names.contains(name)) return reject("同组设备名称已存在");
        names.insert(name);
    }
    for(const QString &name:names)
        for(const QJsonValue &value:rows("devices","name=? AND COALESCE(group_id,0)=?",{name,group.toInteger()}))
            if(!selected.contains(value.toObject().value("id").toInteger())) return reject("同组设备名称已存在");
    if(!db_.transaction()) return reject(databaseFailure(db_.lastError(),db_));
    for(qint64 id:selected) if(!update("devices",id,{{"groupId",group},{"sortOrder",0}},error)) { db_.rollback(); return false; }
    if(!db_.commit()) { db_.rollback(); return reject(databaseFailure(db_.lastError(),db_)); }
    return true;
}

QString Database::config(const QString &key, const QString &fallback) const {
    QSqlQuery q(db_); q.prepare("SELECT value FROM server_configs WHERE `key` = ? AND deleted_at IS NULL"); q.addBindValue(key);
    return q.exec() && q.next() ? q.value(0).toString() : fallback;
}

bool Database::setConfig(const QString &key, const QString &value) {
    return execute(mysql_ ?
        "INSERT INTO server_configs(`key`,value) VALUES(?,?) ON DUPLICATE KEY UPDATE value=VALUES(value),updated_at=CURRENT_TIMESTAMP" :
        "INSERT INTO server_configs(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value,updated_at=CURRENT_TIMESTAMP", {key,value});
}

QString Database::hashPassword(const QString &password) {
    QByteArray salt(16, 0);
    for (char &c : salt) c = char(QRandomGenerator::system()->generate() & 0xff);
    const QByteArray digest = QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(), salt, 120000, 32);
    return "pbkdf2-sha256$120000$" + QString::fromLatin1(salt.toHex()) + "$" + QString::fromLatin1(digest.toHex());
}

bool Database::checkPassword(const QString &password, const QString &stored) {
    const QStringList parts = stored.split('$');
    if (parts.size()!=4 || parts[0]!="pbkdf2-sha256") return false;
    bool ok=false; int rounds=parts[1].toInt(&ok);
    if (!ok || rounds<10000 || rounds>1000000) return false;
    const QByteArray salt=QByteArray::fromHex(parts[2].toLatin1()), expected=QByteArray::fromHex(parts[3].toLatin1());
    if (salt.size()!=16 || expected.size()!=32) return false;
    const QByteArray actual=QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,password.toUtf8(),salt,rounds,expected.size());
    uchar diff=0; for(int i=0;i<actual.size();++i) diff |= uchar(actual[i]^expected[i]);
    return diff==0;
}
