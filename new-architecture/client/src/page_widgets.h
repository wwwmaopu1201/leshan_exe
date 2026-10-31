#pragma once

#include <QFrame>
#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QTableWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QTreeWidgetItem;

struct TableColumn {
    QString label,key;
    int width=120;
};

QFrame *clientCard(QWidget *parent=nullptr);
QLabel *clientHeading(const QString &text,QWidget *parent=nullptr);
QLabel *clientHint(const QString &text,QWidget *parent=nullptr);
QLabel *clientSelectionLabel(QWidget *parent=nullptr);
QLabel *clientBusinessLabel(const QString &text,QWidget *parent=nullptr);
void setClientSelection(QLabel *label,const QString &source="全部分组",bool business=false);
void setClientTreeSelection(QLabel *label,QTreeWidgetItem *item);
bool clientTreeBusinessLabel(const QJsonObject &node);
QWidget *clientPageHeader(const QString &title,const QString &description,QWidget *parent=nullptr);
QString displayStatus(const QString &value);
QString requestQuery(const QMap<QString,QString> &items);

class ClientTable : public QTableWidget {
public:
    using Action=std::function<void(const QJsonObject&,const QString&)>;
    explicit ClientTable(QWidget *parent=nullptr);
    void setColumns(const QList<TableColumn> &columns,bool selection=false,
                    const QStringList &actions={},Action action={});
    void setRecords(const QJsonArray &records,int firstRow=0);
    void setSummary(const QJsonObject &summary) { summary_=summary; }
    QList<qint64> checkedIds() const;
    QList<QJsonObject> checkedRecords() const;
    QJsonObject recordAt(int row) const;
    int columnForKey(const QString &key) const;
    QJsonArray records() const { return records_; }
    void setEmptyText(const QString &text) { emptyText_=text; viewport()->update(); }
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    void updateActionWidth();
    void translateUiFields();
    void positionEmpty();
    void positionActions();
    QList<TableColumn> columns_;
    QStringList actions_;
    Action action_;
    QJsonArray records_;
    QJsonObject summary_;
    QWidget *empty_=nullptr;
    QString emptyText_="暂无数据";
    bool selection_=false;
    QTableWidget *actionTable_=nullptr;
    int actionWidth_=0;
};

class ClientPagination : public QFrame {
    Q_OBJECT
public:
    explicit ClientPagination(QWidget *parent=nullptr);
    int page() const { return page_; }
    int pageSize() const { return size_; }
    void setTotal(int total);
    void setPage(int page) {page_=page;updateUi();emit changed();}
    void reset() { page_=1; updateUi(); }
signals:
    void changed();
private:
    void updateUi();
    QLabel *totalLabel_,*pageLabel_;
    QLineEdit *jump_;
    QPushButton *previous_,*next_;
    QComboBox *sizes_;
    int page_=1,size_=20,total_=0;
};

class ClientChart : public QFrame {
public:
    enum Kind { Line, Bar, Pie, HorizontalBar };
    explicit ClientChart(Kind kind,QWidget *parent=nullptr);
    void setSeries(const QJsonArray &series,const QString &labelKey="date",const QString &valueKey="value");
    void setSecondaryValue(const QString &key,const QColor &first=QColor("#409eff"),
                           const QColor &second=QColor("#67c23a")) {
        secondaryKey_=key; firstColor_=first; secondColor_=second; update();
    }
    QSize sizeHint() const override { return QSize(380,190); }
    QRectF plotArea() const;
    QRectF pieArea() const;
    void setValueLabels(const QString &primary,const QString &secondary={},const QString &unit={}) {
        primaryLabel_=primary; secondaryLabel_=secondary; valueUnit_=unit; setToolTip({});
    }
    void setHoverDetail(const QString &key,const QString &label,const QString &unit={}) {
        detailKey_=key;detailLabel_=label;detailUnit_=unit;setToolTip({});
    }
protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
private:
    Kind kind_;
    QJsonArray series_;
    QString labelKey_,valueKey_;
    QString secondaryKey_;
    QString primaryLabel_,secondaryLabel_,valueUnit_;
    QString detailKey_,detailLabel_,detailUnit_;
    QColor firstColor_=QColor("#409eff"),secondColor_=QColor("#67c23a");
};
