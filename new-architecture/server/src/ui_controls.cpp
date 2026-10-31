#include "ui_controls.h"
#include "ui_i18n.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCalendarWidget>
#include <QColor>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QIcon>
#include <QIntValidator>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QToolButton>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

QIcon passwordVisibilityIcon(bool visible) {
    QPixmap pixmap(20,20); pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#64748b"),1.5));
    painter.drawEllipse(QRectF(2,5,16,10)); painter.drawEllipse(QRectF(8,8,4,4));
    if(!visible) painter.drawLine(QPointF(3,17),QPointF(17,3));
    painter.end();
    return QIcon(pixmap);
}

QString editableChoiceText(const QComboBox *combo) {
    if(!combo) return {};
    const int index=combo->currentIndex();
    const QString text=combo->currentText().trimmed();
    if(index>=0 && combo->itemData(index).toString().isEmpty() && text==combo->itemText(index).trimmed()) return {};
    return text;
}

QIcon deviceStateIcon(const QString &status) {
    QPixmap dot(14,14); dot.fill(Qt::transparent);
    QPainter painter(&dot); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(status=="working" ? QColor("#f56c6c") : status=="idle" ? QColor("#67c23a") : QColor("#909399"));
    painter.drawEllipse(3,3,8,8); painter.end(); return QIcon(dot);
}
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTimer>
#include <QWidgetAction>

void bindVersionedProductTitle(QLabel *label,const QString &chinese,const QString &english) {
    label->setProperty("i18nSkip",true);label->setMinimumWidth(1);
    auto update=[label,chinese,english]{
        const bool en=UiI18n::instance() && UiI18n::instance()->english();
        const QString text=(en ? english : chinese)+"  v"+QCoreApplication::applicationVersion();
        label->setText(text);label->setToolTip(text);
    };
    if(auto *i18n=UiI18n::instance())QObject::connect(i18n,&UiI18n::languageChanged,label,update);
    update();
}

