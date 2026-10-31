#include "page_widgets.h"
#include "ui_controls.h"
#include "ui_i18n.h"

#include <QCheckBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollBar>
#include <QUrlQuery>
#include <QTreeWidget>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSet>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
class SelectionLabel : public QLabel {
public:
    explicit SelectionLabel(QWidget *parent):QLabel(parent) {setTextFormat(Qt::PlainText);setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);setMinimumWidth(0);}
    void render(const QString &value) {full_=value;setToolTip(value);QLabel::setText(fontMetrics().elidedText(value,Qt::ElideRight,qMax(0,contentsRect().width())));}
protected:
    void resizeEvent(QResizeEvent *event) override {QLabel::resizeEvent(event);render(full_);}
private:
    QString full_;
};
Qt::Alignment columnAlignment(const QString &key) {
    if(QStringList{"#","status","progress","value","deviceCount","patternCount"}.contains(key)) return Qt::AlignCenter;
    if(QStringList{"stitches","unitPrice","fileSize","size","pieces","totalPieces","salary","totalSalary"}.contains(key))
        return Qt::AlignRight|Qt::AlignVCenter;
    return Qt::AlignLeft|Qt::AlignVCenter;
}
class EmptyArtwork : public QWidget {
public:
    explicit EmptyArtwork(QWidget *parent):QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void setLabel(const QString &label) { label_=label; update(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        const qreal x=(width()-52)/2.0;
        painter.setPen(QPen(QColor("#91a3b8"),2,Qt::SolidLine,Qt::RoundCap));
        painter.setBrush(QColor("#f5f9fe"));
        painter.drawRoundedRect(QRectF(x,5,52,52),5,5);
        painter.drawLine(QPointF(x+12,21),QPointF(x+40,21));
        painter.drawLine(QPointF(x+12,31),QPointF(x+40,31));
        painter.drawLine(QPointF(x+12,41),QPointF(x+32,41));
        painter.setPen(QColor("#8291a4"));
        painter.drawText(QRect(0,69,width(),30),Qt::AlignCenter,
            UiI18n::instance() ? UiI18n::instance()->text(label_) : label_);
    }
private:
    QString label_="暂无数据";
};
QString cellText(const QJsonValue &value) {
    if(value.isNull() || value.isUndefined()) return "-";
    if(value.isBool()) return value.toBool() ? "是" : "否";
    if(value.isString()) return value.toString().isEmpty() ? "-" : value.toString();
    return value.toVariant().toString();
}
QString fileSizeText(qint64 bytes) {
    if(bytes<1024) return QString::number(bytes)+" B";
    if(bytes<1024*1024) return QString::number(bytes/1024.0,'f',1)+" KB";
    return QString::number(bytes/(1024.0*1024.0),'f',1)+" MB";
}
}

