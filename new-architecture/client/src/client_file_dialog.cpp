#include "client_file_dialog.h"
#include "ui_i18n.h"
#include "ui_dialogs.h"

#include <QDialog>
#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>

namespace {
QString startingDirectory(const QString &requested) {
    if(!requested.isEmpty() && QFileInfo(requested).isDir()) return requested;
    QString downloads=QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if(downloads.isEmpty() || !QDir(downloads).exists()) downloads=QDir::homePath();
    return downloads;
}
QStringList choose(QWidget *parent,const QString &title,const QString &initial,
                   const QString &filter,QFileDialog::AcceptMode mode,QFileDialog::FileMode files) {
    QFileDialog dialog(parent,title,startingDirectory(initial),filter);
    dialog.setObjectName("clientFileDialog");
    dialog.setOption(QFileDialog::DontUseNativeDialog,true);
    dialog.setWindowModality(Qt::ApplicationModal);
    UiModalDialog::attach(&dialog,parent);
    dialog.setMinimumSize(790,520);
    dialog.setAcceptMode(mode);
    dialog.setFileMode(files);
    const bool english=UiI18n::instance() && UiI18n::instance()->english();
    dialog.setLabelText(QFileDialog::LookIn,english ? "Location" : "位置");
    dialog.setLabelText(QFileDialog::FileName,english ? "File name" : "文件名");
    dialog.setLabelText(QFileDialog::FileType,english ? "File type" : "文件类型");
    dialog.setLabelText(QFileDialog::Accept,english ?
        (mode==QFileDialog::AcceptSave ? "Save" : "Open") :
        (mode==QFileDialog::AcceptSave ? "保存" : "打开"));
    dialog.setLabelText(QFileDialog::Reject,english ? "Cancel" : "取消");
    if(!initial.isEmpty() && !QFileInfo(initial).isDir()) dialog.selectFile(initial);
    if(mode==QFileDialog::AcceptSave) {
        const auto suffix=QRegularExpression(R"(\*\.([A-Za-z0-9]+))").match(filter);
        if(suffix.hasMatch()) dialog.setDefaultSuffix(suffix.captured(1));
    }
    QTimer::singleShot(0,&dialog,[&dialog] {
        for(auto *buttons:dialog.findChildren<QDialogButtonBox*>())
            for(QAbstractButton *button:buttons->buttons())
                if(buttons->buttonRole(button)==QDialogButtonBox::AcceptRole) {
                    button->setObjectName("primaryButton");
                    button->style()->unpolish(button); button->style()->polish(button);
                }
        dialog.raise(); dialog.activateWindow();
    });
    if(dialog.exec()!=QDialog::Accepted) return {};
    return dialog.selectedFiles();
}
}

QString ClientFileDialog::getOpenFileName(QWidget *parent,const QString &title,
                                          const QString &initial,const QString &filter) {
    return choose(parent,title,initial,filter,QFileDialog::AcceptOpen,
                  QFileDialog::ExistingFile).value(0);
}
QStringList ClientFileDialog::getOpenFileNames(QWidget *parent,const QString &title,
                                               const QString &initial,const QString &filter) {
    return choose(parent,title,initial,filter,QFileDialog::AcceptOpen,
                  QFileDialog::ExistingFiles);
}
QString ClientFileDialog::getSaveFileName(QWidget *parent,const QString &title,
                                          const QString &suggested,const QString &filter) {
    return choose(parent,title,suggested,filter,QFileDialog::AcceptSave,
                  QFileDialog::AnyFile).value(0);
}