void UiCheckboxStyle::drawPrimitive(PrimitiveElement element,const QStyleOption *option,QPainter *painter,
                                    const QWidget *widget) const {
    if(element!=PE_IndicatorCheckBox && element!=PE_IndicatorItemViewItemCheck) {
        QProxyStyle::drawPrimitive(element,option,painter,widget); return;
    }
    const bool enabled=option->state & State_Enabled;
    const bool checked=option->state & State_On;
    const bool partial=option->state & State_NoChange;
    const qreal side=qMin<qreal>(16,qMin(option->rect.width(),option->rect.height()));
    const QRectF box(option->rect.x()+(option->rect.width()-side)/2.0+0.5,
                     option->rect.y()+(option->rect.height()-side)/2.0+0.5,side-1,side-1);
    painter->save(); painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(enabled ? QColor(checked || partial ? "#409eff" : "#c8d1dc") : QColor("#dcdfe6"),1));
    painter->setBrush(enabled ? QColor(checked || partial ? "#409eff" : "#ffffff") : QColor("#f5f7fa"));
    painter->drawRoundedRect(box,2,2);
    if(checked || partial) {
        painter->setPen(QPen(enabled ? QColor("#ffffff") : QColor("#b6bec9"),1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        if(partial) painter->drawLine(QPointF(box.left()+4,box.center().y()),QPointF(box.right()-4,box.center().y()));
        else {
            QPainterPath check;
            check.moveTo(box.left()+3.3,box.top()+7.6);
            check.lineTo(box.left()+6.4,box.top()+10.6);
            check.lineTo(box.left()+11.8,box.top()+4.8);
            painter->drawPath(check);
        }
    }
    painter->restore();
}

int UiCheckboxStyle::pixelMetric(PixelMetric metric,const QStyleOption *option,const QWidget *widget) const {
    if(metric==PM_IndicatorWidth || metric==PM_IndicatorHeight) return 16;
    return QProxyStyle::pixelMetric(metric,option,widget);
}

UiFlowLayout::UiFlowLayout(QWidget *parent,int horizontalSpacing,int verticalSpacing)
    :QLayout(parent),horizontalSpacing_(horizontalSpacing),verticalSpacing_(verticalSpacing) {
    setContentsMargins(0,0,0,0);
}
UiFlowLayout::~UiFlowLayout() { while(auto *item=takeAt(0)) delete item; }
void UiFlowLayout::addItem(QLayoutItem *item) { items_.append(item); invalidate(); }
int UiFlowLayout::count() const { return items_.size(); }
QLayoutItem *UiFlowLayout::itemAt(int index) const { return items_.value(index,nullptr); }
QLayoutItem *UiFlowLayout::takeAt(int index) { return index>=0 && index<items_.size() ? items_.takeAt(index) : nullptr; }
Qt::Orientations UiFlowLayout::expandingDirections() const { return {}; }
bool UiFlowLayout::hasHeightForWidth() const { return true; }
int UiFlowLayout::heightForWidth(int width) const { return doLayout(QRect(0,0,width,0),true); }
void UiFlowLayout::setGeometry(const QRect &rect) { QLayout::setGeometry(rect); doLayout(rect,false); }
QSize UiFlowLayout::sizeHint() const { return minimumSize(); }
QSize UiFlowLayout::minimumSize() const {
    QSize size;
    for(auto *item:items_) if(!item->isEmpty())size=size.expandedTo(item->minimumSize());
    const QMargins margins=contentsMargins();
    return size+QSize(margins.left()+margins.right(),margins.top()+margins.bottom());
}
int UiFlowLayout::doLayout(const QRect &rect,bool testOnly) const {
    const QMargins margins=contentsMargins();
    const QRect area=rect.adjusted(margins.left(),margins.top(),-margins.right(),-margins.bottom());
    int x=area.left(), y=area.top(), lineHeight=0;
    for(auto *item:items_) {
        if(item->isEmpty())continue;
        const QSize itemSize=item->sizeHint();
        if(x>area.left() && x+itemSize.width()>area.right()+1) {
            x=area.left(); y+=lineHeight+verticalSpacing_; lineHeight=0;
        }
        if(!testOnly) item->setGeometry(QRect(QPoint(x,y),itemSize));
        x+=itemSize.width()+horizontalSpacing_;
        lineHeight=qMax(lineHeight,itemSize.height());
    }
    return y+lineHeight-rect.y()+margins.bottom();
}

namespace {
class ComboOptionDelegate : public QStyledItemDelegate {
public:
    explicit ComboOptionDelegate(QComboBox *combo):QStyledItemDelegate(combo),combo_(combo) {}
    QSize sizeHint(const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        QSize size=QStyledItemDelegate::sizeHint(option,index);
        size.setHeight(qMax(28,combo_->height()));
        return size;
    }
private:
    QComboBox *combo_;
};
}

bool ButtonFeedbackFilter::eventFilter(QObject *watched,QEvent *event) {
    auto *button=qobject_cast<QAbstractButton*>(watched);
    if(!button || (!qobject_cast<QPushButton*>(button) && !qobject_cast<QToolButton*>(button))) return false;
    if(event->type()==QEvent::Polish || event->type()==QEvent::Show) {
        // Mouse clicks must not acquire a persistent keyboard focus outline.
        // Keep Tab navigation, and honor buttons explicitly configured NoFocus.
        if(button->focusPolicy() & Qt::ClickFocus)
            button->setFocusPolicy((button->focusPolicy() & Qt::TabFocus) ? Qt::TabFocus : Qt::NoFocus);
        if(auto *push=qobject_cast<QPushButton*>(button);push && !push->isDefault())push->setAutoDefault(false);
    }
    if(event->type()==QEvent::FocusIn) {
        const auto reason=static_cast<QFocusEvent*>(event)->reason();
        if(reason==Qt::TabFocusReason || reason==Qt::BacktabFocusReason || reason==Qt::ShortcutFocusReason)
            button->setProperty("mouseActivated",false);
        if(reason==Qt::MouseFocusReason || (reason==Qt::OtherFocusReason && button->property("mouseActivated").toBool()))
            QTimer::singleShot(0,button,[button]{if(button->hasFocus() && button->property("mouseActivated").toBool())button->clearFocus();});
    }
    if(event->type()==QEvent::MouseButtonPress && static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton)
        button->setProperty("mouseActivated",true);
    if(event->type()==QEvent::KeyPress) {
        button->setProperty("mouseActivated",false);
        const int key=static_cast<QKeyEvent*>(event)->key();
        if(auto *push=qobject_cast<QPushButton*>(button);push && push->isEnabled() && push->hasFocus() &&
           !push->autoDefault() && !push->isDefault() && !push->menu() && (key==Qt::Key_Return || key==Qt::Key_Enter)) {
            push->click();return true;
        }
    }
    if(!button->isEnabled())return false;
    bool mouseClick=false,clicked=false;
    if(event->type()==QEvent::MouseButtonRelease) {
        auto *mouse=static_cast<QMouseEvent*>(event);
        clicked=mouseClick=mouse->button()==Qt::LeftButton && button->rect().contains(mouse->position().toPoint());
    } else if(event->type()==QEvent::KeyRelease) {
        const int key=static_cast<QKeyEvent*>(event)->key();
        clicked=key==Qt::Key_Space || key==Qt::Key_Return || key==Qt::Key_Enter;
    }
    if(!clicked)return false;
    if(mouseClick)QTimer::singleShot(0,button,[button]{if(button->hasFocus() && button->property("mouseActivated").toBool())button->clearFocus();});
    const int generation=button->property("feedbackGeneration").toInt()+1;
    button->setProperty("feedbackGeneration",generation);
    button->setProperty("clickFlash",true);
    button->style()->unpolish(button);button->style()->polish(button);button->update();
    QTimer::singleShot(180,button,[button,generation] {
        if(button->property("feedbackGeneration").toInt()!=generation)return;
        button->setProperty("clickFlash",false);
        button->style()->unpolish(button);button->style()->polish(button);button->update();
    });
    return false;
}

PortStepper::PortStepper(QWidget *parent):QFrame(parent) {
    setObjectName("portStepper"); setFixedHeight(28);
    auto *layout=new QHBoxLayout(this); layout->setContentsMargins(1,1,1,1); layout->setSpacing(0);
    input_=new QLineEdit("1"); input_->setObjectName("portStepperInput"); validator_=new QIntValidator(minimum_,maximum_,input_); input_->setValidator(validator_);
    input_->setAlignment(Qt::AlignCenter);
    layout->addWidget(input_,1);
    auto *buttons=new QWidget; buttons->setObjectName("portStepperButtons"); buttons->setFixedWidth(24);
    auto *stack=new QVBoxLayout(buttons); stack->setContentsMargins(0,0,0,0); stack->setSpacing(0);
    auto *up=new QPushButton, *down=new QPushButton;
    up->setObjectName("stepButtonUp"); down->setObjectName("stepButtonDown");
    up->setIcon(QIcon(":/spin-up.svg")); down->setIcon(QIcon(":/spin-down.svg"));
    up->setIconSize(QSize(10,6)); down->setIconSize(QSize(10,6));
    for(auto *button:{up,down}) { button->setFixedSize(24,13); button->setAutoRepeat(true); button->setAutoRepeatDelay(350); button->setAutoRepeatInterval(75); stack->addWidget(button); }
    layout->addWidget(buttons);
    connect(up,&QPushButton::clicked,this,[this]{ setValue(value_+1); });
    connect(down,&QPushButton::clicked,this,[this]{ setValue(value_-1); });
    connect(input_,&QLineEdit::editingFinished,this,[this]{ setValue(input_->text().toInt()); });
}

void PortStepper::setRange(int minimum,int maximum) {
    minimum_=minimum; maximum_=qMax(minimum,maximum);
    validator_->setRange(minimum_,maximum_);
    setValue(value_);
}

void PortStepper::setValue(int value) {
    const int bounded=qBound(minimum_,value,maximum_);
    if(value_!=bounded) { value_=bounded; input_->setText(QString::number(value_)); emit valueChanged(value_); }
    else if(input_->text()!=QString::number(value_)) input_->setText(QString::number(value_));
}

NumericStepper::NumericStepper(int decimals,QWidget *parent):QFrame(parent),decimals_(qBound(0,decimals,6)) {
    setObjectName("numericStepper"); setFixedHeight(34);
    auto *layout=new QHBoxLayout(this); layout->setContentsMargins(1,1,1,1); layout->setSpacing(0);
    input_=new QLineEdit; input_->setObjectName("numericStepperInput");
    validator_=new QDoubleValidator(minimum_,maximum_,decimals_,input_);
    validator_->setNotation(QDoubleValidator::StandardNotation);
    validator_->setLocale(QLocale::c()); input_->setValidator(validator_);
    input_->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
    layout->addWidget(input_,1);
    auto *buttons=new QWidget; buttons->setObjectName("numericStepperButtons"); buttons->setFixedWidth(22);
    auto *stack=new QVBoxLayout(buttons); stack->setContentsMargins(0,0,0,0); stack->setSpacing(0);
    auto *up=new QPushButton, *down=new QPushButton;
    up->setObjectName("numericStepUp"); down->setObjectName("numericStepDown");
    up->setIcon(QIcon(":/spin-up.svg")); down->setIcon(QIcon(":/spin-down.svg"));
    up->setIconSize(QSize(10,6)); down->setIconSize(QSize(10,6));
    for(auto *button:{up,down}) {
        button->setFixedSize(22,16); button->setAutoRepeat(true);
        button->setAutoRepeatDelay(350); button->setAutoRepeatInterval(75);
        stack->addWidget(button);
    }
    layout->addWidget(buttons);
    const double step=decimals_==0 ? 1.0 : 0.001;
    connect(up,&QPushButton::clicked,this,[this,step]{ setValue(value()+step); });
    connect(down,&QPushButton::clicked,this,[this,step]{ setValue(value()-step); });
    connect(input_,&QLineEdit::editingFinished,this,[this]{ setValue(value()); });
    setValue(0);
}

void NumericStepper::setRange(double minimum,double maximum) {
    minimum_=minimum; maximum_=qMax(minimum,maximum);
    validator_->setRange(minimum_,maximum_,decimals_);
    setValue(value());
}

void NumericStepper::setValue(double value) {
    value_=qBound(minimum_,value,maximum_);
    input_->setText(QString::number(value_,'f',decimals_));
}

double NumericStepper::value() const {
    bool valid=false;
    const double parsed=QLocale::c().toDouble(input_->text(),&valid);
    if(!valid) return value_;
    return QString::number(qBound(minimum_,parsed,maximum_),'f',decimals_).toDouble();
}

UiComboBox::UiComboBox(QWidget *parent):QComboBox(parent) {
    auto *list=new QListView(this); list->setItemDelegate(new ComboOptionDelegate(this)); setView(list);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);setMinimumContentsLength(8);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);list->setTextElideMode(Qt::ElideRight);
    setMaxVisibleItems(10);
    connect(this,&QComboBox::currentTextChanged,this,[this](const QString &text){setToolTip(text);});
}

