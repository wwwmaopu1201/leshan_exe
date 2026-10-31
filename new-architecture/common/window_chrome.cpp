#include "window_chrome.h"
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPointer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QTimer>
#include <QAbstractButton>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
namespace {
class Chrome : public QObject {
public:
    Chrome(QWidget *window):QObject(window),window_(window) {
        window->setWindowFlag(Qt::FramelessWindowHint,true);
        scanHeaders();
        qApp->installEventFilter(this);
    }
    ~Chrome() override {qApp->removeEventFilter(this);}
    bool eventFilter(QObject *object,QEvent *event) override {
#ifdef Q_OS_WIN
        if(object==window_ && event->type()==QEvent::Show && QApplication::platformName()=="windows") {
            using SetAttribute=HRESULT (WINAPI *)(HWND,DWORD,LPCVOID,DWORD);
            static const auto library=LoadLibraryExW(L"dwmapi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            static const auto setAttribute=library ? reinterpret_cast<SetAttribute>(GetProcAddress(library,"DwmSetWindowAttribute")) : nullptr;
            if(setAttribute) {const DWORD noBorder=0xfffffffe;setAttribute(reinterpret_cast<HWND>(window_->winId()),34,&noBorder,sizeof(noBorder));}
        }
#endif
        if((event->type()==QEvent::Show || event->type()==QEvent::ChildAdded) && !scanPending_) {
            auto *widget=qobject_cast<QWidget*>(object);
            auto *layout=qobject_cast<QLayout*>(object);
            if((widget && widget->window()==window_) || (layout && layout->parentWidget() && layout->parentWidget()->window()==window_)) {
                scanPending_=true;QTimer::singleShot(0,this,[this]{scanPending_=false;scanHeaders();});
            }
        }
        if(object==window_ && (event->type()==QEvent::Resize || event->type()==QEvent::WindowStateChange || event->type()==QEvent::Show)) {
            if(window_->isMaximized() || window_->isFullScreen())window_->clearMask();
            else {QPainterPath path;path.addRoundedRect(QRectF(window_->rect()),10,10);window_->setMask(QRegion(path.toFillPolygon().toPolygon()));}
        }
        auto *widget=qobject_cast<QWidget*>(object);
        if(!widget || widget->window()!=window_ || (QApplication::activeModalWidget() && QApplication::activeModalWidget()!=window_))return false;
        const bool dragSurface=isDragSurface(widget);
        if(event->type()==QEvent::MouseButtonDblClick && dragSurface) {toggle();return true;}
        if(event->type()!=QEvent::MouseButtonPress)return false;
        auto *mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)return false;
        const QPoint p=window_->mapFromGlobal(mouse->globalPosition().toPoint());
        Qt::Edges edges;
        if(!window_->isMaximized() && !window_->isFullScreen()) {
            if(p.x()<6)edges|=Qt::LeftEdge;if(p.x()>=window_->width()-6)edges|=Qt::RightEdge;
            if(p.y()<6)edges|=Qt::TopEdge;if(p.y()>=window_->height()-6)edges|=Qt::BottomEdge;
        }
        if(edges && window_->windowHandle())return window_->windowHandle()->startSystemResize(edges);
        if(dragSurface && window_->windowHandle())return window_->windowHandle()->startSystemMove();
        return false;
    }
private:
    void scanHeaders() {
        for(auto *row:window_->findChildren<QHBoxLayout*>("windowChromeLayout")) {
            if(row->property("chromeControlsAdded").toBool())continue;
            row->setProperty("chromeControlsAdded",true);
            auto *host=row->parentWidget();host->setProperty("windowDragSurface",true);
            auto *controls=new QWidget(host);controls->setObjectName("windowChrome");controls->setProperty("i18nSkip",true);
            auto *buttons=new QHBoxLayout(controls);buttons->setContentsMargins(0,0,0,0);buttons->setSpacing(0);
            const bool blue=host->objectName()=="clientTopbar" || host->objectName()=="topBar";
            controls->setStyleSheet(QString("QWidget#windowChrome{background:transparent;} QToolButton{border:0;border-radius:0;background:transparent;color:%1;font-size:17px;} QToolButton:hover{background:%2;} QToolButton#windowClose:hover{background:#e34b53;color:white;}").arg(blue ? "white" : "#35445c",blue ? "rgba(255,255,255,40)" : "#dce7f6"));
            auto add=[&](const QString &text,const QString &name){auto *b=new QToolButton(controls);b->setText(text);b->setObjectName(name);b->setFixedSize(36,30);b->setProperty("i18nSkip",true);buttons->addWidget(b);return b;};
            connect(add("−","windowMinimize"),&QToolButton::clicked,window_,&QWidget::showMinimized);
            connect(add("□","windowMaximize"),&QToolButton::clicked,this,[this]{toggle();});
            connect(add("×","windowClose"),&QToolButton::clicked,window_,&QWidget::close);
            row->addWidget(controls);
        }
    }
    bool isDragSurface(QWidget *widget) const {
        if(!qobject_cast<QLabel*>(widget) && !widget->property("windowDragSurface").toBool())return false;
        for(auto *p=widget;p && p!=window_;p=p->parentWidget()) {
            if(qobject_cast<QAbstractButton*>(p))return false;
            if(p->property("windowDragSurface").toBool())return true;
        }
        return false;
    }
    void toggle(){if(window_->isMaximized())window_->showNormal();else window_->showMaximized();}
    QWidget *window_;bool scanPending_=false;
};
}
void attachWindowChrome(QWidget *window) {if(window && !window->property("windowChromeAttached").toBool()){window->setProperty("windowChromeAttached",true);new Chrome(window);}}
