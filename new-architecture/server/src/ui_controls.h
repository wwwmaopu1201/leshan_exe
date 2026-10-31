#pragma once

#include <QAbstractButton>
#include <QComboBox>
#include <QDate>
#include <QFrame>
#include <QLineEdit>
#include <QLayout>
#include <QProxyStyle>
class QEvent;
class QLabel;
class QPushButton;
class QIntValidator;
class QDoubleValidator;
QIcon passwordVisibilityIcon(bool visible);
QString editableChoiceText(const QComboBox *combo);
QIcon deviceStateIcon(const QString &status);
void bindVersionedProductTitle(QLabel *label,const QString &chinese,const QString &english);

class UiCheckboxStyle : public QProxyStyle {
public:
    explicit UiCheckboxStyle(QStyle *baseStyle=nullptr):QProxyStyle(baseStyle) {}
    void drawPrimitive(PrimitiveElement element,const QStyleOption *option,QPainter *painter,
                       const QWidget *widget=nullptr) const override;
    int pixelMetric(PixelMetric metric,const QStyleOption *option=nullptr,
                    const QWidget *widget=nullptr) const override;
};

class UiFlowLayout : public QLayout {
public:
    explicit UiFlowLayout(QWidget *parent=nullptr,int horizontalSpacing=6,int verticalSpacing=6);
    ~UiFlowLayout() override;
    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;
private:
    int doLayout(const QRect &rect,bool testOnly) const;
    QList<QLayoutItem*> items_;
    int horizontalSpacing_;
    int verticalSpacing_;
};

class ButtonFeedbackFilter : public QObject {
public:
    explicit ButtonFeedbackFilter(QObject *parent=nullptr):QObject(parent) {}
protected:
    bool eventFilter(QObject *watched,QEvent *event) override;
};

class PortStepper : public QFrame {
    Q_OBJECT
public:
    explicit PortStepper(QWidget *parent=nullptr);
    void setRange(int minimum,int maximum);
    void setValue(int value);
    int value() const { return value_; }
signals:
    void valueChanged(int value);
private:
    QLineEdit *input_;
    QIntValidator *validator_;
    int minimum_=1,maximum_=65535,value_=1;
};

class NumericStepper : public QFrame {
    Q_OBJECT
public:
    explicit NumericStepper(int decimals=0,QWidget *parent=nullptr);
    void setRange(double minimum,double maximum);
    void setValue(double value);
    double value() const;
    int decimals() const { return decimals_; }
private:
    QLineEdit *input_;
    QDoubleValidator *validator_;
    double minimum_=0,maximum_=999999999.999,value_=0;
    int decimals_=0;
};

class UiComboBox : public QComboBox {
public:
    explicit UiComboBox(QWidget *parent=nullptr);
protected:
    void showPopup() override;
};

class DateRangeWidget : public QFrame {
public:
    explicit DateRangeWidget(QWidget *parent=nullptr);
    QDate startDate() const { return start_; }
    QDate endDate() const { return end_; }
    void clear();
    void setRange(const QDate &start,const QDate &end);
private:
    void openCalendar(bool start);
    void updateButtons();
    QDate start_,end_;
    QPushButton *startButton_,*endButton_;
};

class UiSwitch : public QAbstractButton {
public:
    explicit UiSwitch(QWidget *parent=nullptr);
    QSize sizeHint() const override { return QSize(40,20); }
protected:
    void paintEvent(QPaintEvent *event) override;
};

class CounterLine : public QLineEdit {
public:
    CounterLine(int maximum, QWidget *parent=nullptr);
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    void updateCounter();
    QLabel *counter_;
    int maximum_;
};
