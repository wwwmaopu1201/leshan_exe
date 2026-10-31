#pragma once

#include "api_client.h"
#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QNetworkAccessManager>

class QLabel;
class QStackedWidget;
class QTreeWidget;
class QWidget;
class ClientPage;
class QTimer;

class ClientWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ClientWindow(QWidget *parent=nullptr);
    ApiClient *api() { return &api_; }
    QJsonObject user() const { return user_; }
    void updateUser(const QJsonObject &user);
    bool hasPermission(const QString &key) const;
    void navigate(const QString &path);
    void navigateFiltered(const QString &path,const QString &key,const QString &value);
    void notify(const QString &message,bool error=false);
    void previewSession(const QJsonObject &user);
    void testSession(const QString &host,int port,const QJsonObject &user,const QString &token);
    QString currentPath() const { return currentPath_; }
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    void createLogin();
    void createShell();
    void rebuildNavigation();
    void enterSession(const QJsonObject &user,const QString &token,const QString &serverVersion,int deviceTcpPort=38400);
    void logout(bool notifyServer);
    void updateServerTag();
    QString firstAccessiblePath() const;
    bool allowedPath(const QString &path) const;
    ApiClient api_;
    QNetworkAccessManager avatarNetwork_;
    QJsonObject user_,permissions_;
    QStackedWidget *root_=nullptr,*pages_=nullptr;
    QWidget *login_=nullptr,*shell_=nullptr;
    QTreeWidget *navigation_=nullptr;
    QLabel *serverTag_=nullptr,*toast_=nullptr;
    QTimer *toastTimer_=nullptr;
    QHash<QString,ClientPage*> pageMap_;
    QString currentPath_;
    QString serverVersion_;
    int deviceTcpPort_=38400;
};
