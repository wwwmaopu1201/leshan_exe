#pragma once
#include <QObject>
#include <QJsonObject>
#include <QUrl>
class QNetworkAccessManager;
class QWidget;
class UpdateProgressDialog;
class OnlineUpdate : public QObject {
public:
    explicit OnlineUpdate(const QString &product, QObject *parent = nullptr,QNetworkAccessManager *network=nullptr,const QString &downloadDirectory={});
    void check();
    static QUrl defaultManifestUrl(const QString &product);
    static bool isNewer(const QString &candidate, const QString &current);
    static bool validRelease(const QJsonObject &release);
private:
    friend class OnlineUpdateUiTest;
    void showReady(const QString &path,QWidget *owner);
    void showError(const QString &message,QWidget *owner);
    void offer(const QJsonObject &release);
    void download(const QJsonObject &release,QWidget *parent);
    bool checking_=false,updating_=false;
    QString downloadDirectory_;
    QString product_;
    QUrl manifest_;
    QNetworkAccessManager *network_;
};
// Called once at GUI startup; disabled for command-line regression/test runs.
void startOnlineUpdate(const QString &product);