QFrame *clientCard(QWidget *parent) { auto *card=new QFrame(parent); card->setObjectName("clientCard"); return card; }
QLabel *clientHeading(const QString &text,QWidget *parent) {
    auto *label=new QLabel(text,parent); label->setObjectName("clientSectionTitle"); label->setWordWrap(true); return label;
}
QLabel *clientHint(const QString &text,QWidget *parent) {
    auto *label=new QLabel(text,parent); label->setObjectName("clientMuted"); label->setWordWrap(true); return label;
}
void setClientSelection(QLabel *label,const QString &source,bool business) {
    label->setProperty("selectionSource",source);label->setProperty("selectionBusiness",business);label->setProperty("i18nSkip",true);
    const QString value=business || !UiI18n::instance() ? source : UiI18n::instance()->text(source);
    label->setMinimumWidth(business ? 0 : qMin(100,label->fontMetrics().horizontalAdvance(value)+2));
    if(auto *selection=dynamic_cast<SelectionLabel*>(label))selection->render(value);else {label->setText(value);label->setToolTip(value);}
}
QLabel *clientBusinessLabel(const QString &text,QWidget *parent) {
    auto *label=new SelectionLabel(parent);label->setProperty("i18nSkip",true);label->render(text);return label;
}
QLabel *clientSelectionLabel(QWidget *parent) {
    auto *label=new SelectionLabel(parent);label->setObjectName("clientSectionTitle");setClientSelection(label);
    if(UiI18n::instance())QObject::connect(UiI18n::instance(),&UiI18n::languageChanged,label,[label]{setClientSelection(label,label->property("selectionSource").toString(),label->property("selectionBusiness").toBool());});
    return label;
}
bool clientTreeBusinessLabel(const QJsonObject &node) {
    const QString id=node.value("id").toVariant().toString();
    if(node.value("type").toString()=="device")return true;
    if(node.value("isVirtual").toBool() || id=="all" || id=="ungrouped")return false;
    const QString name=node.value("label").toString(node.value("name").toString());
    return !(id=="1" && name=="总分组");
}
void setClientTreeSelection(QLabel *label,QTreeWidgetItem *item) {
    const bool business=item->data(0,UiI18n::PreserveDataRole).toBool();QString source=item->text(0);
    if(!business)for(const QString &text:QStringList{"全部分组","未分组设备","总分组"})if(source==text || (UiI18n::instance() && source==UiI18n::instance()->text(text))){source=text;break;}
    setClientSelection(label,source,business);
}
QWidget *clientPageHeader(const QString &title,const QString &description,QWidget *parent) {
    auto *head=new QWidget(parent); auto *layout=new QVBoxLayout(head);
    layout->setContentsMargins(2,0,2,8); layout->setSpacing(3);
    auto *heading=new QLabel(title); heading->setObjectName("clientPageTitle"); layout->addWidget(heading);
    if(!description.isEmpty()) layout->addWidget(clientHint(description));
    return head;
}
QString displayStatus(const QString &value) {
    static const QHash<QString,QString> names={{"idle","空闲"},{"working","缝纫"},{"offline","关机"},
        {"waiting","等待中"},{"downloading","下发中"},{"uploading","回传中"},
        {"paused","已暂停"},{"completed","已完成"},{"failed","失败"},
        {"canceled","已取消"},{"pending","待处理"},{"resolved","已处理"}};
    return names.value(value,value);
}
QString requestQuery(const QMap<QString,QString> &items) {
    QUrlQuery query;
    for(auto it=items.cbegin();it!=items.cend();++it) if(!it.value().trimmed().isEmpty())
        query.addQueryItem(it.key(),it.value());
    const QString text=query.toString(QUrl::FullyEncoded);
    return text.isEmpty() ? QString() : "?"+text;
}

ClientTable::ClientTable(QWidget *parent):QTableWidget(parent) {
    setObjectName("clientTable"); setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setAlternatingRowColors(true); setShowGrid(true);setWordWrap(false);setTextElideMode(Qt::ElideRight);
    setFocusPolicy(Qt::NoFocus); verticalHeader()->hide();
    verticalHeader()->setDefaultSectionSize(38); horizontalHeader()->setFixedHeight(36);
    horizontalHeader()->setStretchLastSection(false);
    auto *artwork=new EmptyArtwork(viewport()); artwork->hide(); empty_=artwork;
    connect(verticalScrollBar(),&QScrollBar::valueChanged,this,[this](int value) {
        if(actionTable_) actionTable_->verticalScrollBar()->setValue(value);
    });
    connect(horizontalHeader(),&QHeaderView::sectionClicked,this,[this](int column) {
        if(!selection_ || column!=0) return;
        bool all=true;
        for(int row=0;row<rowCount();++row) {
            auto *holder=cellWidget(row,0); auto *check=holder ? holder->findChild<QCheckBox*>() : nullptr;
            if(check && !check->isChecked()) all=false;
        }
        for(int row=0;row<rowCount();++row) {
            auto *holder=cellWidget(row,0); auto *check=holder ? holder->findChild<QCheckBox*>() : nullptr;
            if(check) check->setChecked(!all);
        }
    });
    connect(this,&QTableWidget::cellClicked,this,[this](int row,int column) {
        if(!selection_ || column==0 || row<0 || row>=records_.size()) return;
        auto *holder=cellWidget(row,0);
        if(auto *check=holder ? holder->findChild<QCheckBox*>() : nullptr) check->toggle();
    });
    connect(this,&QTableWidget::itemSelectionChanged,this,[this] {
        if(actionTable_ && currentRow()>=0 && currentRow()<actionTable_->rowCount())
            actionTable_->selectRow(currentRow());
    });
    if(UiI18n::instance()) connect(UiI18n::instance(),&UiI18n::languageChanged,
        this,[this]{updateActionWidth();translateUiFields();});
}