void UiComboBox::showPopup() {
    view()->doItemsLayout();
    QComboBox::showPopup();
    QWidget *popup=view() ? view()->window() : nullptr;
    if(!popup || popup==this) return;
    const QRect screenRect=screen() ? screen()->availableGeometry().adjusted(4,4,-4,-4) : QRect();
    int contentWidth=width();
    for(int i=0;i<count();++i) {
        setItemData(i,itemText(i),Qt::ToolTipRole);
        contentWidth=qMax(contentWidth,view()->fontMetrics().horizontalAdvance(itemText(i))+48);
    }
    const int maximum=screenRect.isValid() ? screenRect.width() : contentWidth;
    if(screenRect.isValid()){popup->setMinimumHeight(0);popup->setMaximumSize(screenRect.size());}
    popup->resize(qMin(maximum,qMax(contentWidth,popup->width())),screenRect.isValid() ? qMin(popup->height(),screenRect.height()) : popup->height());
    QPoint at=mapToGlobal(QPoint(0,height()+2));
    if(screenRect.isValid()) {
        at.setX(qBound(screenRect.left(),at.x(),screenRect.right()-popup->width()+1));
        if(at.y()+popup->height()>screenRect.bottom()) at.setY(mapToGlobal(QPoint(0,0)).y()-popup->height()-2);
        at.setY(qBound(screenRect.top(),at.y(),qMax(screenRect.top(),screenRect.bottom()-popup->height()+1)));
    }
    popup->move(at); popup->raise();
    QTimer::singleShot(0,popup,[popup,screenRect]{if(!screenRect.isValid())return;const QPoint position(qBound(screenRect.left(),popup->x(),qMax(screenRect.left(),screenRect.right()-popup->width()+1)),qBound(screenRect.top(),popup->y(),qMax(screenRect.top(),screenRect.bottom()-popup->height()+1)));popup->move(position);});
}

