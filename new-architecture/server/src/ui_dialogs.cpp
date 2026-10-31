#include "ui_dialogs.h"
#include "window_chrome.h"
#include <QRegularExpression>
#include "ui_i18n.h"

#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QStyle>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QMainWindow>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <QApplication>

#ifdef Q_OS_MACOS
UiModalNativeState uiMacBlockOwnerWindow(QWidget *window);
void uiMacRestoreOwnerWindow(QWidget *window,const UiModalNativeState &state);
bool uiMacOwnerBlocked(QWidget *window);
bool uiMacOwnerClickThrough(QWidget *window);
#endif
namespace {
void fitNotifications(QWidget *window){for(const QString &name:{QString("clientToast"),QString("actionToast"),QString("loginErrorToast")})if(auto *label=window->findChild<QLabel*>(name);label && label->isVisible())UiWindowGeometry::fitNotification(label,window,name=="clientToast" ? 63 : 62);}
class WindowFitter : public QObject {
public:
    explicit WindowFitter(QWidget *window):QObject(window),window_(window),minimum_(window->minimumSize()){window->installEventFilter(this);}
protected:
    bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()==QEvent::Resize)fitNotifications(window_);
        if((event->type()==QEvent::Show && !shown_) || event->type()==QEvent::ScreenChangeInternal || event->type()==QEvent::WindowStateChange){shown_=true;QTimer::singleShot(0,this,[this]{fit();});}
        return false;
    }
