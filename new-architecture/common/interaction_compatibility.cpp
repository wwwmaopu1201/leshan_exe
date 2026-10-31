#include "interaction_compatibility.h"
#include "ui_dialogs.h"
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace {int failures=0;}
void waitInteraction(int milliseconds){QElapsedTimer t;t.start();do{QApplication::processEvents();QThread::msleep(2);}while(t.elapsed()<milliseconds);}
void recordInteraction(const QString &side,const QString &name,bool passed,const QString &detail){
    const QString profile=side+" "+QApplication::platformName()+" scale="+qEnvironmentVariable("QT_SCALE_FACTOR","1");
    QJsonObject item{{"row",side=="client" ? 345 : 482},{"name","交互补测 "+profile+" "+name},
        {"result",passed ? "PASS" : "FAIL"},{"detail",detail},{"time",QDateTime::currentDateTime().toString(Qt::ISODate)},
        {"scope","实际Qt窗口/计时器/模态层级/键盘与QInputMethodEvent；"+profile+"；输入法事件注入不等于系统拼音候选窗验证"}};
    QFile file(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG"));if(file.open(QIODevice::Append)){file.write(QJsonDocument(item).toJson(QJsonDocument::Compact)+'\n');file.flush();}
    qInfo().noquote()<<item["name"].toString()<<item["result"].toString()<<detail;failures+=!passed;
}
void sendInteractionKey(QWidget *widget,int key,Qt::KeyboardModifiers modifiers){if(!widget)return;QKeyEvent press(QEvent::KeyPress,key,modifiers),release(QEvent::KeyRelease,key,modifiers);QApplication::sendEvent(widget,&press);QApplication::sendEvent(widget,&release);}
int runInteractionCompatibility(QMainWindow &window,const QString &side,
    const std::function<void(const QString&,bool)> &notify,const std::function<void()> &specific){
    failures=0;window.activateWindow();waitInteraction();
    auto toast=[&]{return window.findChild<QLabel*>(side=="client" ? "clientToast" : "actionToast");};
    notify("Sequence first success",false);waitInteraction(1300);notify("Sequence second error",true);waitInteraction(900);
    recordInteraction(side,"成功提示后出现错误提示，旧计时器不得提前隐藏新提示",toast() && toast()->isVisible() && toast()->text()=="Sequence second error");
    waitInteraction(2500);
    // Page refresh/layout work may delay delivery of an already due timer.
    // Keep the earlier visibility deadline strict; allow bounded dispatch latency at expiry.
    for(int retry=0;toast() && toast()->isVisible() && retry<20;++retry)waitInteraction(50);
    recordInteraction(side,"错误提示按自己的时长到期隐藏",toast() && !toast()->isVisible());
    notify("Sequence first error",true);waitInteraction(1700);notify("Sequence second success",false);waitInteraction(1600);
    recordInteraction(side,"错误提示后出现成功提示，新提示保留自己的显示周期",toast() && toast()->isVisible() && toast()->text()=="Sequence second success");
    waitInteraction(600);for(int retry=0;toast() && toast()->isVisible() && retry<20;++retry)waitInteraction(50);
    recordInteraction(side,"成功提示按自己的时长到期隐藏",toast() && !toast()->isVisible());
    for(int i=0;i<5;++i){notify(QString("Burst %1").arg(i),i%2);waitInteraction(60);}recordInteraction(side,"快速交替五条提示仅显示最后内容",toast() && toast()->isVisible() && toast()->text()=="Burst 4");
    if(toast())toast()->hide();
    QLineEdit *ownerEdit=nullptr;for(auto *line:window.findChildren<QLineEdit*>())if(line->isVisible() && line->isEnabled() && !line->isReadOnly()){ownerEdit=line;break;}
    const QString original=ownerEdit ? ownerEdit->text() : QString();if(ownerEdit){ownerEdit->setText("恢复输入 ABC 中文");ownerEdit->setFocus();}
    for(int cycle=0;cycle<3;++cycle){window.showMinimized();waitInteraction(200);const bool minimized=window.isMinimized();window.showNormal();window.activateWindow();waitInteraction(250);recordInteraction(side,QString("最小化恢复%1：状态、边界和输入保留").arg(cycle+1),minimized && !window.isMinimized() && window.isVisible() && window.screen()->availableGeometry().contains(window.frameGeometry()) && (!ownerEdit || ownerEdit->text()=="恢复输入 ABC 中文"));}
    if(ownerEdit){ownerEdit->setText(original);ownerEdit->setFocus();}waitInteraction();
    QDialog outer(&window);outer.setObjectName("interactionOuter");outer.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);UiModalDialog::attach(&outer,&window);outer.resize(540,330);
    auto *layout=new QVBoxLayout(&outer);auto *heading=new QLabel("Interaction test");heading->setObjectName("dialogTitle");layout->addWidget(heading);
    auto *first=new QLineEdit;first->setObjectName("interactionFirst");auto *second=new QLineEdit;second->setObjectName("interactionSecond");layout->addWidget(first);layout->addWidget(second);auto *save=new QPushButton("Confirm");save->setDefault(true);layout->addWidget(save);QObject::connect(save,&QPushButton::clicked,&outer,&QDialog::accept);
    QTimer::singleShot(80,&outer,[&]{
        first->setFocus();waitInteraction();QInputMethodEvent preedit("中文预编辑",{});QApplication::sendEvent(first,&preedit);recordInteraction(side,"中文预编辑不提前写入已提交文本",first->text().isEmpty());
        QInputMethodEvent commit;commit.setCommitString("中文姓名😀");QApplication::sendEvent(first,&commit);recordInteraction(side,"中文和emoji提交完整保留",first->text()=="中文姓名😀");
        QInputMethodEvent canceled("取消候选",{});QApplication::sendEvent(first,&canceled);QInputMethodEvent empty;QApplication::sendEvent(first,&empty);recordInteraction(side,"取消预编辑不污染已提交中文",first->text()=="中文姓名😀");
        sendInteractionKey(first,Qt::Key_Tab);waitInteraction();recordInteraction(side,"Tab进入下一输入框",QApplication::focusWidget()==second);
        sendInteractionKey(second,Qt::Key_Tab,Qt::ShiftModifier);waitInteraction();recordInteraction(side,"ShiftTab返回上一输入框",QApplication::focusWidget()==first);
        QTimer::singleShot(60,&outer,[&]{auto *inner=qobject_cast<QDialog*>(QApplication::activeModalWidget());recordInteraction(side,"嵌套确认框成为最上层模态",inner && inner!=&outer);if(QApplication::platformName()=="cocoa")recordInteraction(side,"嵌套期间两层owner保持原生阻挡",UiModalDialog::nativeOwnerBlocked(&window) && UiModalDialog::nativeOwnerBlocked(&outer));if(inner && inner!=&outer)sendInteractionKey(inner,Qt::Key_Escape);});
        const auto choice=UiMessageBox::question(&outer,"Nested confirmation","Cancel this inner dialog with Escape.");waitInteraction(100);
        recordInteraction(side,"Esc只取消内层确认框，外层输入保留",choice==UiMessageBox::No && outer.isVisible() && QApplication::activeModalWidget()==&outer && first->text()=="中文姓名😀");
        recordInteraction(side,"内层关闭后焦点回到原输入框",QApplication::focusWidget()==first,QApplication::focusWidget() ? QApplication::focusWidget()->objectName() : "null focus");
        if(QApplication::platformName()=="cocoa")recordInteraction(side,"内层关闭仅解除外层owner阻挡，主窗仍受保护",!UiModalDialog::nativeOwnerBlocked(&outer) && UiModalDialog::nativeOwnerBlocked(&window));
        second->setFocus();sendInteractionKey(second,Qt::Key_Return);if(outer.isVisible()){recordInteraction(side,"通用默认确认按钮可由Enter触发",false);outer.done(QDialog::Rejected);}
    });
    QTimer::singleShot(10000,&outer,[&]{if(outer.isVisible()){recordInteraction(side,"嵌套交互在时限内结束",false);outer.done(QDialog::Rejected);}});
    const int result=outer.exec();waitInteraction(120);recordInteraction(side,"外层Enter确认正常结束",result==QDialog::Accepted);
    recordInteraction(side,"关闭全部弹窗后恢复主窗焦点",!QApplication::activeModalWidget() && (!ownerEdit || QApplication::focusWidget()==ownerEdit),QApplication::focusWidget() ? QApplication::focusWidget()->objectName() : "null focus");
    bool overlaysGone=true;for(auto *overlay:window.findChildren<QWidget*>("dialogOverlay",Qt::FindDirectChildrenOnly))overlaysGone&=!overlay->isVisible();recordInteraction(side,"关闭嵌套弹窗后遮罩清理",overlaysGone);
    if(QApplication::platformName()=="cocoa")recordInteraction(side,"关闭全部弹窗后主窗原生阻挡解除",!UiModalDialog::nativeOwnerBlocked(&window));
    const int repeats=QApplication::platformName()=="cocoa" ? 8 : 30;bool destroyed=true;
    for(int i=0;i<repeats;++i){auto *rapid=new QDialog(&window);rapid->setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);UiModalDialog::attach(rapid,&window);QPointer<QDialog> guard(rapid);rapid->show();if(i%2)waitInteraction(4);rapid->close();delete rapid;destroyed&=guard.isNull();QApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);waitInteraction(4);}
    bool clean=true;for(auto *overlay:window.findChildren<QWidget*>("dialogOverlay",Qt::FindDirectChildrenOnly))clean&=!overlay->isVisible();recordInteraction(side,QString("快速开关%1个弹窗无悬挂对象遮罩或原生阻挡").arg(repeats),destroyed && clean && (QApplication::platformName()!="cocoa" || !UiModalDialog::nativeOwnerBlocked(&window)));
    QTimer::singleShot(80,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto *input=dialog ? dialog->findChild<QLineEdit*>("promptInput") : nullptr;if(!input){recordInteraction(side,"快速输入框可找到",false);if(dialog)dialog->done(QDialog::Rejected);return;}input->setText("KeyboardType");input->setFocus();sendInteractionKey(input,Qt::Key_Return);if(dialog->isVisible())dialog->done(QDialog::Rejected);});
    const auto prompted=UiMessageBox::prompt(&window,"键盘快速输入","请输入名称",{},100);recordInteraction(side,"共享快速输入弹窗Enter确认返回输入值",prompted && *prompted=="KeyboardType");
    specific();return failures ? 1 : 0;
}
