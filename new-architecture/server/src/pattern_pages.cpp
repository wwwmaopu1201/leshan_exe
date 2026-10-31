#include <QRegularExpressionValidator>
#include "pattern_pages.h"
#include "database.h"
#include "device_server.h"
#include "ui_controls.h"
#include "ui_dialogs.h"
#include "ui_i18n.h"
#include "stats_time.h"
#include "upload_summary.h"
#include <QProgressBar>

#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QProgressDialog>
#include <QRandomGenerator>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include <climits>

namespace {
class PatternEmptyState : public QWidget {
public:
    explicit PatternEmptyState(QWidget *parent):QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        const qreal x=(width()-52)/2.0;
        painter.setPen(QPen(QColor("#8fa1b7"),2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter.setBrush(QColor("#f7faff"));
        painter.drawRoundedRect(QRectF(x,5,52,52),4,4);
        painter.drawLine(QPointF(x+12,21),QPointF(x+40,21));
        painter.drawLine(QPointF(x+12,31),QPointF(x+40,31));
        painter.drawLine(QPointF(x+12,41),QPointF(x+32,41));
        painter.setPen(QColor("#66768a"));
        painter.drawText(QRect(0,70,width(),25),Qt::AlignCenter,
            UiI18n::instance() ? UiI18n::instance()->text("暂无数据") : "暂无数据");
    }
};
class PatternTable : public QTableWidget {
public:
    PatternTable() {
        empty_=new PatternEmptyState(viewport()); empty_->hide();
    }
    void showEmpty(bool visible) { empty_->setVisible(visible); position(); }
protected:
    void resizeEvent(QResizeEvent *event) override { QTableWidget::resizeEvent(event); position(); }
private:
    void position() {
        empty_->setGeometry((viewport()->width()-170)/2,(viewport()->height()-110)/2,170,110);
        empty_->raise();
    }
    PatternEmptyState *empty_;
};
QFrame *card() { auto *frame=new QFrame; frame->setObjectName("surfaceCard"); return frame; }
QLabel *heading(const QString &text) { auto *label=new QLabel(text); label->setObjectName("sectionTitle"); return label; }
QLabel *hint(const QString &text) { auto *label=new QLabel(text); label->setObjectName("mutedText"); label->setWordWrap(true); return label; }
void prepareTable(QTableWidget *table,const QStringList &headers) {
    table->setColumnCount(headers.size()); table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(38);
    table->horizontalHeader()->setFixedHeight(34);
    table->horizontalHeader()->setStretchLastSection(true);
}
void valueCell(QTableWidget *table,int row,int column,const QString &value,bool preserve=true) {
    auto *item=new QTableWidgetItem(value); item->setToolTip(value);
    if(preserve) item->setData(UiI18n::PreserveDataRole,true);
    table->setItem(row,column,item);
}
QDialog *dialogShell(QWidget *parent,const QString &title,int width=560,int height=560) {
    if(parent && parent->window()) {
        auto *overlay=new QWidget(parent->window());
        overlay->setObjectName("dialogOverlay");
        overlay->setGeometry(parent->window()->rect());
        overlay->setStyleSheet("background: rgba(0, 0, 0, 112);");
        overlay->show(); overlay->raise();
        overlay->setProperty("patternDialogOverlay",true);
    }
    auto *dialog=new QDialog(parent); dialog->setObjectName("editorDialog");
    dialog->setWindowTitle(title); dialog->resize(width,height);
    dialog->setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
    UiModalDialog::attach(dialog,parent);
    auto *layout=new QVBoxLayout(dialog); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    auto *header=new QFrame; header->setObjectName("dialogHeader"); header->setFixedHeight(54);
    auto *headLayout=new QHBoxLayout(header); headLayout->setContentsMargins(20,0,16,0);
    auto *label=heading(title); label->setObjectName("dialogTitle"); headLayout->addWidget(label); headLayout->addStretch();
    auto *close=new QPushButton("×"); close->setObjectName("dialogClose"); headLayout->addWidget(close);
    QObject::connect(close,&QPushButton::clicked,dialog,&QDialog::reject);
    if(parent && parent->window()) {
        for(auto *overlay:parent->window()->findChildren<QWidget*>("dialogOverlay"))
            if(overlay->property("patternDialogOverlay").toBool() && !overlay->property("dialogBound").toBool()) {
                overlay->setProperty("dialogBound",true);
                QObject::connect(dialog,&QDialog::finished,overlay,[overlay] {
                    overlay->hide(); overlay->deleteLater();
                });
                break;
            }
    }
    layout->addWidget(header);
    return dialog;
}
QDialogButtonBox *dialogButtons(QDialog *dialog,const QString &acceptLabel="确定") {
    auto *footer=new QFrame; footer->setObjectName("dialogFooter");
    auto *layout=new QHBoxLayout(footer); layout->setContentsMargins(18,10,18,10); layout->addStretch();
    auto *cancel=new QPushButton("取消"), *accept=new QPushButton(acceptLabel); accept->setObjectName("primaryButton");
    layout->addWidget(cancel); layout->addWidget(accept);
    QObject::connect(cancel,&QPushButton::clicked,dialog,&QDialog::reject);
    QObject::connect(accept,&QPushButton::clicked,dialog,&QDialog::accept);
    dialog->layout()->addWidget(footer);
    return nullptr;
}
QString patternName(const QJsonObject &row) {
    QString name=row.value("name").toString().trimmed();
    if(name.isEmpty()) name=QFileInfo(row.value("fileName").toString()).completeBaseName();
    return name;
}
UiComboBox *patternTypeField(Database *db,const QString &current={}) {
    auto *box=new UiComboBox;
    box->setEditable(true); box->setPlaceholderText("可输入新类型");
    QSet<QString> types;
    for(const QJsonValue &value:db->rows("pattern_type_catalogs")) types.insert(value.toObject().value("value").toString());
    for(const QJsonValue &value:db->rows("patterns")) types.insert(value.toObject().value("patternType").toString());
    types.remove(""); QStringList sorted=types.values(); sorted.sort();
    box->addItems(sorted);
    for(int i=0;i<box->count();++i) box->setItemData(i,true,UiI18n::PreserveDataRole);
    box->setCurrentText(current);
    return box;
}
QString durationText(double hours) {
    const qint64 seconds=qMax<qint64>(0,qRound64(hours*3600.0));
    if(seconds>=3600) return QString("%1小时%2分%3秒").arg(seconds/3600).arg((seconds%3600)/60).arg(seconds%60);
    if(seconds>=60) return QString("%1分%2秒").arg(seconds/60).arg(seconds%60);
    return QString("%1秒").arg(seconds);
}
QString taskStatusLabel(const QString &status) {
    if(status=="waiting") return "等待中";
    if(status=="downloading") return "下发中";
    if(status=="uploading") return "回传中";
    if(status=="paused") return "已暂停";
    if(status=="completed") return "已完成";
    if(status=="failed") return "失败";
    if(status=="canceled") return "已取消";
    return status;
}
void showQueue(QWidget *parent,Database *db,const QString &table,
               const std::function<void(const QString&)> &feedback) {
    const bool download=table=="download_tasks";
    std::unique_ptr<QDialog> dialog(dialogShell(parent,download ? "下发队列" : "上传队列",980,640));
    auto *layout=static_cast<QVBoxLayout*>(dialog->layout());
    auto *content=new QWidget; auto *contentLayout=new QVBoxLayout(content);
    auto *actions=new QHBoxLayout; actions->addStretch();
    auto *refresh=new QPushButton("刷新"), *pause=new QPushButton("暂停"),
         *resume=new QPushButton("继续"), *cancel=new QPushButton("取消任务"),
         *clear=new QPushButton("清理已完成");
    for(auto *button:{refresh,pause,resume,cancel,clear}) actions->addWidget(button);
    pause->setObjectName("nativeTaskPause");resume->setObjectName("nativeTaskResume");cancel->setObjectName("nativeTaskCancel");clear->setObjectName("nativeTaskClear");refresh->setObjectName("nativeTaskRefresh");
    contentLayout->addLayout(actions);
    auto *overall=new QProgressBar;overall->setObjectName("nativeUploadOverallProgress");overall->setRange(0,100);overall->setVisible(!download);overall->setToolTip("处理进度包含已结束的成功或失败任务，请核对各任务结果。");contentLayout->addWidget(overall);auto *counts=new QLabel;counts->setObjectName("nativeUploadOverallCounts");counts->setProperty("i18nSkip",true);counts->setVisible(!download);contentLayout->addWidget(counts);
    auto *tasks=new QTableWidget;tasks->setObjectName("nativeTaskTable");
    prepareTable(tasks,{"序号","设备","花型/文件","状态","进度","信息","创建时间"});
    tasks->setSelectionMode(QAbstractItemView::SingleSelection);
    contentLayout->addWidget(tasks,1);
    layout->addWidget(content,1);
    dialogButtons(dialog.get(),"关闭");
    auto updateActions=[=]{const int row=tasks->currentRow();const auto task=row>=0 && tasks->item(row,0) ? db->row(table,tasks->item(row,0)->data(Qt::UserRole).toLongLong()) : QJsonObject{};const QString state=task.value("status").toString();pause->setEnabled(!task.isEmpty() && (download ? state=="waiting" || state=="downloading" : state=="waiting"));resume->setEnabled(!task.isEmpty() && (download ? state=="paused" || state=="failed" : state=="paused"));cancel->setEnabled(!task.isEmpty() && (download ? state!="completed" && state!="canceled" : state=="waiting" || state=="paused"));};QObject::connect(tasks,&QTableWidget::itemSelectionChanged,dialog.get(),updateActions);
    auto reload=[=] {
        const int selectedRow=tasks->currentRow();
        const qint64 selectedId=selectedRow>=0 && tasks->item(selectedRow,0) ?
            tasks->item(selectedRow,0)->data(Qt::UserRole).toLongLong() : 0;
        const QJsonArray rows=db->rows(table,{}, {},"id DESC",10000);
        if(!download){const auto summary=uploadTaskSummary(rows);const bool en=UiI18n::instance()->english();overall->setValue(summary.value("overallProgress").toInt());overall->setFormat((en ? QString("Task processing ") : QString("任务处理进度 "))+"%p%");counts->setText((en ? QString("Completed %1 · Failed %2 · Total %3") : QString("完成 %1 · 失败 %2 · 总数 %3")).arg(summary.value("completed").toInt()).arg(summary.value("failed").toInt()).arg(summary.value("total").toInt()));}
        bool clearable=false;for(const auto &value:rows)clearable|=download ? value.toObject().value("status")=="completed" : QStringList{"completed","failed","canceled"}.contains(value.toObject().value("status").toString());clear->setEnabled(clearable);
        tasks->setRowCount(rows.size());
        for(int row=0;row<rows.size();++row) {
            const QJsonObject task=rows[row].toObject();
            const QJsonObject device=db->row("devices",task.value("deviceId").toInteger());
            const QJsonObject pattern=db->row("patterns",task.value("patternId").toInteger());
            const QJsonObject file=db->row("device_pattern_files",task.value("deviceFileId").toInteger());
            const QJsonObject snapshot=QJsonDocument::fromJson(task.value("fileSnapshot").toString().toUtf8()).object();
            const QString fileName=download ? pattern.value("name").toString() :
                file.isEmpty() ? snapshot.value("fileName").toString() : file.value("fileName").toString();
            const QStringList values={QString::number(row+1),device.value("name").toString(),fileName,
                taskStatusLabel(task.value("status").toString()),
                QString::number(task.value("progress").toInt())+"%",
                task.value("message").toString(),task.value("createdAt").toString()};
            for(int column=0;column<values.size();++column)
                valueCell(tasks,row,column,values[column],column!=0 && column!=3 && column!=5);
            tasks->item(row,0)->setData(Qt::UserRole,task.value("id").toInteger());
            if(selectedId>0 && task.value("id").toInteger()==selectedId) tasks->setCurrentCell(row,0);
        }
        updateActions();
    };
    auto selected=[=]() -> QJsonObject {
        const int row=tasks->currentRow();
        if(row<0 || !tasks->item(row,0)) return {};
        return db->row(table,tasks->item(row,0)->data(Qt::UserRole).toLongLong());
    };
    QObject::connect(refresh,&QPushButton::clicked,dialog.get(),reload);
    QObject::connect(pause,&QPushButton::clicked,dialog.get(),[=] {
        const QJsonObject task=selected();
        if(task.isEmpty()) return;
        const QString state=task.value("status").toString();
        if(download ? (state!="waiting" && state!="downloading") : state!="waiting") return;
        db->update(table,task.value("id").toInteger(),{{"status","paused"},{"message","任务已暂停"}},nullptr);
        reload(); feedback("任务已暂停");
    });
    QObject::connect(resume,&QPushButton::clicked,dialog.get(),[=] {
        const QJsonObject task=selected();
        if(task.isEmpty()) return;
        const QString state=task.value("status").toString();
        if(download ? (state!="paused" && state!="failed") : state!="paused") return;
        db->update(table,task.value("id").toInteger(),{{"status","waiting"},{"progress",0},
            {"message",download ? "等待下发" : "等待回传"}},nullptr);
        reload(); feedback("任务已继续");
    });
    QObject::connect(cancel,&QPushButton::clicked,dialog.get(),[=] {
        const QJsonObject task=selected();
        if(task.isEmpty()) return;
        const QString state=task.value("status").toString();
        if((download && (state=="completed" || state=="canceled")) || (!download && state!="waiting" && state!="paused")) return;
        const qint64 id=task.value("id").toInteger();
        if(download) db->remove(table,id,nullptr);
        else db->update(table,id,{{"status","canceled"},{"message","任务已取消"}},nullptr);
        reload(); feedback("任务已取消");
    });
    QObject::connect(clear,&QPushButton::clicked,dialog.get(),[=] {
        if(UiMessageBox::question(tasks->window(),"确认清理",download ? "确定清理已完成的下发记录吗？" : "确定清理已完成的上传记录吗？")!=UiMessageBox::Yes)return;
        for(const QJsonValue &value:db->rows(table,{}, {},"id",10000)) {
            const QJsonObject task=value.toObject();
            const QString status=task.value("status").toString();
            if(download ? status=="completed" : QStringList{"completed","failed","canceled"}.contains(status))
                db->remove(table,task.value("id").toInteger(),nullptr);
        }
        reload(); feedback("队列已清理");
    });
    reload();
    auto *timer=new QTimer(dialog.get()); timer->setInterval(2000);
    QObject::connect(timer,&QTimer::timeout,dialog.get(),reload); timer->start();
    dialog->exec();
}
}

namespace {
QJsonObject currentNativeDeviceFile(Database *db,const QJsonObject &snapshot) {
    if(snapshot.isEmpty())return {};
    const auto current=db->rows("device_pattern_files","device_id=? AND pattern_no=? AND file_name=?",{snapshot.value("deviceId").toInteger(),snapshot.value("patternNo").toInt(),snapshot.value("fileName").toString()},"id DESC",1);
    return current.isEmpty() ? QJsonObject{} : current.first().toObject();
}
void deleteNativeDeviceFiles(Database *db,DeviceServer *devices,QWidget *parent,const QList<QJsonObject> &files,const std::function<void(const QString&)> &feedback) {
    if(files.isEmpty()){UiMessageBox::information(parent,"删除设备文件","请至少选择一个设备文件");return;}
    const bool english=UiI18n::instance()->english();const QString question=(english ? QString("Delete the %1 selected device pattern files?") : QString("确定删除选中的 %1 个设备花型文件吗？")).arg(files.size());
    if(UiMessageBox::question(parent,"删除设备文件",question)!=UiMessageBox::Yes)return;
    bool canceled=false;int completed=0;QString failure,failedFile;
    QProgressDialog progress(QString(),UiI18n::instance()->text("停止后续删除"),0,files.size(),parent);progress.setObjectName("nativeDeviceFileDeleteProgress");progress.setAutoClose(false);progress.setAutoReset(false);progress.setMinimumDuration(0);progress.setWindowTitle(UiI18n::instance()->text("删除设备文件"));UiModalDialog::attach(&progress,parent);QObject::connect(&progress,&QProgressDialog::canceled,&progress,[&]{canceled=true;});progress.show();
    for(const auto &snapshot:files){if(canceled)break;const auto file=currentNativeDeviceFile(db,snapshot);if(file.isEmpty()){failure="设备文件列表已更新，请重新读取并选择";failedFile=snapshot.value("fileName").toString();break;}const QString name=file.value("fileName").toString();progress.setLabelText((english ? QString("Deleting device files %1/%2") : QString("正在删除设备文件 %1/%2")).arg(completed+1).arg(files.size())+"\n"+name);progress.setValue(completed);
        if(!devices->deletePatternFile(file.value("deviceId").toInteger(),file.value("patternNo").toInt(),name,&failure)){failedFile=name;break;}
        const auto current=db->rows("device_pattern_files","device_id=? AND pattern_no=? AND file_name=?",{file.value("deviceId").toInteger(),file.value("patternNo").toInt(),name});for(const auto &value:current)if(!db->remove("device_pattern_files",value.toObject().value("id").toInteger(),&failure))break;
        if(!failure.isEmpty()){failedFile=name;break;}++completed;progress.setValue(completed);
    }
    progress.reset();progress.hide();QString message=(english ? QString("Deleted %1/%2 files") : QString("已删除 %1/%2 个文件")).arg(completed).arg(files.size());if(canceled)message+=(english ? "; remaining deletions stopped" : "；已停止后续删除");if(!failure.isEmpty())message+="; "+failedFile+": "+UiI18n::instance()->text(failure);if(failure.isEmpty())feedback(message);else UiMessageBox::warning(parent,"删除设备文件",message);
}
}

PatternStatsPage::PatternStatsPage(Database *db,QWidget *parent):QWidget(parent),db_(db) {
    setObjectName("page");
    auto *layout=new QVBoxLayout(this); layout->setContentsMargins(8,8,8,8); layout->setSpacing(8);
    auto *intro=card(); auto *introLayout=new QVBoxLayout(intro);
    introLayout->addWidget(heading("花型统计"));
    introLayout->addWidget(hint("查看按花型汇总的生产统计，以及逐件时长明细。"));
    layout->addWidget(intro);
    auto *filters=card(); auto *filterLayout=new QHBoxLayout(filters);
    filterLayout->addWidget(new QLabel("时间范围")); dates_=new DateRangeWidget; dates_->setFixedWidth(246);
    dates_->setRange(QDate::currentDate().addDays(-6),QDate::currentDate());
    filterLayout->addWidget(dates_); filterLayout->addWidget(new QLabel("设备"));
    device_=new UiComboBox; device_->setMinimumWidth(220); filterLayout->addWidget(device_);
    auto *search=new QPushButton("查询"), *reset=new QPushButton("重置");
    search->setObjectName("primaryButton"); filterLayout->addWidget(search); filterLayout->addWidget(reset);
    filterLayout->addStretch(); layout->addWidget(filters);
    connect(search,&QPushButton::clicked,this,&PatternStatsPage::refresh);
    connect(reset,&QPushButton::clicked,this,[this]{
        dates_->setRange(QDate::currentDate().addDays(-6),QDate::currentDate());
        device_->setCurrentIndex(0); refresh();
    });
    auto *summaryCard=card(); auto *summaryLayout=new QVBoxLayout(summaryCard);
    summaryLayout->addWidget(heading("花型生产统计表"));
    summaryLayout->addWidget(hint("按花型查看累计件数、累计时长、最近完成和最近信息。"));
    summary_=new PatternTable;
    summary_->setObjectName("patternStatsSummary");
    prepareTable(summary_,{"序号","花型编号","花型名称","累计件数","累计时长","最近完成","最近信息"});
    for(int column=0;column<6;++column)
        summary_->setColumnWidth(column,QList<int>{60,110,180,110,150,170}[column]);
    summaryLayout->addWidget(summary_); layout->addWidget(summaryCard,1);
    auto *detailCard=card(); auto *detailLayout=new QVBoxLayout(detailCard);
    detailLayout->addWidget(heading("每件时长明细表"));
    detailLayout->addWidget(hint("查看每件生产的开始时间、结束时间和单件时长。"));
    details_=new PatternTable;
    details_->setObjectName("patternStatsDetails");
    prepareTable(details_,{"序号","花型编号","花型名称","第几件","开始时间","结束时间","单件时长"});
    for(int column=0;column<6;++column)
        details_->setColumnWidth(column,QList<int>{60,110,180,100,170,170}[column]);
    detailLayout->addWidget(details_); layout->addWidget(detailCard,1);
}

void PatternStatsPage::refresh() {
    const qint64 selected=device_->currentData().toLongLong();
    const QString previous=device_->currentText();
    device_->clear(); device_->addItem("全部设备",QVariant());
    for(const QJsonValue &value:db_->rows("devices",{}, {},"name,id",10000)) {
        const QJsonObject row=value.toObject();
        device_->addItem(row.value("name").toString(),row.value("id").toInteger());
        device_->setItemData(device_->count()-1,true,UiI18n::PreserveDataRole);
    }
    const int index=device_->findData(selected);
    if(index>=0) device_->setCurrentIndex(index);
    else if(selected>0) device_->setCurrentText(previous);
    QString where="pr.deleted_at IS NULL"; QVariantList binds;
    if(dates_->startDate().isValid()) { where+=" AND substr(pr.record_date,1,10)>=?"; binds << dates_->startDate().toString("yyyy-MM-dd"); }
    if(dates_->endDate().isValid()) { where+=" AND substr(pr.record_date,1,10)<=?"; binds << dates_->endDate().toString("yyyy-MM-dd"); }
    if(selected>0) { where+=" AND pr.device_id=?"; binds << selected; }
    QSqlQuery query(db_->connection());
    query.prepare("SELECT pr.id,pr.pattern_id,pr.pattern_no,COALESCE(NULLIF(pr.pattern_name,''),p.name,'未命名花型'),"
                  "pr.pieces,pr.running_time,pr.start_time,pr.end_time,pr.created_at,"
                  "COALESCE(d.name,''),pr.protocol_user_id,pr.stop_reason,pr.record_date "
                  "FROM production_records pr LEFT JOIN patterns p ON p.id=pr.pattern_id "
                  "LEFT JOIN devices d ON d.id=pr.device_id WHERE "+where+
                  " ORDER BY COALESCE(pr.end_time,pr.start_time,pr.created_at),pr.id");
    for(const QVariant &bind:binds) query.addBindValue(bind);
    if(!query.exec()) { UiMessageBox::warning(this,"花型统计",query.lastError().text()); return; }
    struct Aggregate { QString key,name,last,recent; int number=0,pieces=0; double hours=0; };
    QVector<Aggregate> aggregate;
    QHash<QString,int> indexByKey,pieceCounter;
    QVector<QStringList> detailRows;
    while(query.next()) {
        const qint64 patternId=query.value(1).toLongLong();
        const int number=query.value(2).toInt(),pieces=query.value(4).toInt();
        const QString name=query.value(3).toString();
        const QString key=patternId>0 ? "id:"+QString::number(patternId) : "name:"+name.toLower();
        const QString started=sqlText(query.value(6)), ended=sqlText(query.value(7));
        const QDateTime startTime=QDateTime::fromString(started,"yyyy-MM-dd HH:mm:ss");
        const QDateTime endTime=QDateTime::fromString(ended,"yyyy-MM-dd HH:mm:ss");
        const double hours=startTime.isValid() && endTime.isValid() && endTime>startTime ?
            startTime.secsTo(endTime)/3600.0 : query.value(5).toDouble();
        const auto display=productionDisplayRange(sqlText(query.value(12)),sqlText(query.value(8)),
            started,ended,hours);
        const QString completed=!ended.isEmpty() ? ended : !started.isEmpty() ? started : sqlText(query.value(8));
        if(!indexByKey.contains(key)) {
            indexByKey[key]=aggregate.size(); aggregate.append({key,name,{}, {},number,0,0});
        }
        Aggregate &item=aggregate[indexByKey[key]];
        item.pieces+=pieces; item.hours+=hours;
        if(completed>item.last) {
            item.last=completed;
            item.recent=QString("设备:%1 / 工号:%2 / 停止原因:%3")
                .arg(query.value(9).toString(),query.value(10).toString()).arg(query.value(11).toInt());
        }
        pieceCounter[key]++;
        detailRows.append({QString::number(detailRows.size()+1),QString::number(number),name,
            QString::number(pieceCounter.value(key)),display.first,display.second,durationText(hours)});
    }
    std::sort(aggregate.begin(),aggregate.end(),[](const Aggregate &a,const Aggregate &b){
        return a.last==b.last ? a.name<b.name : a.last>b.last;
    });
    summary_->setRowCount(aggregate.size());
    static_cast<PatternTable*>(summary_)->showEmpty(aggregate.isEmpty());
    for(int row=0;row<aggregate.size();++row) {
        const Aggregate &item=aggregate[row];
        const QStringList values={QString::number(row+1),QString::number(item.number),item.name,
            QString::number(item.pieces),durationText(item.hours),item.last.isEmpty() ? "-" : item.last,item.recent};
        for(int column=0;column<values.size();++column) valueCell(summary_,row,column,values[column],column!=0);
    }
    details_->setRowCount(detailRows.size());
    static_cast<PatternTable*>(details_)->showEmpty(detailRows.isEmpty());
    for(int row=0;row<detailRows.size();++row)
        for(int column=0;column<detailRows[row].size();++column)
            valueCell(details_,row,column,detailRows[row][column],column!=0);
}

PatternFilesPage::PatternFilesPage(Database *db,DeviceServer *devices,
    std::function<void(const QString&)> feedback,QWidget *parent)
    :QWidget(parent),db_(db),devices_(devices),feedback_(std::move(feedback)) {
    setObjectName("page");
    auto *layout=new QVBoxLayout(this); layout->setContentsMargins(8,8,8,8); layout->setSpacing(8);
    auto *intro=card(); auto *introLayout=new QVBoxLayout(intro);
    introLayout->addWidget(heading("花型列表"));
    introLayout->addWidget(hint("维护服务器花型与设备花型，支持上传、下发、编辑、删除和设备文件回传。"));
    layout->addWidget(intro);
    auto *filters=card(); auto *filterLayout=new QHBoxLayout(filters);
    filterLayout->addWidget(new QLabel("花型名称")); keyword_=new QLineEdit;
    keyword_->setPlaceholderText("名称/文件名/类型/订单号"); keyword_->setMaximumWidth(210); filterLayout->addWidget(keyword_);
    filterLayout->addWidget(new QLabel("花型类型")); type_=new UiComboBox;
    type_->setMinimumWidth(120); filterLayout->addWidget(type_);
    filterLayout->addWidget(new QLabel("订单编号")); orderNo_=new QLineEdit;
    orderNo_->setMaximumWidth(150); filterLayout->addWidget(orderNo_);
    filterLayout->addWidget(new QLabel("上传时间")); dates_=new DateRangeWidget;
    dates_->setFixedWidth(246); filterLayout->addWidget(dates_);
    auto *search=new QPushButton("搜索"), *reset=new QPushButton("重置"); search->setObjectName("primaryButton");
    filterLayout->addWidget(search); filterLayout->addWidget(reset); filterLayout->addStretch();
    layout->addWidget(filters);
    connect(search,&QPushButton::clicked,this,[this]{ serverPage_=1; refreshServer(); });
    connect(reset,&QPushButton::clicked,this,[this] {
        keyword_->clear(); type_->setCurrentIndex(0); orderNo_->clear(); dates_->clear();
        serverPage_=1; refreshServer();
    });

    auto *serverCard=card(); auto *serverLayout=new QVBoxLayout(serverCard);
    auto *serverActions=new QHBoxLayout; serverActions->addWidget(heading("服务器花型文件")); serverActions->addStretch();
    auto *upload=new QPushButton("上传花型"), *queue=new QPushButton("下发队列"), *reload=new QPushButton("刷新");
    upload->setObjectName("primaryButton");
    for(auto *button:{upload,queue,reload}) serverActions->addWidget(button);
    serverLayout->addLayout(serverActions);
    serverLayout->addWidget(hint("服务器端花型文件管理，支持上传、编辑和删除。"));
    serverTable_=new PatternTable;
    serverTable_->setObjectName("patternServerTable");
    prepareTable(serverTable_,{"序号","花型名称","花型类型","文件名","文件大小","针数","工价","订单编号","上传时间","操作"});
    for(int column=0;column<9;++column)
        serverTable_->setColumnWidth(column,QList<int>{70,190,130,180,100,90,90,130,170}[column]);
    serverLayout->addWidget(serverTable_,1); layout->addWidget(serverCard,2);
    auto *serverFooter=new QHBoxLayout; serverTotal_=hint("共 0 条"); serverFooter->addWidget(serverTotal_);
    serverFooter->addStretch(); serverPageSize_=new UiComboBox; serverPageSize_->setObjectName("patternServerPageSize");
    for(int size:{10,20,50,100}) serverPageSize_->addItem(QString("%1条/页").arg(size),size);
    serverPageSize_->setCurrentIndex(1); serverPageSize_->setFixedWidth(100);
    serverFooter->addWidget(serverPageSize_);
    serverPrev_=new QPushButton("‹"); serverNext_=new QPushButton("›");
    serverPrev_->setObjectName("patternServerPrevious"); serverNext_->setObjectName("patternServerNext");
    serverPageLabel_=new QLabel("1 / 1");
    for(auto *button:{serverPrev_,serverNext_}) button->setFixedSize(28,28);
    serverFooter->addWidget(serverPrev_); serverFooter->addWidget(serverPageLabel_); serverFooter->addWidget(serverNext_);
    serverFooter->addWidget(new QLabel("前往")); serverJump_=new QLineEdit("1");
    serverJump_->setValidator(new QIntValidator(1,1000000,serverJump_)); serverJump_->setFixedWidth(48);
    serverJump_->setAlignment(Qt::AlignCenter); serverFooter->addWidget(serverJump_);
    serverFooter->addWidget(new QLabel("页"));
    serverLayout->addLayout(serverFooter);
    connect(serverPrev_,&QPushButton::clicked,this,[this]{ serverPage_=qMax(1,serverPage_-1); refreshServer(); });
    connect(serverNext_,&QPushButton::clicked,this,[this]{ ++serverPage_; refreshServer(); });
    connect(serverPageSize_,&QComboBox::currentIndexChanged,this,[this](int){ serverPage_=1; refreshServer(); });
    connect(serverJump_,&QLineEdit::returnPressed,this,[this] { serverPage_=serverJump_->text().toInt(); refreshServer(); });
    connect(upload,&QPushButton::clicked,this,&PatternFilesPage::uploadFile);
    connect(queue,&QPushButton::clicked,this,&PatternFilesPage::openDownloadQueue);
    connect(reload,&QPushButton::clicked,this,&PatternFilesPage::refreshServer);

    auto *deviceCard=card(); auto *deviceLayout=new QVBoxLayout(deviceCard);
    deviceLayout->addWidget(heading("设备花型文件"));
    deviceLayout->addWidget(hint("读取设备中的花型文件，支持删除设备文件和回传到服务器。"));
    auto *deviceFilters=new QHBoxLayout;
    deviceFilters->addWidget(new QLabel("设备")); deviceSelect_=new UiComboBox;
    deviceSelect_->setObjectName("patternDeviceSelect");
    deviceSelect_->setMinimumWidth(170); deviceFilters->addWidget(deviceSelect_);auto *deviceState=new QLabel;deviceState->setObjectName("nativePatternDeviceStatus");deviceFilters->addWidget(deviceState);
    deviceKeyword_=new QLineEdit; deviceKeyword_->setPlaceholderText("文件名/订单号"); deviceKeyword_->setMaximumWidth(180);
    deviceFilters->addWidget(deviceKeyword_);
    deviceType_=new UiComboBox; deviceType_->setMinimumWidth(130); deviceFilters->addWidget(deviceType_);
    auto *query=new QPushButton("查询"), *deviceReset=new QPushButton("重置");
    query->setObjectName("primaryButton"); deviceFilters->addWidget(query); deviceFilters->addWidget(deviceReset);
    deviceFilters->addStretch(); deviceLayout->addLayout(deviceFilters);
    auto *deviceActions=new QHBoxLayout;
    deviceActions->addWidget(hint("查询按钮只读取服务器缓存；需要读取设备实时列表时请点“实时读取设备”。"),1);
    auto *deviceReload=new QPushButton("实时读取设备"),
         *uploadBack=new QPushButton("回传选中文件"), *uploadQueue=new QPushButton("上传队列");
    uploadBack->setObjectName("nativeUploadDeviceFiles");deviceReload->setObjectName("nativeLiveReadDeviceFiles");
    auto *deleteSelected=new QPushButton("删除选中文件");deleteSelected->setObjectName("nativeDeleteDeviceFiles");
    for(auto *button:{deviceReload,uploadBack,uploadQueue,deleteSelected}) deviceActions->addWidget(button);
    connect(deleteSelected,&QPushButton::clicked,this,[this]{QList<QJsonObject> files;for(int row=0;row<deviceTable_->rowCount();++row){auto *check=deviceTable_->cellWidget(row,0) ? deviceTable_->cellWidget(row,0)->findChild<QCheckBox*>() : nullptr;if(check && check->isChecked() && deviceTable_->item(row,0)){auto file=deviceTable_->item(row,0)->data(Qt::UserRole+2).toJsonObject();if(!file.isEmpty())files<<file;}}deleteNativeDeviceFiles(db_,devices_,this,files,feedback_);refreshDeviceFiles();});
    deviceLayout->addLayout(deviceActions);
    deviceTable_=new PatternTable;
    deviceTable_->setObjectName("patternDeviceTable");
    prepareTable(deviceTable_,{"","序号","设备文件编号","设备文件名","花型类型","针数","文件大小","工价","订单编号","更新时间","操作"});
    for(int column=0;column<10;++column)
        deviceTable_->setColumnWidth(column,QList<int>{48,60,115,180,130,100,100,110,130,170}[column]);
    deviceTable_->horizontalHeader()->setStretchLastSection(false);
    deviceTable_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
    deviceTable_->setColumnWidth(10,90);
    deviceTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    deviceLayout->addWidget(deviceTable_,1); layout->addWidget(deviceCard,2);
    auto *deviceFooter=new QHBoxLayout; deviceTotal_=hint("共 0 条"); deviceFooter->addWidget(deviceTotal_);
    deviceFooter->addStretch(); devicePageSize_=new UiComboBox; devicePageSize_->setObjectName("patternDevicePageSize");
    for(int size:{10,20,50,100}) devicePageSize_->addItem(QString("%1条/页").arg(size),size);
    devicePageSize_->setCurrentIndex(1); devicePageSize_->setFixedWidth(100);
    deviceFooter->addWidget(devicePageSize_);
    devicePrev_=new QPushButton("‹"); deviceNext_=new QPushButton("›");
    devicePrev_->setObjectName("patternDevicePrevious"); deviceNext_->setObjectName("patternDeviceNext");
    devicePageLabel_=new QLabel("1 / 1");
    for(auto *button:{devicePrev_,deviceNext_}) button->setFixedSize(28,28);
    deviceFooter->addWidget(devicePrev_); deviceFooter->addWidget(devicePageLabel_); deviceFooter->addWidget(deviceNext_);
    deviceFooter->addWidget(new QLabel("前往")); deviceJump_=new QLineEdit("1");
    deviceJump_->setValidator(new QIntValidator(1,1000000,deviceJump_)); deviceJump_->setFixedWidth(48);
    deviceJump_->setAlignment(Qt::AlignCenter); deviceFooter->addWidget(deviceJump_);
    deviceFooter->addWidget(new QLabel("页"));
    deviceLayout->addLayout(deviceFooter);
    connect(devicePrev_,&QPushButton::clicked,this,[this]{ devicePage_=qMax(1,devicePage_-1); refreshDeviceFiles(); });
    connect(deviceNext_,&QPushButton::clicked,this,[this]{ ++devicePage_; refreshDeviceFiles(); });
    connect(devicePageSize_,&QComboBox::currentIndexChanged,this,[this](int){ devicePage_=1; refreshDeviceFiles(); });
    connect(deviceJump_,&QLineEdit::returnPressed,this,[this] { devicePage_=deviceJump_->text().toInt(); refreshDeviceFiles(); });
    connect(query,&QPushButton::clicked,this,[this]{ devicePage_=1; refreshDeviceFiles(); });
    connect(deviceReset,&QPushButton::clicked,this,[this] {
        deviceKeyword_->clear(); deviceType_->setCurrentIndex(0);
        devicePage_=1; refreshDeviceFiles();
    });
    connect(deviceSelect_,&QComboBox::currentIndexChanged,this,[this](int){ devicePage_=1; refreshDeviceFiles(); });
    connect(deviceReload,&QPushButton::clicked,this,[this] {
        const qint64 id=deviceSelect_->currentData().toLongLong();
        if(id<1) { UiMessageBox::information(this,"设备花型文件","请先选择设备"); return; }
        QString error;
        if(!devices_->refreshPatternList(id,&error)) UiMessageBox::warning(this,"刷新设备文件",error);
        else { refreshDeviceFiles(); feedback_("设备花型列表已刷新"); }
    });
    connect(uploadBack,&QPushButton::clicked,this,[this] {
        const qint64 deviceId=deviceSelect_->currentData().toLongLong();
        if(deviceId<1 || !devices_->deviceConnected(deviceId)) {
            UiMessageBox::warning(this,"回传到服务器","设备未开机连接"); return;
        }
        QList<qint64> ids;
        for(int row=0;row<deviceTable_->rowCount();++row) {
            auto *check=deviceTable_->cellWidget(row,0) ?
                deviceTable_->cellWidget(row,0)->findChild<QCheckBox*>() : nullptr;
            if(check && check->isChecked() && deviceTable_->item(row,0)) {const auto file=currentNativeDeviceFile(db_,deviceTable_->item(row,0)->data(Qt::UserRole+2).toJsonObject());if(file.isEmpty()){UiMessageBox::warning(this,"回传到服务器","设备文件列表已更新，请重新读取并选择");return;}ids.append(file.value("id").toInteger());}
        }
        if(ids.isEmpty()) { UiMessageBox::information(this,"回传到服务器","请至少选择一个设备文件"); return; }
        QHash<qint64,QJsonObject> selectedSnapshots;for(qint64 id:ids)selectedSnapshots[id]=db_->row("device_pattern_files",id);
        QString mode="ask";
        QHash<qint64,QString> renameNames;
        QJsonArray conflicts;
        for(qint64 id:ids) {
            const QJsonObject file=db_->row("device_pattern_files",id);
            const QString name=QFileInfo(file.value("fileName").toString()).completeBaseName();
            if(db_->count("patterns","name=?",{name})>0)
                conflicts.append(QJsonObject{{"fileId",id},{"fileName",file.value("fileName")},
                    {"patternName",name},{"patternNo",file.value("patternNo")}});
        }
        if(!conflicts.isEmpty()) {
            std::unique_ptr<QDialog> dialog(dialogShell(this,"同名花型处理",650,650));auto *content=new QWidget;auto *layout=new QVBoxLayout(content);layout->setContentsMargins(20,12,20,12);layout->setSpacing(8);layout->addWidget(hint("检测到服务器中已存在同名花型，请选择处理方式。"));
            auto *overwrite=new QRadioButton("覆盖同名花型"),*rename=new QRadioButton("重命名后新增");layout->addWidget(overwrite);layout->addWidget(rename);
            auto *renameBox=new QWidget;auto *renameLayout=new QVBoxLayout(renameBox);renameLayout->setContentsMargins(0,0,0,0);auto *stack=new QStackedWidget;stack->setObjectName("nativeConflictPages");auto *renameScroll=new QScrollArea;renameScroll->setWidgetResizable(true);renameScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);renameScroll->setWidget(stack);renameLayout->addWidget(renameScroll,1);QHash<qint64,QLineEdit*> fields;QHash<qint64,int> fieldPages;QSet<QString> reserved;for(const auto &value:db_->rows("patterns",{}, {},"id",10000))reserved.insert(value.toObject().value("name").toString().toCaseFolded());
            QFormLayout *form=nullptr;int row=0;for(const auto &value:conflicts){if(row%10==0){auto *page=new QWidget;form=new QFormLayout(page);form->setContentsMargins(0,0,0,0);form->setSpacing(8);stack->addWidget(page);}const auto entry=value.toObject();const auto id=entry.value("fileId").toInteger();const QString base=entry.value("patternName").toString();int suffix=1;QString candidate;do{candidate=base+QString("(%1)").arg(suffix++);}while(reserved.contains(candidate.toCaseFolded()));reserved.insert(candidate.toCaseFolded());auto *field=new QLineEdit(candidate);field->setMaxLength(255);field->setProperty("nativeConflictName",id);field->setPlaceholderText("请输入新的花型名称");const QString full=QString("[#%1] ").arg(entry.value("patternNo").toInt())+entry.value("fileName").toString();auto *label=new QLabel;label->setTextFormat(Qt::PlainText);label->setProperty("i18nSkip",true);label->setMaximumWidth(220);label->setText(label->fontMetrics().elidedText(full,Qt::ElideRight,220));label->setToolTip(full);form->addRow(label,field);fields[id]=field;fieldPages[id]=row/10;++row;}
            auto *pager=new QHBoxLayout;auto *previous=new QPushButton("‹"),*next=new QPushButton("›");previous->setObjectName("nativeConflictPrevious");next->setObjectName("nativeConflictNext");auto *pageLabel=new QLabel;pageLabel->setObjectName("nativeConflictPageLabel");pager->addStretch();pager->addWidget(previous);pager->addWidget(pageLabel);pager->addWidget(next);renameLayout->addLayout(pager);auto update=[=]{previous->setEnabled(stack->currentIndex()>0);next->setEnabled(stack->currentIndex()+1<stack->count());pageLabel->setText(QString("%1 / %2").arg(stack->currentIndex()+1).arg(stack->count()));};connect(previous,&QPushButton::clicked,dialog.get(),[=]{stack->setCurrentIndex(stack->currentIndex()-1);});connect(next,&QPushButton::clicked,dialog.get(),[=]{stack->setCurrentIndex(stack->currentIndex()+1);});connect(stack,&QStackedWidget::currentChanged,dialog.get(),[update](int){update();});update();layout->addWidget(renameBox,1);connect(rename,&QRadioButton::toggled,renameBox,&QWidget::setVisible);rename->setChecked(true);
            auto *error=new QLabel;error->setObjectName("promptError");error->setWordWrap(true);error->hide();layout->addWidget(error);for(auto it=fields.cbegin();it!=fields.cend();++it){auto *field=it.value();const auto id=it.key();connect(field,&QLineEdit::textChanged,dialog.get(),[=](const QString &text){if(!rename->isChecked())return;if(text.trimmed().isEmpty()){error->setProperty("fieldId",id);error->setText(UiI18n::instance()->text("请输入有效的新花型名称"));error->show();}else if(error->property("fieldId").toLongLong()==id)error->hide();});}connect(overwrite,&QRadioButton::toggled,dialog.get(),[=](bool checked){if(checked)error->hide();});static_cast<QVBoxLayout*>(dialog->layout())->addWidget(content,1);
            auto *footer=new QWidget;auto *buttons=new QHBoxLayout(footer);buttons->addStretch();auto *cancel=new QPushButton("取消"),*accept=new QPushButton("确定");accept->setObjectName("primaryButton");buttons->addWidget(cancel);buttons->addWidget(accept);static_cast<QVBoxLayout*>(dialog->layout())->addWidget(footer);connect(cancel,&QPushButton::clicked,dialog.get(),&QDialog::reject);connect(accept,&QPushButton::clicked,dialog.get(),[&]{QSet<QString> names;if(rename->isChecked())for(auto it=fields.cbegin();it!=fields.cend();++it){const QString name=it.value()->text().trimmed();QString invalid;if(name.isEmpty())invalid="请输入有效的新花型名称";else if(names.contains(name.toCaseFolded()) || db_->count("patterns","name=?",{name})>0)invalid="新花型名称已存在，请修改后重试";if(!invalid.isEmpty()){stack->setCurrentIndex(fieldPages.value(it.key()));renameScroll->ensureWidgetVisible(it.value());error->setText(UiI18n::instance()->text(invalid));error->show();it.value()->setFocus();return;}names.insert(name.toCaseFolded());}dialog->accept();});
            if(dialog->exec()!=QDialog::Accepted)return;mode=rename->isChecked() ? "rename" : "overwrite";if(mode=="rename")for(auto it=fields.cbegin();it!=fields.cend();++it)renameNames[it.key()]=it.value()->text().trimmed();
        }
        QList<QPair<qint64,QJsonObject>> currentFiles;for(qint64 id:ids){const auto file=currentNativeDeviceFile(db_,selectedSnapshots.value(id));if(file.isEmpty()){UiMessageBox::warning(this,"回传到服务器","设备文件列表已更新，请重新读取并选择");return;}currentFiles<<qMakePair(id,file);}
        auto connection=db_->connection();if(!connection.transaction()){UiMessageBox::warning(this,"回传到服务器","加入上传队列失败，请重试");return;}QString error;for(const auto &entry:currentFiles){const auto &file=entry.second;if(!db_->insert("upload_tasks",{{"deviceFileId",file.value("id")},{"deviceId",deviceId},{"status","waiting"},{"progress",0},{"message","等待回传"},{"conflictMode",mode},{"targetName",renameNames.value(entry.first)},{"fileSnapshot",QString::fromUtf8(QJsonDocument(file).toJson(QJsonDocument::Compact))}},nullptr,&error)){connection.rollback();UiMessageBox::warning(this,"回传到服务器","加入上传队列失败，请重试");return;}}
        if(!connection.commit()){connection.rollback();UiMessageBox::warning(this,"回传到服务器","加入上传队列失败，请重试");return;}feedback_("已加入上传队列，请点击上传队列查看进度");
    });
    connect(uploadQueue,&QPushButton::clicked,this,&PatternFilesPage::openUploadQueue);
    auto *statusTimer=new QTimer(this);statusTimer->setInterval(2000);connect(statusTimer,&QTimer::timeout,this,[this]{if(isVisible()){const qint64 id=deviceSelect_->currentData().toLongLong();const QString state=QString::number(id)+":"+QString::number(devices_->deviceConnected(id))+":"+db_->row("devices",id).value("status").toString();if(state!=deviceSelect_->property("fileDeviceState").toString())refreshDeviceFiles();}});statusTimer->start();
}