private:
    void fit() {
        if(!window_->isVisible() || window_->isMaximized() || window_->isFullScreen() || window_->isMinimized() || !window_->screen())return;
        const QRect area=window_->screen()->availableGeometry().adjusted(8,8,-8,-8);
        const QSize decoration=window_->frameGeometry().size()-window_->size();
        const QSize capacity(qMax(100,area.width()-decoration.width()),qMax(100,area.height()-decoration.height()));
        if((minimum_.width()>capacity.width() || minimum_.height()>capacity.height()) && !wrapped_){
            QWidget *content=nullptr;auto *main=qobject_cast<QMainWindow*>(window_);
            if(main)content=main->takeCentralWidget();else{content=new QWidget;content->setLayout(window_->layout());}
            auto *scroll=new QScrollArea(window_);scroll->setObjectName("adaptiveWindowScroll");scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);content->setMinimumSize(content->minimumSize().expandedTo(minimum_));scroll->setWidget(content);
            if(main)main->setCentralWidget(scroll);else{auto *outer=new QVBoxLayout(window_);outer->setContentsMargins(0,0,0,0);outer->addWidget(scroll);}wrapped_=true;
            // Login layouts contain decorative space to the left. On a small
            // screen initially reveal the form rather than that decoration.
            QTimer::singleShot(0,scroll,[scroll]{for(const QString &name:{QString("clientLoginCard"),QString("loginCard")})if(auto *card=scroll->widget()->findChild<QWidget*>(name);card && card->isVisible()){const QPoint center=card->mapTo(scroll->widget(),card->rect().center());scroll->ensureVisible(center.x(),center.y(),qMin(card->width()/2+12,qMax(1,scroll->viewport()->width()/2-4)),qMin(card->height()/2+12,qMax(1,scroll->viewport()->height()/2-4)));}});
        }
        window_->setMinimumSize(minimum_.boundedTo(capacity));
        window_->resize(window_->size().boundedTo(capacity));
        const QRect frame=window_->frameGeometry();QPoint top=frame.topLeft();top.setX(qBound(area.left(),top.x(),qMax(area.left(),area.right()-frame.width()+1)));top.setY(qBound(area.top(),top.y(),qMax(area.top(),area.bottom()-frame.height()+1)));window_->move(window_->pos()+top-frame.topLeft());
    }
    QWidget *window_;QSize minimum_;bool shown_=false,wrapped_=false;
};
class ModalInputBlocker : public QObject {
public:
    ModalInputBlocker(QDialog *dialog,QWidget *owner):QObject(dialog),dialog_(dialog),owner_(owner) {
        dialog->installEventFilter(this);
        if(owner)owner->installEventFilter(this);
    }
    ~ModalInputBlocker() override { restore(); }
protected:
    bool eventFilter(QObject *watched,QEvent *event) override {
        if(watched==dialog_) {
            if(event->type()==QEvent::Show) {block();preferred_=dialog_->size();QTimer::singleShot(0,this,[this]{fitDialog();});}
            else if(event->type()==QEvent::Hide) restore();
            else if(event->type()==QEvent::Move && active_ && dialog_->property("fixedModalPosition").toBool())fitDialog();
        }
        if(watched==owner_ && (event->type()==QEvent::Resize || event->type()==QEvent::Move || event->type()==QEvent::ScreenChangeInternal) && active_){
            for(auto *overlay:owner_->findChildren<QWidget*>("dialogOverlay",Qt::FindDirectChildrenOnly))overlay->setGeometry(owner_->rect());
            QTimer::singleShot(0,this,[this]{fitDialog();});
        }
        return false;
    }
private:
    void fitDialog() {
        if(fitting_)return;
        QScopedValueRollback<bool> guard(fitting_,true);
        if(!dialog_ || !dialog_->isVisible() || !dialog_->screen())return;
        QScreen *screen=owner_ && dialog_->property("fixedModalPosition").toBool() ? owner_->screen() : dialog_->screen();
        QRect area=screen->availableGeometry().adjusted(10,10,-10,-10);
        if(owner_){const QRect ownerArea(owner_->mapToGlobal(QPoint()),owner_->size());const QRect intersection=area.intersected(ownerArea.adjusted(8,8,-8,-8));if(intersection.width()>200 && intersection.height()>160)area=intersection;}
        if(!preferred_.isValid())preferred_=dialog_->size();
        const QSize target=preferred_.boundedTo(area.size());
        if(target!=preferred_){
            if(dialog_->layout())dialog_->layout()->setSizeConstraint(QLayout::SetNoConstraint);
            dialog_->setMinimumSize(dialog_->minimumSize().boundedTo(target));
            for(auto *scroll:dialog_->findChildren<QScrollArea*>()){scroll->setMinimumSize(0,0);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);}
            for(auto *title:dialog_->findChildren<QLabel*>("dialogTitle")){title->setWordWrap(true);title->setMinimumWidth(0);title->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Preferred);}
        }
        dialog_->resize(target);const QSize size=dialog_->size();dialog_->move(area.left()+qMax(0,(area.width()-size.width())/2),area.top()+qMax(0,(area.height()-size.height())/2));
    }
    void block() {
        if(active_ || !owner_ || owner_==dialog_) return;
        focusBefore_=owner_->focusWidget();
        bool covered=false;
        for(QWidget *existing:owner_->findChildren<QWidget*>("dialogOverlay",Qt::FindDirectChildrenOnly))
            if(existing->isVisible() && existing->geometry()==owner_->rect()) { covered=true; break; }
        if(!covered) {
            overlay_=new QWidget(owner_);
            overlay_->setObjectName("dialogOverlay");
            overlay_->setGeometry(owner_->rect());
            overlay_->setStyleSheet("background: rgba(0, 0, 0, 112);");
            overlay_->show(); overlay_->raise();
        }
#ifdef Q_OS_MACOS
        if(QGuiApplication::platformName()=="cocoa") {
            nativeState_=uiMacBlockOwnerWindow(owner_);
        }
#endif
        active_=true;
    }
    void restore() {
        if(!active_) return;
        if(overlay_) { overlay_->hide(); overlay_->deleteLater(); overlay_=nullptr; }
#ifdef Q_OS_MACOS
        if(owner_ && QGuiApplication::platformName()=="cocoa")
            uiMacRestoreOwnerWindow(owner_,nativeState_);
#endif
        active_=false;
        const QPointer<QWidget> owner=owner_,target=focusBefore_;
        if(owner)QTimer::singleShot(0,owner,[owner,target]{
            if(!owner || !owner->isVisible() || owner->isMinimized())return;
            QWidget *modal=QApplication::activeModalWidget();if(modal && modal!=owner)return;
            owner->activateWindow();if(target && target->isVisible() && target->isEnabled())target->setFocus(Qt::OtherFocusReason);
        });
    }
    QPointer<QDialog> dialog_;
    QPointer<QWidget> owner_;
    QPointer<QWidget> overlay_;
    QPointer<QWidget> focusBefore_;
    bool active_=false,fitting_=false;
    QSize preferred_;
    UiModalNativeState nativeState_;
};
void addOverlay(QWidget *parent,QDialog *dialog) {
    if(!parent) return;
    QWidget *surface=parent->window();
    auto *overlay=new QWidget(surface); overlay->setObjectName("dialogOverlay");
    overlay->setGeometry(surface->rect()); overlay->setStyleSheet("background: rgba(0, 0, 0, 112);");
    overlay->show(); overlay->raise();
    QObject::connect(dialog,&QDialog::finished,overlay,&QObject::deleteLater);
}