DateRangeWidget::DateRangeWidget(QWidget *parent):QFrame(parent) {
    setObjectName("dateRange"); setFixedHeight(28);
    auto *layout=new QHBoxLayout(this); layout->setContentsMargins(8,0,6,0); layout->setSpacing(3);
    auto *icon=new QLabel; icon->setPixmap(QIcon(":/calendar.svg").pixmap(QSize(16,16),2.0)); icon->setFixedWidth(16); layout->addWidget(icon);
    startButton_=new QPushButton; endButton_=new QPushButton;
    for(auto *button:{startButton_,endButton_}) { button->setObjectName("datePart"); button->setFlat(true); button->setFixedHeight(24); layout->addWidget(button,1); }
    layout->insertWidget(2,new QLabel("至"));
    connect(startButton_,&QPushButton::clicked,this,[this]{ openCalendar(true); });
    connect(endButton_,&QPushButton::clicked,this,[this]{ openCalendar(false); });
    updateButtons();
}

void DateRangeWidget::clear() { start_=QDate(); end_=QDate(); updateButtons(); }
void DateRangeWidget::setRange(const QDate &start,const QDate &end) {
    start_=start; end_=end;
    if(start_.isValid() && end_.isValid() && start_>end_) qSwap(start_,end_);
    updateButtons();
}

