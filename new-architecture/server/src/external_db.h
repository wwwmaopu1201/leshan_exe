#pragma once

#include <QJsonObject>
#include <QString>

class ExternalDb {
public:
    static QJsonObject defaults();
    static QJsonObject normalize(QJsonObject config);
    static QByteArray connectionFingerprint(const QJsonObject &config);
    static QString validateHost(const QString &host);
    static QString validate(const QJsonObject &config,bool credentialsRequired);
    static bool test(const QJsonObject &config,QString *error,bool *writable=nullptr,QString *writeError=nullptr);
    static bool sync(const QJsonObject &source,const QJsonObject &config,bool full,QString *error);
};