QPushButton *addHeader(QDialog *dialog,QVBoxLayout *outer,const QString &title) {
    auto *header=new QFrame; header->setObjectName("dialogHeader"); header->setFixedHeight(54);
    auto *layout=new QHBoxLayout(header); layout->setContentsMargins(20,0,16,0);
    auto *heading=new QLabel(title); heading->setObjectName("dialogTitle"); layout->addWidget(heading); layout->addStretch();
    auto *close=new QPushButton("×"); close->setObjectName("dialogClose"); close->setFixedSize(26,26);
    close->setAutoDefault(false);close->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(close); outer->addWidget(header);
    QObject::connect(close,&QPushButton::clicked,dialog,&QDialog::reject);
    return close;
}

int messageDialog(QWidget *parent,const QString &title,const QString &message,bool confirm) {
    QDialog dialog(parent); addOverlay(parent,&dialog);
    dialog.setObjectName("editorDialog"); dialog.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
    UiModalDialog::attach(&dialog,parent);
    dialog.setWindowTitle(title); dialog.resize(440,qBound(200,170+message.count('\n')*17,460));
    auto *outer=new QVBoxLayout(&dialog); outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);
    addHeader(&dialog,outer,title);
    auto *body=new QScrollArea; body->setObjectName("dialogScroll"); body->setWidgetResizable(true); body->setFrameShape(QFrame::NoFrame);
    auto *content=new QWidget; content->setObjectName("dialogContent"); auto *contentLayout=new QVBoxLayout(content);
    contentLayout->setContentsMargins(22,15,22,15);
    auto *label=new QLabel(message); label->setObjectName("messageBoxText"); label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse); contentLayout->addWidget(label); contentLayout->addStretch();
    body->setWidget(content); outer->addWidget(body,1);
    auto *footer=new QFrame; footer->setObjectName("dialogFooter");
    auto *buttons=new QHBoxLayout(footer); buttons->setContentsMargins(18,10,18,10); buttons->addStretch();
    if(confirm) { auto *cancel=new QPushButton("取消"); buttons->addWidget(cancel); QObject::connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject); }
    auto *accept=new QPushButton(confirm ? "确定" : "知道了"); accept->setObjectName("primaryButton"); buttons->addWidget(accept);
    accept->setDefault(true);
    QObject::connect(accept,&QPushButton::clicked,&dialog,&QDialog::accept); outer->addWidget(footer);
    return dialog.exec();
}
}

void UiWindowGeometry::attach(QWidget *window) {if(window){
#ifdef Q_OS_WIN
attachWindowChrome(window);
#endif
new WindowFitter(window);if(UiI18n::instance())QObject::connect(UiI18n::instance(),&UiI18n::languageChanged,window,[window]{fitNotifications(window);});}}

void UiWindowGeometry::fitNotification(QLabel *label,QWidget *window,int top) {
    if(!label || !window)return;
    label->ensurePolished();
    const int maximumWidth=qMax(100,window->width()-32),maximumHeight=qMax(38,window->height()-top-12);
    label->setMinimumSize(0,0);label->setMaximumSize(maximumWidth,maximumHeight);label->setWordWrap(true);
    const int width=qMin(maximumWidth,label->fontMetrics().horizontalAdvance(label->text())+42);
    const int height=qBound(38,label->heightForWidth(width),maximumHeight);
    label->resize(width,height);label->move((window->width()-width)/2,top);
}

void UiModalDialog::attach(QDialog *dialog,QWidget *owner) {
    if(!dialog) return;
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setModal(true);
    if(owner) new ModalInputBlocker(dialog,owner->window());
}

bool UiModalDialog::nativeOwnerBlocked(QWidget *owner) {
#ifdef Q_OS_MACOS
    if(owner && QGuiApplication::platformName()=="cocoa")
        return uiMacOwnerBlocked(owner->window());
#else
    Q_UNUSED(owner);
#endif
    return false;
}

bool UiModalDialog::nativeOwnerClickThrough(QWidget *owner) {
#ifdef Q_OS_MACOS
    if(owner && QGuiApplication::platformName()=="cocoa")
        return uiMacOwnerClickThrough(owner->window());
#else
    Q_UNUSED(owner);
#endif
    return false;
}

void UiMessageBox::warning(QWidget *parent,const QString &title,const QString &message) { messageDialog(parent,title,message,false); }
void UiMessageBox::information(QWidget *parent,const QString &title,const QString &message) { messageDialog(parent,title,message,false); }
UiMessageBox::StandardButton UiMessageBox::question(QWidget *parent,const QString &title,const QString &message) {
    return messageDialog(parent,title,message,true)==QDialog::Accepted ? Yes : No;
}