void PatternFilesPage::refresh() {
    const qint64 selected=deviceSelect_->currentData().toLongLong();
    deviceSelect_->clear(); deviceSelect_->addItem("请选择设备",QVariant());
    for(const QJsonValue &value:db_->rows("devices",{}, {},"name,id",10000)) {
        const QJsonObject row=value.toObject();
        deviceSelect_->addItem(row.value("name").toString(),row.value("id").toInteger());
        deviceSelect_->setItemData(deviceSelect_->count()-1,true,UiI18n::PreserveDataRole);
    }
    const int found=deviceSelect_->findData(selected);
    if(found>=0) deviceSelect_->setCurrentIndex(found);
    refreshServer(); refreshDeviceFiles();
}

void PatternFilesPage::refreshServer() {
    const QString currentType=type_->currentData().toString();
    const QString currentDeviceType=deviceType_->currentData().toString();
    type_->clear(); type_->addItem("全部类型",QString());
    deviceType_->clear(); deviceType_->addItem("花型类型",QString());
    QSet<QString> types;
    for(const QJsonValue &value:db_->rows("pattern_type_catalogs")) types.insert(value.toObject().value("value").toString());
    for(const QJsonValue &value:db_->rows("patterns")) types.insert(value.toObject().value("patternType").toString());
    types.remove(""); QStringList sorted=types.values(); sorted.sort();
    for(const QString &type:sorted) {
        type_->addItem(type,type);
        type_->setItemData(type_->count()-1,true,UiI18n::PreserveDataRole);
        deviceType_->addItem(type,type);
        deviceType_->setItemData(deviceType_->count()-1,true,UiI18n::PreserveDataRole);
    }
    const int selected=type_->findData(currentType);
    if(selected>=0) type_->setCurrentIndex(selected);
    const int selectedDeviceType=deviceType_->findData(currentDeviceType);
    if(selectedDeviceType>=0) deviceType_->setCurrentIndex(selectedDeviceType);
    QJsonArray visible;
    const QString keyword=keyword_->text().trimmed(),order=orderNo_->text().trimmed();
    for(const QJsonValue &value:db_->rows("patterns",{}, {},"id DESC",10000)) {
        const QJsonObject row=value.toObject();
        if(!keyword.isEmpty() && !(row.value("name").toString()+" "+
             row.value("fileName").toString()+" "+row.value("patternType").toString()+" "+
             row.value("orderNo").toString()).contains(keyword,Qt::CaseInsensitive)) continue;
        if(!currentType.isEmpty() && row.value("patternType").toString()!=currentType) continue;
        if(!order.isEmpty() && !row.value("orderNo").toString().contains(order)) continue;
        const QString date=(row.value("uploadedAt").toString().isEmpty() ?
            row.value("createdAt") : row.value("uploadedAt")).toString().left(10);
        if(dates_->startDate().isValid() && date<dates_->startDate().toString("yyyy-MM-dd")) continue;
        if(dates_->endDate().isValid() && date>dates_->endDate().toString("yyyy-MM-dd")) continue;
        visible.append(row);
    }
    const int size=serverPageSize_->currentData().toInt();
    const int pageCount=qMax(1,(visible.size()+size-1)/size);
    serverPage_=qBound(1,serverPage_,pageCount);
    const int start=(serverPage_-1)*size, shown=qMax(0,qMin(size,visible.size()-start));
    serverTable_->setRowCount(shown);
    static_cast<PatternTable*>(serverTable_)->showEmpty(visible.isEmpty());
    serverTotal_->setText(QString("共 %1 条").arg(visible.size()));
    serverPageLabel_->setText(QString("%1 / %2").arg(serverPage_).arg(pageCount));
    serverJump_->setText(QString::number(serverPage_));
    serverPrev_->setEnabled(serverPage_>1); serverNext_->setEnabled(serverPage_<pageCount);
    for(int row=0;row<shown;++row) {
        const QJsonObject item=visible[start+row].toObject();
        const QStringList values={QString::number(start+row+1),patternName(item),item.value("patternType").toString(),
            item.value("fileName").toString(),QString::number(item.value("fileSize").toInteger()),
            QString::number(item.value("stitches").toInt()),
            QString::number(item.value("unitPrice").toDouble(),'f',3),item.value("orderNo").toString(),
            item.value("uploadedAt").toString().isEmpty() ? item.value("createdAt").toString() : item.value("uploadedAt").toString()};
        for(int column=0;column<values.size();++column) valueCell(serverTable_,row,column,values[column],column!=0);
        serverTable_->item(row,0)->setData(Qt::UserRole,item.value("id").toInteger());
        auto *actions=new QWidget; auto *buttons=new QHBoxLayout(actions);
        buttons->setContentsMargins(2,1,2,1); buttons->setSpacing(3);
        auto *send=new QPushButton("下发"), *download=new QPushButton("下载"),
             *edit=new QPushButton("编辑"), *remove=new QPushButton("删除");
        for(auto *button:{send,download,edit,remove}) buttons->addWidget(button);
        serverTable_->setCellWidget(row,9,actions);
        const qint64 id=item.value("id").toInteger();
        connect(send,&QPushButton::clicked,this,[this,id]{ downloadToDevice(id); });
        connect(download,&QPushButton::clicked,this,[this,id] {
            const QJsonObject file=db_->row("patterns",id);
            const QString source=file.value("filePath").toString();
            if(!QFileInfo::exists(source)) { UiMessageBox::warning(this,"下载文件","花型文件不存在"); return; }
            QString suggested=patternName(file);
            const QString extension=QFileInfo(file.value("fileName").toString()).suffix();
            if(!extension.isEmpty() && !suggested.endsWith("."+extension,Qt::CaseInsensitive)) suggested+="."+extension;
            const QString target=QFileDialog::getSaveFileName(this,"下载文件",suggested);
            if(target.isEmpty()) return;
            QFile::remove(target);
            if(!QFile::copy(source,target)) UiMessageBox::warning(this,"下载文件","保存文件失败");
            else feedback_("文件已下载");
        });
        connect(edit,&QPushButton::clicked,this,[this,id]{ editPattern(id); });
        connect(remove,&QPushButton::clicked,this,[this,id] {
            if(UiMessageBox::question(this,"删除花型","确定删除花型吗？")!=UiMessageBox::Yes) return;
            const QString path=db_->row("patterns",id).value("filePath").toString();
            QString error;
            if(!db_->remove("patterns",id,&error)) { UiMessageBox::warning(this,"删除花型",error); return; }
            if(!path.isEmpty()) QFile::remove(path);
            refreshServer(); feedback_("花型已删除");
        });
    }
}