void ClientTable::setColumns(const QList<TableColumn> &columns,bool selection,
                             const QStringList &actions,Action action) {
    columns_=columns; selection_=selection; actions_=actions; action_=std::move(action);
    QStringList labels;
    if(selection_) labels << "";
    for(const auto &column:columns_) labels << column.label;
    if(!actions_.isEmpty()) labels << "";
    setColumnCount(labels.size()); setHorizontalHeaderLabels(labels);
    int offset=selection_ ? 1 : 0;
    if(selection_) setColumnWidth(0,48);
    int flexible=-1;
    for(int i=0;i<columns_.size();++i) {
        setColumnWidth(i+offset,columns_[i].width);
        horizontalHeaderItem(i+offset)->setTextAlignment(columnAlignment(columns_[i].key));horizontalHeaderItem(i+offset)->setToolTip(UiI18n::instance() ? UiI18n::instance()->text(columns_[i].label) : columns_[i].label);
        if(flexible<0 && (columns_[i].key=="name" || columns_[i].key=="fileName" ||
                          columns_[i].key=="patternName" || columns_[i].key=="message")) flexible=i+offset;
    }
    if(flexible<0 && !columns_.isEmpty()) flexible=columns_.size()-1+offset;
    if(flexible>=0) horizontalHeader()->setSectionResizeMode(flexible,QHeaderView::Stretch);
    updateActionWidth();
    if(!actions_.isEmpty()) {
        if(!actionTable_) {
            actionTable_=new QTableWidget(this); actionTable_->setObjectName("clientFrozenActions");
            actionTable_->setColumnCount(1); actionTable_->setHorizontalHeaderLabels({"操作"});
            actionTable_->verticalHeader()->hide();
            actionTable_->verticalHeader()->setDefaultSectionSize(38);
            actionTable_->horizontalHeader()->setFixedHeight(36);
            actionTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
            actionTable_->setSelectionMode(QAbstractItemView::SingleSelection);
            actionTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
            actionTable_->setFocusPolicy(Qt::NoFocus);
            actionTable_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            actionTable_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            actionTable_->setAlternatingRowColors(true);
            actionTable_->show();
        }
        actionTable_->setColumnWidth(0,actionWidth_-2); positionActions();
    } else if(actionTable_) actionTable_->hide();
}

