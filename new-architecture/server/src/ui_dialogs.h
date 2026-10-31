#pragma once

#include <QString>
#include <optional>

class QWidget;
class QDialog;
class QMainWindow;
class QLabel;

struct UiModalNativeState {
    bool valid=false;
    bool ignoresMouseEvents=false;
    bool movable=true;
    bool closeEnabled=true;
    bool minimizeEnabled=true;
    bool zoomEnabled=true;
};

class UiModalDialog {
public:
    static void attach(QDialog *dialog,QWidget *owner);
    static bool nativeOwnerBlocked(QWidget *owner);
    static bool nativeOwnerClickThrough(QWidget *owner);
};

class UiWindowGeometry {
public:
    static void attach(QWidget *window);
    static void fitNotification(QLabel *label,QWidget *window,int top=62);
};

class UiMessageBox {
public:
    enum StandardButton { No=0, Yes=1 };
    static void warning(QWidget *parent,const QString &title,const QString &message);
    static void information(QWidget *parent,const QString &title,const QString &message);
    static StandardButton question(QWidget *parent,const QString &title,const QString &message);
    static void output(QWidget *parent,const QString &title,const QString &content);
    static std::optional<QString> prompt(QWidget *parent,const QString &title,const QString &message,
                                         const QString &initial={},int maximum=0,bool lettersAndDigits=false);
};