void DateRangeWidget::updateButtons() {
    startButton_->setText(start_.isValid() ? start_.toString("yyyy-MM-dd") : "开始日期");
    endButton_->setText(end_.isValid() ? end_.toString("yyyy-MM-dd") : "结束日期");
    startButton_->setProperty("empty",!start_.isValid()); endButton_->setProperty("empty",!end_.isValid());
    startButton_->style()->unpolish(startButton_); startButton_->style()->polish(startButton_);
    endButton_->style()->unpolish(endButton_); endButton_->style()->polish(endButton_);
    if(UiI18n::instance()) UiI18n::instance()->refresh();
}

void DateRangeWidget::openCalendar(bool start) {
    auto *menu=new QMenu(this); menu->setObjectName("datePopup"); menu->setAttribute(Qt::WA_DeleteOnClose);
    auto *calendar=new QCalendarWidget(menu); calendar->setGridVisible(false);
    calendar->setLocale(UiI18n::instance() && UiI18n::instance()->english() ? QLocale(QLocale::English,QLocale::UnitedStates) : QLocale(QLocale::Chinese,QLocale::China));
    calendar->setNavigationBarVisible(false); calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    calendar->setStyleSheet("QCalendarWidget QAbstractItemView { border: 0; selection-background-color: #409eff; selection-color: white; }");
    calendar->setSelectedDate((start ? start_ : end_).isValid() ? (start ? start_ : end_) : QDate::currentDate());
    auto *content=new QWidget(menu); content->setObjectName("datePopupContent");
    auto *column=new QVBoxLayout(content); column->setContentsMargins(4,4,4,4); column->setSpacing(2);
    auto *header=new QWidget(content); header->setObjectName("calendarHeader");
    auto *row=new QHBoxLayout(header); row->setContentsMargins(4,0,4,0); row->setSpacing(2);
    auto navButton=[header,row](const QString &label,const QString &hint) {
        auto *button=new QPushButton(label,header); button->setObjectName("calendarNavButton"); button->setToolTip(hint); button->setFixedSize(28,28); row->addWidget(button); return button;
    };
    auto *previousYear=navButton("«","上一年"), *previousMonth=navButton("‹","上一月");
    auto *title=new QLabel(header); title->setObjectName("calendarTitle"); title->setAlignment(Qt::AlignCenter); row->addWidget(title,1);
    auto *nextMonth=navButton("›","下一月"), *nextYear=navButton("»","下一年");
    connect(previousYear,&QPushButton::clicked,calendar,&QCalendarWidget::showPreviousYear);
    connect(previousMonth,&QPushButton::clicked,calendar,&QCalendarWidget::showPreviousMonth);
    connect(nextMonth,&QPushButton::clicked,calendar,&QCalendarWidget::showNextMonth);
    connect(nextYear,&QPushButton::clicked,calendar,&QCalendarWidget::showNextYear);
    connect(calendar,&QCalendarWidget::currentPageChanged,title,[title](int year,int month){
        const QString value=QString("%1 年 %2 月").arg(year).arg(month);
        title->setText(UiI18n::instance() ? UiI18n::instance()->text(value) : value);
    });
    const QString monthTitle=QString("%1 年 %2 月").arg(calendar->yearShown()).arg(calendar->monthShown());
    title->setText(UiI18n::instance() ? UiI18n::instance()->text(monthTitle) : monthTitle);
    column->addWidget(header); column->addWidget(calendar);
    auto *action=new QWidgetAction(menu); action->setDefaultWidget(content); menu->addAction(action);
    connect(calendar,&QCalendarWidget::clicked,menu,[this,start,menu](const QDate &selected){
        if(start) { start_=selected; if(end_.isValid() && end_<start_) end_=QDate(); }
        else { end_=selected; if(start_.isValid() && start_>end_) start_=QDate(); }
        updateButtons(); menu->close();
    });
    QPushButton *button=start ? startButton_ : endButton_;
    menu->popup(button->mapToGlobal(QPoint(0,button->height()+2)));
}

