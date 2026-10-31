#include "ui_controls.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>
static void wait(int ms) {QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
static void click(QAbstractButton *b) {
    const QPointF p=b->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress,p,b->mapToGlobal(p.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,p,b->mapToGlobal(p.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(b,&press);QApplication::sendEvent(b,&release);
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.installEventFilter(new ButtonFeedbackFilter(&app));
    int errors=0;auto check=[&](bool ok,const char *message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);++errors;}};
    QDialog owner;auto *layout=new QVBoxLayout(&owner);auto *input=new QLineEdit;layout->addWidget(input);
    QList<QPushButton*> actions;
    for(const auto &label:{"刷新","查询","重置","导出","上一页","下一页"}){auto *b=new QPushButton(label);layout->addWidget(b);actions<<b;}
    auto *save=new QPushButton("保存");save->setDefault(true);layout->addWidget(save);
    auto *toggle=new QPushButton("展开");toggle->setCheckable(true);layout->addWidget(toggle);
    auto *tool=new QToolButton;tool->setText("工具");tool->setFocusPolicy(Qt::StrongFocus);layout->addWidget(tool);
    auto *noFocus=new QPushButton("关闭");noFocus->setFocusPolicy(Qt::NoFocus);layout->addWidget(noFocus);
    auto *checkbox=new QCheckBox("真实选中状态");layout->addWidget(checkbox);
    owner.show();input->setFocus();wait(20);
    for(auto *b:actions) {
        int clicks=0;const auto connection=QObject::connect(b,&QPushButton::clicked,[&]{++clicks;});
        check(!(b->focusPolicy() & Qt::ClickFocus) && (b->focusPolicy() & Qt::TabFocus),"mouse focus removed, Tab focus retained");
        check(!b->autoDefault() && !b->isDefault(),"ordinary action does not become default");
        click(b);b->setFocus(Qt::MouseFocusReason);wait(1);
        check(clicks==1 && !b->hasFocus() && !b->isDown() && !b->isChecked(),"mouse action fires once and releases focus/pressed state");
        check(b->property("clickFlash").toBool(),"brief feedback visible");
        wait(220);check(!b->property("clickFlash").toBool(),"feedback expires");
        b->setFocus(Qt::TabFocusReason);wait(1);check(b->hasFocus(),"keyboard focus remains visible");
        QKeyEvent down(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier),up(QEvent::KeyRelease,Qt::Key_Space,Qt::NoModifier);
        QApplication::sendEvent(b,&down);QApplication::sendEvent(b,&up);wait(220);
        check(clicks==2 && b->hasFocus() && !b->isDown(),"Space activates button and preserves keyboard focus");
        QKeyEvent enterDown(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier),enterUp(QEvent::KeyRelease,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(b,&enterDown);QApplication::sendEvent(b,&enterUp);wait(220);
        check(clicks==3 && b->hasFocus(),"Enter invokes focused ordinary action without becoming a default button");
        QObject::disconnect(connection);
    }
    check(save->isDefault(),"explicit default confirmation preserved");
    int saves=0;QObject::connect(save,&QPushButton::clicked,[&]{++saves;});
    input->setFocus();QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(input,&enter);check(saves==1,"Enter still invokes explicit default confirmation");
    click(toggle);wait(220);check(toggle->isChecked(),"real checked state preserved after feedback expires");
    click(checkbox);check(checkbox->isChecked(),"checkbox state preserved");
    click(tool);tool->setFocus(Qt::MouseFocusReason);wait(220);check(!tool->hasFocus() && !tool->isDown(),"tool button also releases mouse focus");
    check(noFocus->focusPolicy()==Qt::NoFocus,"NoFocus behavior preserved");
    auto *rapid=actions.first();click(rapid);wait(100);click(rapid);wait(100);check(rapid->property("clickFlash").toBool(),"latest rapid click retains brief feedback");wait(120);check(!rapid->property("clickFlash").toBool(),"rapid clicks do not leave stuck feedback");
    rapid->setEnabled(false);click(rapid);check(!rapid->property("clickFlash").toBool(),"disabled action has no click feedback");
    auto *temporary=new QPushButton("销毁测试",&owner);temporary->show();click(temporary);delete temporary;wait(220);
    return errors?1:0;
}
