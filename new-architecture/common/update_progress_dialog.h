#pragma once
#include <QDialog>
class QLabel;
class QProgressBar;
class QPushButton;
class UpdateProgressDialog : public QDialog {
public:
    explicit UpdateProgressDialog(const QString &product,const QString &version,QWidget *owner=nullptr);
    void setDownloadProgress(qint64 received,qint64 total);
    bool wasCanceled() const { return canceled_; }
    void reject() override;
private:
    void retranslate();
    QString product_,version_;
    QLabel *heading_,*description_,*status_,*amount_;
    QProgressBar *bar_;
    QPushButton *cancel_;
    qint64 received_=0,total_=-1;
    bool canceled_=false;
};
// App-language strings, independent of the OS/Qt translator language.
QString updateText(const char *chinese,const char *english);
void configureUpdateDialog(QDialog *dialog);
