#include "update_progress_dialog.h"
#include "ui_dialogs.h"
#include "ui_i18n.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
QString updateText(const char *chinese,const char *english) {
    return QString::fromUtf8(UiI18n::instance() && UiI18n::instance()->english() ? english : chinese);
}
void configureUpdateDialog(QDialog *dialog) {
    dialog->setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
    dialog->setProperty("fixedModalPosition",true);
    dialog->setObjectName("editorDialog");
    UiModalDialog::attach(dialog,dialog->parentWidget());
}
UpdateProgressDialog::UpdateProgressDialog(const QString &product,const QString &version,QWidget *owner)
    :QDialog(owner),product_(product),version_(version) {
    configureUpdateDialog(this);
    resize(640,300);setMinimumSize(440,250);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(28,24,28,24);layout->setSpacing(16);
    heading_=new QLabel;heading_->setObjectName("updateProgressTitle");heading_->setStyleSheet("font-size:20px;font-weight:600;color:#303133;");layout->addWidget(heading_);
    description_=new QLabel;description_->setWordWrap(true);layout->addWidget(description_);
    status_=new QLabel;status_->setObjectName("updateProgressStatus");layout->addWidget(status_);
    bar_=new QProgressBar;bar_->setObjectName("updateProgressBar");bar_->setFixedHeight(18);bar_->setRange(0,0);bar_->setTextVisible(false);layout->addWidget(bar_);
    amount_=new QLabel;amount_->setObjectName("updateProgressAmount");layout->addWidget(amount_);
    layout->addStretch();auto *footer=new QHBoxLayout;footer->addStretch();
    cancel_=new QPushButton;cancel_->setObjectName("updateProgressCancel");cancel_->setMinimumSize(120,36);cancel_->setAutoDefault(false);footer->addWidget(cancel_);layout->addLayout(footer);
    connect(cancel_,&QPushButton::clicked,this,&UpdateProgressDialog::reject);
    setProperty("i18nSkip",true);for(auto *child:findChildren<QWidget*>())child->setProperty("i18nSkip",true);
    if(auto *i18n=UiI18n::instance())connect(i18n,&UiI18n::languageChanged,this,[this]{retranslate();});
    retranslate();
}
void UpdateProgressDialog::setDownloadProgress(qint64 received,qint64 total) {
    received_=qMax<qint64>(0,received);total_=total;
    if(total>0){bar_->setRange(0,100);bar_->setValue(qBound(0,int(100.0*received_/total),100));}
    else bar_->setRange(0,0);
    retranslate();
}
void UpdateProgressDialog::retranslate() {
    setWindowTitle(updateText("下载更新","Download update"));heading_->setText(windowTitle());
    description_->setText(updateText("正在下载%1新版本 %2，下载完成后将校验安装包。","Downloading %1 version %2. The installer will be verified after download.")
        .arg(product_=="client" ? updateText("客户端","Client") : updateText("服务端","Server"),version_));
    cancel_->setText(updateText("取消下载","Cancel download"));
    status_->setText(received_==0 ? updateText("正在连接更新服务器…","Connecting to the update server…") : updateText("正在下载更新，请稍候…","Downloading update, please wait…"));
    const auto megabytes=[](qint64 bytes){return QLocale().toString(bytes/1048576.0,'f',1)+" MB";};
    if(total_>0)amount_->setText(QString("%1%    %2 / %3").arg(qBound(0,int(100.0*received_/total_),100)).arg(megabytes(received_),megabytes(total_)));
    else amount_->setText(updateText("已下载 %1，正在获取文件大小…","Downloaded %1. Waiting for file size…").arg(megabytes(received_)));
}
void UpdateProgressDialog::reject(){canceled_=true;QDialog::reject();}
