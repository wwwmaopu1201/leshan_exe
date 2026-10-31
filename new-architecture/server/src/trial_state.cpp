#include "trial_state.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QSysInfo>
#include <QVersionNumber>
#include <limits>

namespace {
constexpr qint64 durationSeconds=99999999999999LL*86400LL;
constexpr qint64 rollbackLeeway=600;
constexpr int policyVersion=6;

QString machineHash() {
    QStringList parts;
#ifdef Q_OS_WIN
    QSettings registry("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography",QSettings::NativeFormat);
    const QString guid=registry.value("MachineGuid").toString().trimmed().toLower();
    if(!guid.isEmpty()) parts={"windows-machine-v2",guid};
    else parts={"windows",QSysInfo::machineHostName(),qEnvironmentVariable("USERNAME")};
#elif defined(Q_OS_MACOS)
    const QByteArray stableId=QSysInfo::machineUniqueId();
    if(stableId.isEmpty()) return {};
    return QString::fromLatin1(QCryptographicHash::hash(
        "darwin-machine-v2|"+stableId+"|",QCryptographicHash::Sha256).toHex());
#else
    parts={QSysInfo::kernelType(),QSysInfo::machineHostName(),qEnvironmentVariable("USER"),QDir::homePath()};
#endif
    QByteArray input;
    for(const QString &part:parts) input+=part.toUtf8()+"|";
    return QString::fromLatin1(QCryptographicHash::hash(input,QCryptographicHash::Sha256).toHex());
}
#ifdef Q_OS_MACOS
QString legacyMacHash(const QString &host) {
    const QByteArray input=("darwin|"+host+"|"+qEnvironmentVariable("USER")+"|"+QDir::homePath()+"|").toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(input,QCryptographicHash::Sha256).toHex());
}
QStringList acceptedLegacyMacHashes() {
    QStringList hosts={QSysInfo::machineHostName()};
    QProcess process;
    process.start("/usr/sbin/scutil",{"--get","LocalHostName"});
    if(process.waitForFinished(1500) && process.exitStatus()==QProcess::NormalExit &&
       process.exitCode()==0) {
        const QString local=QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if(!local.isEmpty()) hosts << local << local+".local";
    }
    hosts.removeAll(""); hosts.removeDuplicates();
    QStringList hashes; for(const QString &host:hosts) hashes << legacyMacHash(host);
    return hashes;
}
#endif
bool skipTrial() {
    const QString value=qEnvironmentVariable("BOERLAN_SKIP_TRIAL").trimmed().toLower();
    return QStringList{"1","true","yes","on"}.contains(value) ||
        qEnvironmentVariable("APP_ENV").trimmed().compare("development",Qt::CaseInsensitive)==0;
}
}

bool TrialState::ensure(const QString &databasePath,QString *error) {
    if(skipTrial()) return true;
    const QString path=QDir(QFileInfo(databasePath).absolutePath()).filePath("server-trial-state.json");
#ifdef Q_OS_MACOS
    return ensureAt(path,machineHash(),QDateTime::currentDateTimeUtc(),"qt-0.1.0",error,
                    acceptedLegacyMacHashes());
#else
    return ensureAt(path,machineHash(),QDateTime::currentDateTimeUtc(),"qt-0.1.0",error);
#endif
}

bool TrialState::ensureAt(const QString &path,const QString &machineHash,const QDateTime &now,
                          const QString &version,QString *error,
                          const QStringList &acceptedLegacyHashes) {
    if(machineHash.isEmpty()) { if(error) *error="无法获取设备身份"; return false; }
    if(!now.isValid()) { if(error) *error="系统时间无效"; return false; }
    QJsonObject state;
    const qint64 nowSeconds=now.toSecsSinceEpoch();
    QFile file(path);
    if(file.exists()) {
        if(!file.open(QIODevice::ReadOnly)) { if(error) *error="读取试用状态失败"; return false; }
        QJsonParseError parseError;
        const QJsonDocument parsed=QJsonDocument::fromJson(file.readAll(),&parseError);
        // Windows cannot replace the state file while its read handle is open.
        // Release it before QSaveFile commits the updated launch state below.
        file.close();
        if(parseError.error!=QJsonParseError::NoError || !parsed.isObject()) {
            if(error) *error="解析试用状态失败"; return false;
        }
        state=parsed.object();
        const QString bound=state.value("machineHash").toString();
        if(!bound.isEmpty() && bound!=machineHash && !acceptedLegacyHashes.contains(bound)) {
            if(error) *error="试用授权已绑定到其他设备，无法继续使用"; return false;
        }
    }
    const QVersionNumber current=QVersionNumber::fromString(version.mid(version.startsWith("qt-") ? 3 : 0));
    const QString storedVersion=state.value("appVersion").toString();
    const QVersionNumber previous=QVersionNumber::fromString(storedVersion.mid(storedVersion.startsWith("qt-") ? 3 : 0));
    const bool reset=state.isEmpty() || state.value("policyVersion").toInt()<policyVersion ||
        (!previous.isNull() && !current.isNull() && QVersionNumber::compare(current,previous)>0);
    if(reset) {
        state={{"machineHash",machineHash},{"firstSeenAt",nowSeconds},{"lastSeenAt",nowSeconds},
            {"launchCount",0},{"policyVersion",policyVersion},{"appVersion",version}};
    } else {
        const qint64 last=state.value("lastSeenAt").toInteger();
        const qint64 first=state.value("firstSeenAt").toInteger();
        if(nowSeconds<last-rollbackLeeway) {
            if(error) *error="检测到系统时间被回拨，试用已失效"; return false;
        }
        if(first>0 && first<=std::numeric_limits<qint64>::max()-durationSeconds &&
           nowSeconds>first+durationSeconds) {
            if(error) *error="试用已过期，请联系供应商"; return false;
        }
    }
    state["machineHash"]=machineHash;
    state["lastSeenAt"]=nowSeconds;
    state["launchCount"]=state.value("launchCount").toInteger()+1;
    state["policyVersion"]=policyVersion;
    if(!QDir().mkpath(QFileInfo(path).absolutePath())) { if(error) *error="创建试用状态目录失败"; return false; }
    QSaveFile output(path);
    const QByteArray bytes=QJsonDocument(state).toJson(QJsonDocument::Compact);
    if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit()) {
        if(error) *error=QString("写入试用状态失败：%1\n%2").arg(path,output.errorString()); return false;
    }
    return true;
}
