#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

class TrialState {
public:
    static bool ensure(const QString &databasePath,QString *error);
    static bool ensureAt(const QString &path,const QString &machineHash,
                         const QDateTime &now,const QString &version,QString *error,
                         const QStringList &acceptedLegacyHashes={});
};