void ClientTable::setRecords(const QJsonArray &records,int firstRow) {
    auto selectionKey=[this](const QJsonObject &record){if(property("deviceFiles").toBool())return QString("file:%1:%2:%3").arg(record.value("deviceId").toInteger()).arg(record.value("patternNo").toInt()).arg(record.value("fileName").toString());const qint64 id=record.value("id").toInteger();return id>0 ? "id:"+QString::number(id) : record.value("value").toString().isEmpty() ? QString() : "value:"+record.value("value").toString();};
    QSet<QString> checked;
    if(selection_)for(int row=0;row<records_.size();++row){auto *check=cellWidget(row,0) ? cellWidget(row,0)->findChild<QCheckBox*>() : nullptr;const auto key=selectionKey(records_[row].toObject());if(check && check->isChecked() && !key.isEmpty())checked.insert(key);}
    records_=records;
    clearContents(); setRowCount(records_.size()+(summary_.isEmpty() ? 0 : 1));
    if(actionTable_) { actionTable_->clearContents(); actionTable_->setRowCount(records_.size()); }
    static_cast<EmptyArtwork*>(empty_)->setLabel(emptyText_);
    empty_->setVisible(records_.isEmpty()); positionEmpty();
    const int offset=selection_ ? 1 : 0;
    for(int row=0;row<records_.size();++row) {
        const QJsonObject record=records_[row].toObject();
        if(selection_) {
            auto *holder=new QWidget; auto *layout=new QHBoxLayout(holder);
            layout->setContentsMargins(0,0,0,0); layout->setAlignment(Qt::AlignCenter);
            auto *check=new QCheckBox(holder); check->setObjectName("rowCheckBox");
            check->setFocusPolicy(Qt::NoFocus);check->setChecked(checked.contains(selectionKey(record))); layout->addWidget(check);
            setCellWidget(row,0,holder);
        }
        for(int index=0;index<columns_.size();++index) {
            const auto &column=columns_[index];
            const QString source=column.key=="#" ? QString::number(firstRow+row+1) :
                column.key=="status" ? displayStatus(record.value("status").toString()) :
                column.key=="size" && record.contains("fileSize") ?
                    fileSizeText(record.value("fileSize").toInteger()) :
                cellText(record.value(column.key));
            auto *item=new QTableWidgetItem(source); item->setToolTip(source);
            item->setTextAlignment(columnAlignment(column.key));
            if(row==0 && property("highlightFirst").toBool()) item->setBackground(QColor("#a6f3bb"));
            if(column.key!="sortOrder" || !property("editableSort").toBool())
                item->setFlags(item->flags()&~Qt::ItemIsEditable);
            if(column.key!="#" && column.key!="status") item->setData(UiI18n::PreserveDataRole,true);
            if(column.key=="status") {
                const QString state=record.value("status").toString();
                item->setForeground(QColor(state=="working" ? "#f56c6c" : state=="idle" ? "#67c23a" : state=="offline" ? "#909399" :
                    state=="completed" || state=="resolved" ? "#67c23a" : state=="failed" || state=="canceled" ? "#f56c6c" : "#e6a23c"));
                item->setTextAlignment(Qt::AlignCenter);
            }
            if(column.key=="#") item->setTextAlignment(Qt::AlignCenter);
            if(property("linkCounts").toBool() &&
               (column.key=="deviceCount" || column.key=="patternCount")) {
                item->setForeground(QColor("#409eff"));
                item->setTextAlignment(Qt::AlignCenter);
            }
            setItem(row,index+offset,item);
            if(column.key=="status") {
                auto *holder=new QWidget; auto *layout=new QHBoxLayout(holder);
                layout->setContentsMargins(2,4,2,4); layout->setAlignment(Qt::AlignCenter);
                auto *pill=new QLabel(source);
                const QString state=record.value("status").toString();
                pill->setObjectName(state=="offline" ? "clientStatusNeutral" : state=="working" ? "clientStatusBad" : state=="idle" || state=="completed" || state=="resolved" ||
                    state=="成功" || state=="已处理" ? "clientStatusOk" :
                    state=="failed" || state=="canceled" || state=="失败" ?
                    "clientStatusBad" : "clientStatusWarn");
                pill->setAlignment(Qt::AlignCenter);
                pill->setMinimumWidth(qMax(48,pill->fontMetrics().horizontalAdvance(source)+20));
                layout->addWidget(pill); setCellWidget(row,index+offset,holder);
            }
            if(column.key=="progress") {
                auto *holder=new QWidget; auto *layout=new QHBoxLayout(holder);
                layout->setContentsMargins(5,4,5,4);
                auto *progress=new QProgressBar; progress->setObjectName("clientProgress");
                progress->setRange(0,100);
                progress->setValue(qBound(0,record.value("progress").toInt(),100));
                progress->setFixedHeight(18); progress->setFormat("%p%");
                layout->addWidget(progress); setCellWidget(row,index+offset,holder);
            }
        }
        if(!actions_.isEmpty()) {
            auto *holder=new QWidget; auto *layout=new QHBoxLayout(holder);
            layout->setContentsMargins(2,0,2,0); layout->setSpacing(1);
            for(const QString &action:actions_) {
                auto *button=new QPushButton(action);
                if(property("uploadQueue").toBool()) {
                    const QString status=record.value("status").toString();
                    if(action=="暂停") button->setEnabled(status=="waiting");
                    else if(action=="继续") button->setEnabled(status=="paused");
                    else if(action=="取消") button->setEnabled(status=="waiting" || status=="paused");
                }
                button->setObjectName(action=="删除" || action=="取消" || action=="移出" ?
                    "clientDangerLink" : "clientLink");
                button->setFocusPolicy(Qt::NoFocus);
                button->setMinimumWidth(button->fontMetrics().horizontalAdvance(
                    UiI18n::instance() ? UiI18n::instance()->text(action) : action)+12);
                layout->addWidget(button);
                connect(button,&QPushButton::clicked,this,[this,record,action] {
                    if(action_) action_(record,action);
                });
            }
            if(actionTable_) actionTable_->setCellWidget(row,0,holder);
        }
    }
    if(!summary_.isEmpty()) {
        const int row=records_.size();
        for(int index=0;index<columns_.size();++index) {
            const QString key=columns_[index].key;
            auto *item=new QTableWidgetItem(summary_.value(key).toVariant().toString());
            item->setFlags(Qt::ItemIsEnabled);
            item->setBackground(QColor("#f5f8fb"));
            item->setForeground(QColor("#334155"));
            if(key=="#") item->setTextAlignment(Qt::AlignCenter);
            setItem(row,index+(selection_ ? 1 : 0),item);
        }
    }
    translateUiFields();
}

