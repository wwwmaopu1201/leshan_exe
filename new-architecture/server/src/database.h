#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

class Database {
public:
    bool open(const QString &path, QString *error);
    bool openMySql(const QString &host,int port,const QString &user,const QString &password,
                   const QString &name,const QString &charset,const QString &storageDirectory,QString *error);
    QSqlDatabase connection() const { return db_; }
    QString dataDirectory() const { return dataDirectory_; }
    bool isMySql() const { return mysql_; }
    QJsonObject connectionOptions() const;
    QJsonArray rows(const QString &table, const QString &where = {},
                    const QVariantList &args = {}, const QString &order = "id DESC",
                    int limit = 1000) const;
    // Full local models and trees explicitly opt out of rows()'s bounded limit.
    QJsonArray rowsAll(const QString &table,const QString &where={},const QVariantList &args={},const QString &order="id DESC") const;
    QJsonObject row(const QString &table, qint64 id) const;
    qint64 count(const QString &table, const QString &where = {},
                 const QVariantList &args = {}) const;
    bool insert(const QString &table, const QJsonObject &body, qint64 *id, QString *error);
    bool update(const QString &table, qint64 id, const QJsonObject &body, QString *error);
    bool remove(const QString &table, qint64 id, QString *error);
    bool employeePhoneAvailable(const QString &phone,qint64 existingId=0) const;
    bool removeGroup(qint64 id, QString *error);
    bool moveDevices(const QList<qint64> &ids,const QJsonValue &group,QString *error);
    QString config(const QString &key, const QString &fallback = {}) const;
    bool setConfig(const QString &key, const QString &value);
    static QString hashPassword(const QString &password);
    static bool checkPassword(const QString &password, const QString &stored);
    static QString column(const QString &key);
private:
    QJsonArray selectRows(const QString &table,const QString &where,const QVariantList &args,const QString &order,int limit) const;
    QSqlDatabase db_;
    QString dataDirectory_;
    QString mysqlPassword_;
    QString mysqlCharset_;
    bool mysql_=false;
    bool initializeSchema(QString *error);
    bool normalizeEmployeePhone(QJsonObject *body,qint64 existingId,QString *error) const;
    bool normalizeGroup(QJsonObject *body,qint64 existingId,QString *error) const;
    bool execute(const QString &sql, const QVariantList &args, QString *error = nullptr) const;
    static QJsonObject recordObject(const QSqlRecord &record);
    static bool validTable(const QString &table);
};
