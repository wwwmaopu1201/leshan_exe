#include "window_chrome.h"
#include "ui_dialogs.h"
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <cstdio>
int main(int argc,char **argv) {
    QApplication app(argc,argv);int errors=0;
    auto check=[&](bool ok,const char *label){if(!ok){std::fprintf(stderr,"FAIL: %s\n",label);++errors;}};
    QMainWindow window;auto *content=new QWidget;auto *body=new QVBoxLayout(content);body->setContentsMargins(0,0,0,0);
    auto *top=new QWidget;top->setObjectName("clientTopbar");auto *header=new QHBoxLayout(top);header->setObjectName("windowChromeLayout");header->addWidget(new QLabel("Existing blue header"));body->addWidget(top);body->addWidget(new QLabel("Business content"),1);
    window.setCentralWidget(content);window.resize(800,600);
    attachWindowChrome(&window);attachWindowChrome(&window);window.show();app.processEvents();
    check(window.windowFlags().testFlag(Qt::FramelessWindowHint),"no native title bar");
    check(window.findChildren<QWidget*>("windowChrome").size()==1,"attach is idempotent");
    check(content->isVisible() && window.centralWidget()==content,"no extra title-bar wrapper inserted");
    check(top->mapTo(&window,QPoint()).y()==0 && window.findChild<QWidget*>("windowChrome")->parentWidget()==top,"window controls are inside the existing top bar");
    check(!window.mask().isEmpty() && !window.mask().contains(QPoint(0,0)),"rounded restored window");
    auto *maximize=window.findChild<QToolButton*>("windowMaximize");maximize->click();app.processEvents();
    check(window.isMaximized() && window.mask().isEmpty(),"maximize clears rounded mask");
    maximize->click();app.processEvents();check(!window.isMaximized() && !window.mask().isEmpty(),"restore rounds window");
    auto *dynamicHeader=new QWidget;auto *dynamicRow=new QHBoxLayout(dynamicHeader);dynamicRow->setObjectName("windowChromeLayout");body->addWidget(dynamicHeader);dynamicHeader->show();app.processEvents();app.processEvents();
    check(dynamicHeader->findChild<QWidget*>("windowChrome"),"headers created after login also receive inline controls");delete dynamicHeader;
    QDialog login;auto *layout=new QHBoxLayout(&login);layout->setObjectName("windowChromeLayout");auto *input=new QLabel("Login content");layout->addWidget(input);attachWindowChrome(&login);login.show();app.processEvents();
    check(input->isVisible() && login.findChild<QToolButton*>("windowClose"),"login content and controls preserved");
    QDialog prompt(&window);prompt.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);prompt.resize(300,180);UiModalDialog::attach(&prompt,&window);prompt.show();app.processEvents();
    check(prompt.isModal(),"update prompt remains modal");
    check(prompt.windowFlags().testFlag(Qt::FramelessWindowHint),"fixed modal has no drag title bar");
    prompt.reject();window.findChild<QToolButton*>("windowClose")->click();app.processEvents();check(!window.isVisible(),"close control works");
    return errors?1:0;
}