void ClientTable::translateUiFields() {
    const int column=columnForKey("message"),sizeColumn=columnForKey("size");if(column<0 && sizeColumn<0)return;
    QSignalBlocker blocker(this);
    for(int column=0;column<columnCount();++column)if(auto *header=horizontalHeaderItem(column))header->setToolTip(header->text());
    for(int row=0;row<records_.size();++row){if(column>=0)if(auto *cell=item(row,column)) {
        const QString source=records_[row].toObject().value("message").toString();
        const QString display=UiI18n::instance() ? UiI18n::instance()->text(source) : source;
        cell->setText(display);cell->setToolTip(display);
    }
        if(property("deviceFiles").toBool() && sizeColumn>=0 && records_[row].toObject().value("fileSize").toInteger()<=0)if(auto *cell=item(row,sizeColumn)) {
            cell->setText(UiI18n::instance() ? UiI18n::instance()->text("未知") : QString("未知"));
            cell->setToolTip(UiI18n::instance() ? UiI18n::instance()->text("设备未提供文件大小，可按设备文件编号区分同名花型") : QString("设备未提供文件大小，可按设备文件编号区分同名花型"));
        }
    }
}

QList<qint64> ClientTable::checkedIds() const {
    QList<qint64> selected;
    if(!selection_) return selected;
    for(int row=0;row<records_.size();++row) {
        auto *holder=cellWidget(row,0); auto *check=holder ? holder->findChild<QCheckBox*>() : nullptr;
        if(check && check->isChecked()) selected << records_.at(row).toObject().value("id").toInteger();
    }
    return selected;
}
void ClientTable::updateActionWidth() {
    if(actions_.isEmpty()) { actionWidth_=0; return; }
    int width=12;
    const QFontMetrics measure(font());
    for(const QString &action:actions_)
        width+=qMax(40,measure.horizontalAdvance(
            UiI18n::instance() ? UiI18n::instance()->text(action) : action)+20);
    width+=qMax(0,actions_.size()-1)*2;
    actionWidth_=qMax(100,width);
    if(columnCount()>0) setColumnWidth(columnCount()-1,actionWidth_);
    if(actionTable_) { actionTable_->setColumnWidth(0,actionWidth_-2); positionActions(); }
}

QList<QJsonObject> ClientTable::checkedRecords() const {
    QList<QJsonObject> selected;
    if(!selection_) return selected;
    for(int row=0;row<records_.size();++row) {
        auto *holder=cellWidget(row,0); auto *check=holder ? holder->findChild<QCheckBox*>() : nullptr;
        if(check && check->isChecked()) selected << records_[row].toObject();
    }
    return selected;
}

QJsonObject ClientTable::recordAt(int row) const {
    return row>=0 && row<records_.size() ? records_.at(row).toObject() : QJsonObject{};
}

int ClientTable::columnForKey(const QString &key) const {
    for(int i=0;i<columns_.size();++i) if(columns_[i].key==key) return i+(selection_ ? 1 : 0);
    return -1;
}

