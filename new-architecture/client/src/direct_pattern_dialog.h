#pragma once
#include <QDialog>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
class ApiClient;
class QNetworkReply;
class QProgressBar;
class QTableWidget;
class QLabel;
class QPushButton;
class DirectPatternDialog : public QDialog {
public:
    DirectPatternDialog(QWidget *parent,ApiClient *api,const QList<QPair<QString,QString>> &files,const QJsonArray &devices);
    ~DirectPatternDialog() override;
    void reject() override;
private:
    struct Item {QString path,name,deviceName,state="waiting",error;qint64 deviceId=0;};
    void next();void poll();void finish(const QString &error={});void render();void cancel();
    QPointer<ApiClient> api_;QPointer<QNetworkReply> upload_;
    QList<Item> items_;int index_=-1,progress_=0,pollErrors_=0;
    bool canceled_=false,done_=false,polling_=false;
    QString jobId_;QTimer timer_;QElapsedTimer elapsed_;
    QTableWidget *table_;QProgressBar *bar_;QLabel *title_,*hint_,*status_;QPushButton *close_;
};