void PatternFilesPage::refreshDeviceFiles() {
    const qint64 deviceId=deviceSelect_->currentData().toLongLong();
    const bool connected=deviceId>0 && devices_->deviceConnected(deviceId);deviceSelect_->setProperty("fileDeviceState",QString::number(deviceId)+":"+QString::number(devices_->deviceConnected(deviceId))+":"+db_->row("devices",deviceId).value("status").toString());QSet<QString> selected;for(int row=0;row<deviceTable_->rowCount();++row){auto *check=deviceTable_->cellWidget(row,0) ? deviceTable_->cellWidget(row,0)->findChild<QCheckBox*>() : nullptr;if(connected && check && check->isChecked() && deviceTable_->item(row,0))selected.insert(deviceTable_->item(row,0)->data(Qt::UserRole+1).toString());}
    for(const QString &name:QStringList{"nativeLiveReadDeviceFiles","nativeUploadDeviceFiles","nativeDeleteDeviceFiles"})if(auto *button=findChild<QPushButton*>(name))button->setEnabled(connected);
    if(auto *status=findChild<QLabel*>("nativePatternDeviceStatus")){const QString state=!connected ? "offline" : db_->row("devices",deviceId).value("status").toString();status->setText(UiI18n::instance()->text(deviceId>0 ? state=="working" ? "缝纫" : state=="idle" ? "空闲" : "关机" : "请选择设备"));status->setStyleSheet("color:"+QString(state=="working" ? "#f56c6c" : state=="idle" ? "#67c23a" : "#909399")+";");}

    if(deviceId<1) {
        deviceTable_->setRowCount(0); static_cast<PatternTable*>(deviceTable_)->showEmpty(true);
        deviceTotal_->setText(UiI18n::instance()->text("共 0 条")); devicePageLabel_->setText("1 / 1"); deviceJump_->setText("1");
        devicePrev_->setEnabled(false); deviceNext_->setEnabled(false); return;
    }
    QJsonArray rows;
    const QString selectedType=deviceType_->currentData().toString();
    const QString keyword=deviceKeyword_->text().trimmed();
    for(const QJsonValue &value:db_->rows("device_pattern_files","device_id=?",{deviceId},"id DESC",10000)) {
        const QJsonObject row=value.toObject();
        const QString base=QFileInfo(row.value("fileName").toString()).completeBaseName();
        const QJsonArray matching=db_->rows("patterns","lower(name)=lower(?)",{base},"id DESC",1);
        const QJsonObject pattern=matching.isEmpty() ? QJsonObject{} : matching.first().toObject();
        const QString type=pattern.isEmpty() ? row.value("patternType").toString() : pattern.value("patternType").toString();
        const QString order=pattern.isEmpty() ? row.value("orderNo").toString() : pattern.value("orderNo").toString();
        if(!selectedType.isEmpty() && type!=selectedType) continue;
        if(!keyword.isEmpty() && !(row.value("fileName").toString()+" "+order).contains(keyword,Qt::CaseInsensitive)) continue;
        rows.append(row);
    }
    const int size=devicePageSize_->currentData().toInt();
    const int pageCount=qMax(1,(rows.size()+size-1)/size);
    devicePage_=qBound(1,devicePage_,pageCount);
    const int start=(devicePage_-1)*size, shown=qMax(0,qMin(size,rows.size()-start));
    deviceTable_->setRowCount(shown);
    static_cast<PatternTable*>(deviceTable_)->showEmpty(rows.isEmpty());
    deviceTotal_->setProperty("i18nSkip",true);deviceTotal_->setText(connected ? (UiI18n::instance()->english() ? QString("Total %1 records") : QString("共 %1 条")).arg(rows.size()) : (UiI18n::instance()->english() ? QString("%1 cached files (device offline, read only)") : QString("%1 条离线缓存（只读）")).arg(rows.size()));
    devicePageLabel_->setText(QString("%1 / %2").arg(devicePage_).arg(pageCount));
    deviceJump_->setText(QString::number(devicePage_));
    devicePrev_->setEnabled(devicePage_>1); deviceNext_->setEnabled(devicePage_<pageCount);
    for(int row=0;row<shown;++row) {
        const QJsonObject item=rows[start+row].toObject();
        const QString base=QFileInfo(item.value("fileName").toString()).completeBaseName();
        const QJsonArray matching=db_->rows("patterns","lower(name)=lower(?)",{base},"id DESC",1);
        const QJsonObject pattern=matching.isEmpty() ? QJsonObject{} : matching.first().toObject();
        const QString patternType=pattern.isEmpty() ? item.value("patternType").toString() :
            pattern.value("patternType").toString();
        const int stitches=pattern.isEmpty() ? item.value("stitches").toInt() : pattern.value("stitches").toInt();
        const qint64 size=item.value("fileSize").toInteger();
        const double price=pattern.isEmpty() ? item.value("unitPrice").toDouble() : pattern.value("unitPrice").toDouble();
        const QString order=pattern.isEmpty() ? item.value("orderNo").toString() : pattern.value("orderNo").toString();
        valueCell(deviceTable_,row,0,QString(),false);
        deviceTable_->item(row,0)->setData(Qt::UserRole,item.value("id").toInteger());
        const QString stableKey=QString("%1|%2|%3").arg(deviceId).arg(item.value("patternNo").toInt()).arg(item.value("fileName").toString());deviceTable_->item(row,0)->setData(Qt::UserRole+1,stableKey);deviceTable_->item(row,0)->setData(Qt::UserRole+2,QVariant::fromValue(item));
        auto *holder=new QWidget; auto *checkLayout=new QHBoxLayout(holder);
        checkLayout->setContentsMargins(0,0,0,0); checkLayout->setAlignment(Qt::AlignCenter);
        auto *check=new QCheckBox(holder); check->setFocusPolicy(Qt::NoFocus); check->setObjectName("rowCheckBox");check->setChecked(selected.contains(stableKey));check->setEnabled(connected);
        checkLayout->addWidget(check); deviceTable_->setCellWidget(row,0,holder);
        const QStringList values={QString::number(start+row+1),QString::number(item.value("patternNo").toInt()),item.value("fileName").toString(),
            patternType,QString::number(stitches),size>0 ? QString::number(size) : UiI18n::instance()->text("未知"),
            QString::number(price,'f',3),order,item.value("updatedAt").toString()};
        for(int column=0;column<values.size();++column)
            valueCell(deviceTable_,row,column+1,values[column],column!=0);
        auto *holderAction=new QWidget; auto *actionLayout=new QHBoxLayout(holderAction);
        actionLayout->setContentsMargins(4,0,4,0);
        auto *button=new QPushButton("删除"); button->setObjectName("rowTextDangerButton");
        button->setEnabled(connected);actionLayout->addWidget(button); deviceTable_->setCellWidget(row,10,holderAction);
        const qint64 fileId=item.value("id").toInteger();
        connect(button,&QPushButton::clicked,this,[this,file=item] {deleteNativeDeviceFiles(db_,devices_,this,{file},feedback_);refreshDeviceFiles();});
    }
}