UiSwitch::UiSwitch(QWidget *parent):QAbstractButton(parent) {
    setCheckable(true); setCursor(Qt::PointingHandCursor); setFixedSize(40,20);
}

void UiSwitch::paintEvent(QPaintEvent *) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen); painter.setBrush(isChecked() ? QColor("#409eff") : QColor("#dcdfe6"));
    painter.drawRoundedRect(QRectF(0,0,width(),height()),height()/2.0,height()/2.0);
    painter.setBrush(Qt::white); const qreal diameter=16;
    painter.drawEllipse(QRectF(isChecked() ? width()-diameter-2 : 2,2,diameter,diameter));
}

CounterLine::CounterLine(int maximum,QWidget *parent):QLineEdit(parent),counter_(new QLabel(this)),maximum_(maximum) {
    setMaxLength(maximum); setTextMargins(0,0,52,0);
    counter_->setStyleSheet("color: #909399; background: transparent; border: 0; font-size: 11px;");
    counter_->setAlignment(Qt::AlignRight|Qt::AlignVCenter); counter_->setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(this,&QLineEdit::textChanged,this,&CounterLine::updateCounter); updateCounter();
}

void CounterLine::resizeEvent(QResizeEvent *event) {
    QLineEdit::resizeEvent(event); counter_->setGeometry(width()-54,1,46,height()-2);
}

void CounterLine::updateCounter() { counter_->setText(QString::number(text().size())+"/"+QString::number(maximum_)); }