void ClientTable::resizeEvent(QResizeEvent *event) {
    QTableWidget::resizeEvent(event); positionEmpty(); positionActions();
}
void ClientTable::positionEmpty() {
    if(empty_) { empty_->setGeometry((viewport()->width()-180)/2,(viewport()->height()-110)/2,180,110); empty_->raise(); }
}
void ClientTable::positionActions() {
    if(!actionTable_ || actionWidth_<1) return;
    actionTable_->setGeometry(viewport()->geometry().right()-actionWidth_+1,
        horizontalHeader()->geometry().top(),
        actionWidth_,viewport()->height()+horizontalHeader()->height());
    actionTable_->raise();
}

ClientPagination::ClientPagination(QWidget *parent):QFrame(parent) {
    auto *layout=new QHBoxLayout(this); layout->setContentsMargins(0,2,0,0); layout->setSpacing(7);
    totalLabel_=clientHint("共 0 条"); layout->addWidget(totalLabel_); layout->addStretch();
    sizes_=new UiComboBox; for(int size:{10,20,50,100}) sizes_->addItem(QString("%1条/页").arg(size),size);
    sizes_->setCurrentIndex(1); sizes_->setFixedWidth(100); layout->addWidget(sizes_);
    previous_=new QPushButton("‹"); next_=new QPushButton("›");
    for(auto *button:{previous_,next_}) button->setFixedSize(28,28);
    layout->addWidget(previous_); pageLabel_=new QLabel("1 / 1"); layout->addWidget(pageLabel_); layout->addWidget(next_);
    layout->addWidget(new QLabel("前往")); jump_=new QLineEdit("1"); jump_->setFixedWidth(46);
    jump_->setValidator(new QIntValidator(1,1000000,jump_)); jump_->setAlignment(Qt::AlignCenter);
    layout->addWidget(jump_); layout->addWidget(new QLabel("页"));
    connect(sizes_,&QComboBox::currentIndexChanged,this,[this](int) {
        size_=sizes_->currentData().toInt(); page_=1; updateUi(); emit changed();
    });
    connect(previous_,&QPushButton::clicked,this,[this] { page_=qMax(1,page_-1); updateUi(); emit changed(); });
    connect(next_,&QPushButton::clicked,this,[this] { page_=qMin(qMax(1,(total_+size_-1)/size_),page_+1); updateUi(); emit changed(); });
    connect(jump_,&QLineEdit::returnPressed,this,[this] {
        page_=qBound(1,jump_->text().toInt(),qMax(1,(total_+size_-1)/size_)); updateUi(); emit changed();
    });
    updateUi();
}

void ClientPagination::setTotal(int total) { total_=qMax(0,total); updateUi(); }
void ClientPagination::updateUi() {
    const int count=qMax(1,(total_+size_-1)/size_); page_=qBound(1,page_,count);
    totalLabel_->setText(QString("共 %1 条").arg(total_));
    pageLabel_->setText(QString("%1 / %2").arg(page_).arg(count));
    jump_->setText(QString::number(page_)); previous_->setEnabled(page_>1); next_->setEnabled(page_<count);
}

