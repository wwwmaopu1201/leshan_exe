#include "window_compatibility.h"
#include "ui_i18n.h"
#include "ui_controls.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QThread>
#include <QToolTip>
#include <QTimer>
#include <QMenu>
#include <QVBoxLayout>

namespace {
int failures=0;
void settle(int duration=25) {QElapsedTimer timer;timer.start();do{QApplication::processEvents();QThread::msleep(1);}while(timer.elapsed()<duration);}
QString rectangle(const QRect &r){return QString("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());}
QString pictureDir(){const QString path=qEnvironmentVariable("BOER_WINDOW_ARTIFACT_DIR");QDir().mkpath(path);return path;}
}
void recordWindowCompatibility(const QString &side,const QString &name,bool passed,const QString &detail) {
    const QString scale=qEnvironmentVariable("QT_SCALE_FACTOR","1");
    const QString profile=side+(QApplication::platformName()=="offscreen" ? QString() : " "+QApplication::platformName());
    QJsonObject item{{"row",side=="client" ? 345 : 482},{"name","窗口兼容 "+profile+" scale="+scale+" "+name},
        {"result",passed ? "PASS" : "FAIL"},{"detail",detail},
        {"time",QDateTime::currentDateTime().toString(Qt::ISODate)},
        {"scope","Qt实际控件几何/滚动/最大化恢复/弹窗；平台="+QApplication::platformName()+"，QT_SCALE_FACTOR="+scale+"；Qt缩放验证不代替Windows系统DPI验收"}};
    QFile file(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG"));if(file.open(QIODevice::Append)){file.write(QJsonDocument(item).toJson(QJsonDocument::Compact)+'\n');file.flush();}
    qInfo().noquote()<<item["name"].toString()<<item["result"].toString()<<detail;failures+=!passed;
}
void waitWindowCompatibilityLayout(int milliseconds){settle(milliseconds);}
void checkWindowNotification(QMainWindow &owner,const QString &side,const QString &objectName){
    auto *label=owner.findChild<QLabel*>(objectName);settle();
    const QString detail=label ? QString("geometry=%1 requiredHeight=%2 owner=%3x%4").arg(rectangle(label->geometry())).arg(label->heightForWidth(label->width())).arg(owner.width()).arg(owner.height()) : "missing label";
    recordWindowCompatibility(side,"长提示在窗口内完整换行",label && owner.rect().contains(label->geometry()) && label->height()>=label->heightForWidth(label->width()),detail);
    const QSize previous=owner.size();owner.resize(previous+QSize(90,45));settle();owner.resize(previous);settle();
    recordWindowCompatibility(side,"窗口变化后长提示仍在边界内",label && owner.rect().contains(label->geometry()) && label->height()>=label->heightForWidth(label->width()));
    if(label){label->grab().save(pictureDir()+"/"+side+"-long-notification.png");label->hide();}
}
void scheduleWindowDialogCheck(QMainWindow &owner,const QString &side,const QString &label) {
    QTimer::singleShot(50,&owner,[&owner,side,label]{
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        recordWindowCompatibility(side,label+" 弹窗位于可用屏幕",dialog && dialog->screen()->availableGeometry().contains(dialog->frameGeometry()),dialog ? rectangle(dialog->frameGeometry()) : "missing dialog");
        if(!dialog)return;
        bool heading=true,hasHeading=false;for(auto *title:dialog->findChildren<QLabel*>("dialogTitle")){hasHeading=true;heading&=!title->text().isEmpty() && title->width()>title->fontMetrics().averageCharWidth()*3;}
        recordWindowCompatibility(side,label+" 弹窗标题有可读宽度",hasHeading && heading);
        const QSize previous=owner.size();owner.resize(previous+QSize(90,45));
        QTimer::singleShot(50,dialog,[&owner,dialog,previous,side,label]{
            bool overlays=true,found=false;for(auto *overlay:owner.findChildren<QWidget*>("dialogOverlay",Qt::FindDirectChildrenOnly))if(overlay->isVisible()){found=true;overlays&=overlay->geometry()==owner.rect();}
            recordWindowCompatibility(side,label+" 主窗变化后遮罩覆盖全部区域",found && overlays);
            bool footer=true;for(auto *bar:dialog->findChildren<QWidget*>("dialogFooter"))for(auto *button:bar->findChildren<QPushButton*>())if(button->isVisible())footer&=dialog->rect().contains(QRect(button->mapTo(dialog,QPoint()),button->size()));
            recordWindowCompatibility(side,label+" 弹窗保存取消按钮可达",footer && dialog->screen()->availableGeometry().contains(dialog->frameGeometry()));
            dialog->grab().save(pictureDir()+"/"+side+"-dialog-"+label+".png");dialog->done(QDialog::Rejected);owner.resize(previous);
        });
    });
}
int runWindowCompatibility(QMainWindow &window,const QString &side,const QStringList &pages,
                           const std::function<void(const QString&)> &navigate,const std::function<void()> &dialogs) {
    failures=0;settle();const QRect screen=window.screen()->availableGeometry();
    recordWindowCompatibility(side,"首次显示完整位于可用屏幕",screen.contains(window.frameGeometry()),"screen="+rectangle(screen)+" frame="+rectangle(window.frameGeometry()));
    if(auto *scroll=window.findChild<QScrollArea*>("adaptiveWindowScroll")){scroll->horizontalScrollBar()->setValue(scroll->horizontalScrollBar()->maximum());scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());settle();const QPoint corner=scroll->widget()->mapTo(scroll->viewport(),scroll->widget()->rect().bottomRight());recordWindowCompatibility(side,"小屏滚动可达内容右下角",scroll->viewport()->rect().adjusted(-1,-1,1,1).contains(corner));scroll->horizontalScrollBar()->setValue(0);scroll->verticalScrollBar()->setValue(0);}
    for(bool english:{false,true}) {
        UiI18n::instance()->setEnglish(english);settle();
        for(const QSize requested:{QSize(1000,650),QSize(1120,700),QSize(1280,720),QSize(1366,768),QSize(1600,900),QSize(1920,1080)}) {
            if(side=="server" && requested.width()<1120)continue;
            window.resize(requested);settle();
            for(const auto &page:pages) {
                navigate(page);settle();QToolTip::hideText();QStringList errors;
                for(auto *area:window.findChildren<QScrollArea*>())if(area->isVisible() && area->objectName()!="adaptiveWindowScroll" && area->horizontalScrollBar()->maximum()>0)
                    errors<<QString("unexpected horizontal scroll %1 max=%2").arg(area->objectName()).arg(area->horizontalScrollBar()->maximum());
                for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && !button->isWindow() && button->parentWidget() && !button->parentWidget()->rect().adjusted(-1,-1,1,1).contains(button->geometry()))
                    errors<<QString("button %1 outside parent %2 / %3").arg(button->text(),rectangle(button->geometry()),rectangle(button->parentWidget()->rect()));
                const QString label=QString("%1 %2x%3 %4").arg(english ? "en" : "zh").arg(requested.width()).arg(requested.height()).arg(page);
                recordWindowCompatibility(side,label,errors.isEmpty(),errors.join("; "));
                if(!errors.isEmpty() || (english && requested.width()==1120))window.grab().save(pictureDir()+"/"+side+"-"+QString::number(requested.width())+"-"+QString::number(english)+"-"+QString(page).replace('/','-')+".png");
            }
        }
    }
    window.showMaximized();settle();recordWindowCompatibility(side,"最大化可用",window.isMaximized());
    window.showNormal();settle();recordWindowCompatibility(side,"最大化后恢复且屏幕内可用",!window.isMaximized() && window.screen()->availableGeometry().contains(window.frameGeometry()),rectangle(window.frameGeometry()));
    if(QApplication::platformName()=="offscreen" && (qEnvironmentVariable("QT_SCALE_FACTOR")=="1" || qEnvironmentVariable("QT_SCALE_FACTOR")=="2")){
        window.showFullScreen();settle(60);recordWindowCompatibility(side,"Qt全屏完整覆盖虚拟屏幕",window.isFullScreen() && window.size()==window.screen()->geometry().size());
        window.showNormal();settle(60);recordWindowCompatibility(side,"Qt全屏退出后恢复屏幕内窗口",!window.isFullScreen() && window.screen()->availableGeometry().contains(window.frameGeometry()));
    }
    dialogs();settle();
    QWidget edge(&window,Qt::Tool|Qt::FramelessWindowHint);auto *edgeLayout=new QVBoxLayout(&edge);
    auto *combo=new UiComboBox;combo->setFixedWidth(220);for(int i=0;i<25;++i)combo->addItem(QString("Long business option %1 ").arg(i)+QString(100,'W'));
    auto *range=new DateRangeWidget;edgeLayout->addWidget(combo);edgeLayout->addWidget(range);
    const QRect available=window.screen()->availableGeometry();edge.resize(qMin(280,available.width()-20),100);edge.move(available.right()-edge.width()-8,available.bottom()-edge.height()-8);edge.show();settle();
    static_cast<QComboBox*>(combo)->showPopup();settle();auto *popup=combo->view()->window();
    recordWindowCompatibility(side,"屏幕右下角长选项下拉框不越界",popup && available.contains(popup->frameGeometry()),popup ? rectangle(popup->frameGeometry()) : "missing popup");
    const auto last=combo->model()->index(24,0);combo->view()->scrollTo(last);settle();
    const QRect lastRect=combo->view()->visualRect(last);recordWindowCompatibility(side,"下拉框25项可滚动访问最后一项",lastRect.top()>=0 && lastRect.bottom()<combo->view()->viewport()->height());
    if(popup)popup->grab().save(pictureDir()+"/"+side+"-edge-combo.png");combo->hidePopup();
    QTimer::singleShot(40,&edge,[&]{auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());recordWindowCompatibility(side,"屏幕右下角日期弹层不越界",menu && available.contains(menu->frameGeometry()),menu ? rectangle(menu->frameGeometry()) : "missing calendar");if(menu){menu->grab().save(pictureDir()+"/"+side+"-edge-calendar.png");menu->close();}});
    auto *dateButton=range->findChild<QPushButton*>("datePart");if(dateButton){dateButton->click();settle(100);}else recordWindowCompatibility(side,"日期按钮可访问",false);
    edge.hide();
    return failures ? 1 : 0;
}