void PatternFilesPage::uploadFile() {
    std::unique_ptr<QDialog> dialog(dialogShell(this,"上传花型",620,460));
    auto *content=new QWidget; auto *form=new QFormLayout(content);
    form->setContentsMargins(20,18,20,18); form->setSpacing(12);
    auto *fileRow=new QWidget; auto *fileLayout=new QHBoxLayout(fileRow);
    fileLayout->setContentsMargins(0,0,0,0);
    auto *chosen=new QLabel("未选择文件"); chosen->setWordWrap(true);
    auto *choose=new QPushButton("选择文件"); choose->setObjectName("primaryButton");
    fileLayout->addWidget(chosen,1); fileLayout->addWidget(choose);
    QString selectedPath;
    connect(choose,&QPushButton::clicked,dialog.get(),[this,chosen,&selectedPath] {
        const QString path=QFileDialog::getOpenFileName(this,"选择文件",{},
            "Pattern files (*.slw *.xdg *.nsp *.ntp *.mtp *.sdt *.vdt *.sdg *.dst)");
        if(path.isEmpty()) return;
        selectedPath=path; chosen->setText(QFileInfo(path).fileName());
    });
    auto *name=new QLineEdit, *order=new QLineEdit;
    name->setPlaceholderText("默认使用文件名"); order->setPlaceholderText("可选");
    order->setMaxLength(100);order->setValidator(new QRegularExpressionValidator(QRegularExpression("^[A-Za-z0-9]{0,100}$"),order));
    auto *type=patternTypeField(db_);
    auto *stitches=new NumericStepper; stitches->setRange(0,INT_MAX);
    auto *price=new NumericStepper(3); price->setRange(0,999999999.999);
    form->addRow("花型名称",name); form->addRow("花型类型",type);
    form->addRow("针数",stitches); form->addRow("工价",price); form->addRow("订单编号",order);
    auto *fileContent=new QWidget; auto *fileContentLayout=new QVBoxLayout(fileContent);
    fileContentLayout->setContentsMargins(0,0,0,0); fileContentLayout->setSpacing(4);
    fileContentLayout->addWidget(fileRow);
    fileContentLayout->addWidget(hint("支持选择单个花型文件，默认按文件名入库。"));
    form->addRow("选择文件",fileContent);
    connect(choose,&QPushButton::clicked,dialog.get(),[name,&selectedPath] {
        if(!selectedPath.isEmpty() && name->text().trimmed().isEmpty())
            name->setText(QFileInfo(selectedPath).completeBaseName());
    });
    static_cast<QVBoxLayout*>(dialog->layout())->addWidget(content,1);
    dialogButtons(dialog.get(),"上传");
    while(dialog->exec()==QDialog::Accepted) {
        const QFileInfo source(selectedPath);
        if(!source.isFile() || source.size()<1) {
            UiMessageBox::warning(dialog.get(),"上传花型","请选择有效的花型文件"); continue;
        }
        const QString suffix=source.suffix().toLower();
        if(!QStringList{"slw","xdg","nsp","ntp","mtp","sdt","vdt","sdg","dst"}.contains(suffix)) {
            UiMessageBox::warning(dialog.get(),"上传花型","不支持的花型文件格式"); continue;
        }
        const int stitchCount=int(stitches->value());
        const double unitPrice=price->value();
        if(name->text().trimmed().isEmpty() || name->text().trimmed().size()>255 ||
           type->currentText().trimmed().size()>100 || order->text().trimmed().size()>100) {
            UiMessageBox::warning(dialog.get(),"上传花型","请检查花型名称、针数和工价"); continue;
        }
        if(!order->text().isEmpty() && !QRegularExpression("^[A-Za-z0-9]+$").match(order->text()).hasMatch()) {UiMessageBox::warning(dialog.get(),"上传花型","订单编号只允许字母和数字");continue;}
        const QString directory=QDir(db_->dataDirectory()).filePath("uploads/patterns");
        if(!QDir().mkpath(directory)) { UiMessageBox::warning(dialog.get(),"上传花型","创建目录失败"); continue; }
        const QString filename=QDateTime::currentDateTime().toString("yyyyMMddHHmmsszzz")+"_"+ 
            QString::number(QRandomGenerator::global()->generate())+"_"+source.fileName();
        const QString path=QDir(directory).filePath(filename);
        if(!QFile::copy(selectedPath,path)) { UiMessageBox::warning(dialog.get(),"上传花型","复制文件失败"); continue; }
        qint64 id=0; QString error;
        if(!db_->insert("patterns",{{"name",name->text().trimmed()},{"patternType",type->currentText().trimmed()},
            {"fileName",filename},{"filePath",path},{"fileSize",source.size()},{"stitches",stitchCount},
            {"unitPrice",qRound64(unitPrice*1000.0)/1000.0},{"orderNo",order->text()},
            {"uploadedBy",1},{"uploadedAt",QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")}},&id,&error)) {
            QFile::remove(path); UiMessageBox::warning(dialog.get(),"上传花型",error); continue;
        }
        if(!type->currentText().trimmed().isEmpty() &&
           db_->rows("pattern_type_catalogs","value=?",{type->currentText().trimmed()},"id",1).isEmpty())
            db_->insert("pattern_type_catalogs",{{"value",type->currentText().trimmed()}},nullptr,nullptr);
        if(!order->text().trimmed().isEmpty() &&
           db_->rows("order_no_catalogs","value=?",{order->text().trimmed()},"id",1).isEmpty())
            db_->insert("order_no_catalogs",{{"value",order->text().trimmed()}},nullptr,nullptr);
        refreshServer(); feedback_("花型上传成功"); return;
    }
}

void PatternFilesPage::editPattern(qint64 id) {
    const QJsonObject original=db_->row("patterns",id);
    if(original.isEmpty()) return;
    std::unique_ptr<QDialog> dialog(dialogShell(this,"编辑花型",560,420));
    auto *content=new QWidget; auto *form=new QFormLayout(content);
    form->setContentsMargins(20,18,20,18); form->setSpacing(12);
    auto *name=new QLineEdit(original.value("name").toString());
    auto *type=patternTypeField(db_,original.value("patternType").toString());
    auto *stitches=new NumericStepper;
    stitches->setRange(0,INT_MAX); stitches->setValue(original.value("stitches").toInt());
    auto *price=new NumericStepper(3);
    price->setRange(0,999999999.999); price->setValue(original.value("unitPrice").toDouble());
    auto *order=new QLineEdit(original.value("orderNo").toString());
    order->setMaxLength(100);order->setValidator(new QRegularExpressionValidator(QRegularExpression("^[A-Za-z0-9]{0,100}$"),order));
    order->setPlaceholderText("可选");
    form->addRow("花型名称",name); form->addRow("花型类型",type);
    form->addRow("针数",stitches); form->addRow("工价",price); form->addRow("订单编号",order);
    static_cast<QVBoxLayout*>(dialog->layout())->addWidget(content,1); dialogButtons(dialog.get(),"保存");
    while(dialog->exec()==QDialog::Accepted) {
        const int count=int(stitches->value());
        const double amount=price->value();
        if(name->text().trimmed().isEmpty() || name->text().trimmed().size()>255 ||
           type->currentText().trimmed().size()>100 || order->text().trimmed().size()>100) {
            UiMessageBox::warning(dialog.get(),"编辑花型","请检查花型名称、针数和工价"); continue;
        }
        QString error;
        if(!db_->update("patterns",id,{{"name",name->text().trimmed()},
            {"patternType",type->currentText().trimmed()},{"stitches",count},
            {"unitPrice",qRound64(amount*1000.0)/1000.0},{"orderNo",order->text()}},&error)) {
            UiMessageBox::warning(dialog.get(),"编辑花型",error); continue;
        }
        refreshServer(); feedback_("花型已更新"); return;
    }
}

void PatternFilesPage::downloadToDevice(qint64 patternId) {
    const QJsonObject pattern=db_->row("patterns",patternId);
    if(pattern.isEmpty() || !QFileInfo::exists(pattern.value("filePath").toString())) {
        UiMessageBox::warning(this,"下发花型","花型文件不存在"); return;
    }
    if(patternName(pattern).size()*2>44) {
        UiMessageBox::warning(this,"下发花型","花型名称超过设备协议44字节限制"); return;
    }
    std::unique_ptr<QDialog> dialog(dialogShell(this,"下发花型",480,520));
    auto *content=new QWidget; auto *layout=new QVBoxLayout(content);
    layout->setContentsMargins(20,18,20,18);
    layout->addWidget(hint("请选择目标设备，未连接的设备会在队列中等待开机。"));
    auto *list=new QListWidget;
    for(const QJsonValue &value:db_->rows("devices",{}, {},"name,id",10000)) {
        const QJsonObject device=value.toObject();
        auto *item=new QListWidgetItem(device.value("name").toString(),list);
        item->setData(Qt::UserRole,device.value("id").toInteger());
        item->setData(UiI18n::PreserveDataRole,true);
        item->setFlags(item->flags()|Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    layout->addWidget(list,1);
    static_cast<QVBoxLayout*>(dialog->layout())->addWidget(content,1);
    dialogButtons(dialog.get(),"开始下发");
    while(dialog->exec()==QDialog::Accepted) {
        QList<qint64> selected;
        for(int i=0;i<list->count();++i)
            if(list->item(i)->checkState()==Qt::Checked) selected << list->item(i)->data(Qt::UserRole).toLongLong();
        if(selected.isEmpty()) { UiMessageBox::warning(dialog.get(),"下发花型","请至少选择一台设备"); continue; }
        QSqlDatabase connection=db_->connection();
        if(!connection.transaction()) { UiMessageBox::warning(dialog.get(),"下发花型",connection.lastError().text()); continue; }
        QString error;
        for(qint64 deviceId:selected)
            if(!db_->insert("download_tasks",{{"patternId",patternId},{"deviceId",deviceId},
                {"status","waiting"},{"progress",0},{"message","等待下发"},{"operatorId",1}},nullptr,&error)) break;
        if(!error.isEmpty() || !connection.commit()) {
            connection.rollback(); UiMessageBox::warning(dialog.get(),"下发花型",error.isEmpty() ? "创建队列失败" : error); continue;
        }
        feedback_("已加入下发队列"); return;
    }
}

void PatternFilesPage::openDownloadQueue() { showQueue(this,db_,"download_tasks",feedback_); refreshServer(); }
void PatternFilesPage::openUploadQueue() { showQueue(this,db_,"upload_tasks",feedback_); refreshServer(); }
