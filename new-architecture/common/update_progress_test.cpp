#include "update_progress_dialog.h"
#include "ui_i18n.h"
#include <QApplication>
#include <QEventLoop>
#include <QDir>
#include <QScreen>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <cstdio>
static void settle(){QEventLoop loop;QTimer::singleShot(25,&loop,&QEventLoop::quit);loop.exec();}
int main(int argc,char **argv){
    QApplication app(argc,argv);UiI18n i18n(&app);int failures=0;
    auto check=[&](bool ok,const char *message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);++failures;}};
    QWidget owner;owner.resize(QSize(1000,700).boundedTo(app.primaryScreen()->availableGeometry().size()-QSize(160,160)));owner.move(40,40);owner.show();settle();
    for(const auto &product:{QString("client"),QString("server")}) {
        i18n.setEnglish(false);UpdateProgressDialog dialog(product,"0.1.4",&owner);dialog.show();settle();
        auto *cancel=dialog.findChild<QPushButton*>("updateProgressCancel");auto *bar=dialog.findChild<QProgressBar*>("updateProgressBar");auto *amount=dialog.findChild<QLabel*>("updateProgressAmount");
        check(dialog.width()>=qMin(600,owner.width()-16) && dialog.height()>=280,"larger download dialog");
        check(dialog.windowFlags().testFlag(Qt::FramelessWindowHint) && dialog.isModal(),"frameless modal");
        check(cancel->text()=="取消下载" && dialog.windowTitle()=="下载更新","Chinese title and cancel button");
        check(bar->maximum()==0,"unknown content length uses busy progress");
        dialog.setDownloadProgress(1048576,4194304);check(bar->value()==25 && amount->text().contains("25%"),"downloaded bytes and percentage");
        const QString snapshots=qEnvironmentVariable("BOER_UPDATE_PROGRESS_SNAPSHOTS");
        if(!snapshots.isEmpty()){QDir().mkpath(snapshots);dialog.grab().save(snapshots+"/"+product+"-download-zh.png");}
        i18n.setEnglish(true);settle();
        if(!snapshots.isEmpty())dialog.grab().save(snapshots+"/"+product+"-download-en.png");
        check(cancel->text()=="Cancel download" && dialog.windowTitle()=="Download update","live English translation");
        check(bar->value()==25,"language switch preserves progress");
        i18n.setEnglish(false);settle();check(cancel->text()=="取消下载" && bar->value()==25,"switch back to Chinese");
        const QPoint centered=dialog.pos();dialog.move(centered+QPoint(90,60));settle();check(dialog.pos()==centered,"dialog cannot be moved away from center");
        owner.move(owner.pos()+QPoint(35,25));settle();const QRect ownerRect(owner.mapToGlobal(QPoint()),owner.size());
        check((dialog.geometry().center()-ownerRect.center()).manhattanLength()<=2,"dialog follows owner center");
        int cancellations=0;QObject::connect(&dialog,&QDialog::rejected,[&]{++cancellations;});cancel->click();settle();check(cancellations==1 && dialog.wasCanceled() && !dialog.isVisible(),"cancel download closes dialog exactly once");
    }
    return failures?1:0;
}