ClientChart::ClientChart(Kind kind,QWidget *parent):QFrame(parent),kind_(kind) {
    setMinimumHeight(160); setMouseTracking(true);
}
void ClientChart::setSeries(const QJsonArray &series,const QString &labelKey,const QString &valueKey) {
    series_=series; labelKey_=labelKey; valueKey_=valueKey; setToolTip({}); update();
}
void ClientChart::paintEvent(QPaintEvent *) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(),Qt::white);
    const QRectF plot=plotArea();
    if(series_.isEmpty()) {
        painter.setPen(QColor("#9aa8b8"));
        painter.drawText(rect(),Qt::AlignCenter,UiI18n::instance() ? UiI18n::instance()->text("暂无数据") : "暂无数据");
        return;
    }
    QVector<double> values; values.reserve(series_.size());
    for(const QJsonValue &value:series_) values << value.toObject().value(valueKey_).toDouble();
    QVector<double> secondValues;
    if(!secondaryKey_.isEmpty()) for(const QJsonValue &value:series_)
        secondValues << value.toObject().value(secondaryKey_).toDouble();
    const double maximum=qMax(1.0,qMax(*std::max_element(values.cbegin(),values.cend()),
        secondValues.isEmpty() ? 0.0 : *std::max_element(secondValues.cbegin(),secondValues.cend())));
    if(kind_==HorizontalBar) {
        const int count=qMin(10,series_.size());
        const qreal top=25,rowHeight=qMin<qreal>(38,(height()-45.0)/qMax(1,count));
        const qreal left=74,right=qMax<qreal>(left+30,width()-18);
        QLinearGradient gradient(left,0,right,0);
        gradient.setColorAt(0,QColor("#409eff")); gradient.setColorAt(1,QColor("#29bf80"));
        for(int i=0;i<count;++i) {
            const QJsonObject row=series_[i].toObject();
            const qreal y=top+i*rowHeight;
            painter.setPen(QColor("#6a7f9d"));
            painter.drawText(QRectF(0,y-2,left-5,22),Qt::AlignRight,
                row.value(labelKey_).toString().left(8));
            const qreal length=(right-left)*qBound(0.0,values[i]/maximum,1.0);
            painter.setPen(Qt::NoPen); painter.setBrush(gradient);
            painter.drawRoundedRect(QRectF(left,y+3,length,14),6,6);
        }
        return;
    }
    if(kind_==Pie) {
        for(double &value:values)value=qMax(0.0,value);
        const double total=std::accumulate(values.cbegin(),values.cend(),0.0);
        const QRectF circle=pieArea();
        const QList<QColor> colors={QColor("#409eff"),QColor("#67c23a"),QColor("#e6a23c"),QColor("#f56c6c"),QColor("#9b83e8")};
        int begin=90*16;
        for(int i=0;i<values.size() && total>0;++i) {
            const int angle=qRound(values[i]/total*360*16);
            painter.setBrush(i<colors.size() ? colors[i] : QColor::fromHsv((i*137)%360,145,225)); painter.setPen(Qt::white);
            painter.drawPie(circle,begin,-angle); begin-=angle;
        }
        if(total<=0) { painter.setBrush(QColor("#edf2f7")); painter.drawEllipse(circle); }
        return;
    }
    painter.setPen(QPen(QColor("#e7edf5"),1));
    for(int i=0;i<=4;++i) {
        const qreal y=plot.bottom()-plot.height()*i/4.0;
        painter.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));
    }
    painter.setPen(QColor("#98a6b8"));
    painter.drawText(QRectF(0,plot.top()-6,plot.left()-5,18),Qt::AlignRight,QString::number(maximum,'f',0));
    const int count=values.size();
    const int labelStep=qMax(1,int(std::ceil(double(count)*78.0/qMax<qreal>(1,plot.width()))));
    QPainterPath line,secondLine;
    for(int i=0;i<count;++i) {
        const qreal x=plot.left()+plot.width()*(i+0.5)/count;
        const qreal y=plot.bottom()-qBound(0.0,values[i]/maximum,1.0)*plot.height();
        if(kind_==Bar) {
            const qreal bar=qMin<qreal>(32,plot.width()/count*0.58);
            painter.fillRect(QRectF(x-bar/2,y,bar,plot.bottom()-y),QColor("#73b3fb"));
        } else {
            if(i==0) line.moveTo(x,y); else line.lineTo(x,y);
            if(!secondValues.isEmpty()) {
                const qreal secondY=plot.bottom()-qBound(0.0,secondValues[i]/maximum,1.0)*plot.height();
                if(i==0) secondLine.moveTo(x,secondY); else secondLine.lineTo(x,secondY);
            }
        }
        const bool last=i==count-1;
        if(last || (i%labelStep==0 && (i==0 || count-1-i>=labelStep))) {
            const QString label=series_[i].toObject().value(labelKey_).toVariant().toString();
            painter.setPen(QColor("#9aa8b8"));
            const qreal labelLeft=qBound(0.0,x-35,qMax(0.0,width()-70.0));
            painter.drawText(QRectF(labelLeft,plot.bottom()+5,70,20),Qt::AlignCenter,label.right(8));
        }
    }
    if(kind_==Line) {
        painter.setPen(QPen(firstColor_,2.5)); painter.drawPath(line);
        if(!secondValues.isEmpty()) { painter.setPen(QPen(secondColor_,2.5)); painter.drawPath(secondLine); }
        for(int i=0;i<count;++i) {
            const qreal x=plot.left()+plot.width()*(i+0.5)/count;
            painter.setPen(Qt::NoPen); painter.setBrush(firstColor_);
            painter.drawEllipse(QPointF(x,plot.bottom()-qBound(0.0,values[i]/maximum,1.0)*plot.height()),3.1,3.1);
            if(!secondValues.isEmpty()) {
                painter.setBrush(secondColor_);
                painter.drawEllipse(QPointF(x,plot.bottom()-qBound(0.0,secondValues[i]/maximum,1.0)*plot.height()),3.1,3.1);
            }
        }
    }
}
QRectF ClientChart::plotArea() const {
    double maximum=1;
    for(const QJsonValue &value:series_) {
        const QJsonObject row=value.toObject();
        maximum=qMax(maximum,row.value(valueKey_).toDouble());
        if(!secondaryKey_.isEmpty()) maximum=qMax(maximum,row.value(secondaryKey_).toDouble());
    }
    const int left=qMax(32,fontMetrics().horizontalAdvance(QString::number(maximum,'f',0))+8);
    return QRectF(left,18,qMax(1,width()-left-16),qMax(1,height()-52));
}
QRectF ClientChart::pieArea() const {
    const qreal diameter=qMax<qreal>(1,qMin<qreal>(240,qMin(width()-24.0,height()-36.0)));
    return QRectF((width()-diameter)/2.0,18,diameter,diameter);
}
void ClientChart::mouseMoveEvent(QMouseEvent *event) {
    if(series_.isEmpty()) return;
    const QRectF plot=plotArea();
    const double location=kind_==HorizontalBar ?
        (event->position().y()-25)/qMin<qreal>(38,(height()-45.0)/qMax(1,qMin(10,series_.size()))) :
        (event->position().x()-plot.left())/plot.width()*series_.size();
    int index=qBound(0,int(location),series_.size()-1);
    if(kind_==Pie) {
        const auto circle=pieArea();const qreal dx=event->position().x()-circle.center().x(),dy=event->position().y()-circle.center().y();
        double total=0;for(const auto &value:series_)total+=qMax(0.0,value.toObject().value(valueKey_).toDouble());
        if(dx*dx+dy*dy>circle.width()*circle.width()/4 || total<=0){setToolTip({});return;}
        double angle=std::atan2(dx,-dy)*180.0/3.14159265358979323846;if(angle<0)angle+=360;
        int cumulative=0;index=series_.size()-1;
        for(int i=0;i<series_.size();++i){const int span=qRound(qMax(0.0,series_[i].toObject().value(valueKey_).toDouble())/total*360*16);if(angle*16<cumulative+span){index=i;break;}cumulative+=span;}
    }
    const QJsonObject row=series_.at(index).toObject();
    auto formatted=[](const QJsonValue &value,const QString &unit) {
        const bool english=UiI18n::instance() && UiI18n::instance()->english();
        if(unit=="hours") {
            const qint64 seconds=qMax<qint64>(0,qRound64(value.toDouble()*3600));
            return (english ? QString("%1h %2m %3s") : QString("%1小时%2分%3秒")).arg(seconds/3600).arg(seconds%3600/60).arg(seconds%60);
        }
        return value.toVariant().toString()+(unit=="percent" ? "%" : unit=="minutes" ? (english ? " min" : "分") : unit=="currency" ? (english ? " CNY" : "元") : "");
    };
    auto label=[](const QString &source) { return source.isEmpty() ? QString() : (UiI18n::instance() ? UiI18n::instance()->text(source) : source)+": "; };
    QString tip=row.value(labelKey_).toVariant().toString()+"  "+label(primaryLabel_)+formatted(row.value(valueKey_),valueUnit_);
    if(!secondaryKey_.isEmpty()) tip+=" / "+label(secondaryLabel_)+formatted(row.value(secondaryKey_),valueUnit_);
    if(!detailKey_.isEmpty() && row.contains(detailKey_))tip+="\n"+label(detailLabel_)+formatted(row.value(detailKey_),detailUnit_);
    setToolTip(tip);
}