void UiMessageBox::output(QWidget *parent,const QString &title,const QString &content) {
    QDialog dialog(parent); addOverlay(parent,&dialog);
    dialog.setObjectName("editorDialog"); dialog.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
    UiModalDialog::attach(&dialog,parent);
    dialog.setWindowTitle(title); dialog.resize(760,480);
    auto *outer=new QVBoxLayout(&dialog); outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);
    addHeader(&dialog,outer,title);
    auto *body=new QWidget; body->setObjectName("dialogContent"); auto *bodyLayout=new QVBoxLayout(body);
    bodyLayout->setContentsMargins(20,10,20,10);
    auto *text=new QTextEdit; text->setReadOnly(true); text->setPlainText(content);
    text->setObjectName("dialogOutput"); bodyLayout->addWidget(text); outer->addWidget(body,1);
    auto *footer=new QFrame; footer->setObjectName("dialogFooter"); auto *buttons=new QHBoxLayout(footer);
    buttons->setContentsMargins(18,10,18,10); buttons->addStretch();
    auto *close=new QPushButton("关闭"); buttons->addWidget(close); outer->addWidget(footer);
    QObject::connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);
    dialog.exec();
}

std::optional<QString> UiMessageBox::prompt(QWidget *parent,const QString &title,const QString &message,
                                             const QString &initial,int maximum,bool lettersAndDigits) {
    QDialog dialog(parent); addOverlay(parent,&dialog);
    dialog.setObjectName("editorDialog"); dialog.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
    UiModalDialog::attach(&dialog,parent);
    dialog.setWindowTitle(title); dialog.resize(440,244);
    auto *outer=new QVBoxLayout(&dialog); outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);
    addHeader(&dialog,outer,title);
    auto *content=new QWidget; content->setObjectName("dialogContent"); auto *body=new QVBoxLayout(content);
    body->setContentsMargins(22,14,22,10); body->setSpacing(8);
    auto *description=new QLabel(message); description->setObjectName("messageBoxText");
    auto *labelRow=new QHBoxLayout; labelRow->addWidget(description);
    auto *required=new QLabel("*"); required->setObjectName("requiredMarker"); required->setStyleSheet("color: #f56c6c;");
    labelRow->addWidget(required); labelRow->addStretch(); body->addLayout(labelRow);
    auto *input=new QLineEdit(initial); input->setObjectName("promptInput");
    input->setPlaceholderText(message); if(maximum>0) input->setMaxLength(maximum); body->addWidget(input);
    auto *error=new QLabel; error->setObjectName("promptError"); error->setFixedHeight(18); error->hide(); body->addWidget(error);
    body->addStretch(); outer->addWidget(content,1);
    auto *footer=new QFrame; footer->setObjectName("dialogFooter"); auto *buttons=new QHBoxLayout(footer);
    buttons->setContentsMargins(18,10,18,10); buttons->addStretch();
    auto *cancel=new QPushButton("取消"), *confirm=new QPushButton("确定"); confirm->setObjectName("primaryButton");
    buttons->addWidget(cancel); buttons->addWidget(confirm); outer->addWidget(footer);
    confirm->setDefault(true);
    QObject::connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
    auto invalidMessage=[&]() {if(input->text().trimmed().isEmpty())return QString("名称不能为空");if(lettersAndDigits && !QRegularExpression("^[A-Za-z0-9]+$").match(input->text()).hasMatch())return QString("订单编号只允许字母和数字");return QString();};
    auto validate=[&] {
        const QString message=invalidMessage();
        if(!message.isEmpty()) {
            error->setText(message); error->show();
            input->setProperty("invalid",true); input->style()->unpolish(input); input->style()->polish(input);
            if(UiI18n::instance()) UiI18n::instance()->refresh();
            input->setFocus(); return;
        }
        dialog.accept();
    };
    QObject::connect(confirm,&QPushButton::clicked,&dialog,validate);
    QObject::connect(input,&QLineEdit::returnPressed,&dialog,validate);
    QObject::connect(input,&QLineEdit::textChanged,&dialog,[&] {
        const QString message=invalidMessage();const bool invalid=!message.isEmpty();
        if(invalid) { error->setText(message); error->show(); }
        else error->hide();
        input->setProperty("invalid",invalid);
        input->style()->unpolish(input); input->style()->polish(input);
    });
    input->setFocus();
    return dialog.exec()==QDialog::Accepted ? std::optional<QString>(input->text().trimmed()) : std::nullopt;
}
