#include "ui_i18n.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QScreen>
#include <QTreeWidget>
#include <QWidget>

namespace {
UiI18n *current=nullptr;
constexpr int originalRole=Qt::UserRole+120;
constexpr int renderedRole=Qt::UserRole+121;

void loadWords(QHash<QString,QString> &words,const QString &path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject values=QJsonDocument::fromJson(file.readAll()).object();
    for(auto it=values.begin();it!=values.end();++it) words.insert(it.key(),it.value().toString());
}

template<typename Setter>
void updateWidgetText(QWidget *widget,const char *name,const QString &currentText,const UiI18n *i18n,Setter setter) {
    const QByteArray originalKey=QByteArray("_i18n_original_")+name;
    const QByteArray renderedKey=QByteArray("_i18n_rendered_")+name;
    QString original=widget->property(originalKey.constData()).toString();
    const QString rendered=widget->property(renderedKey.constData()).toString();
    if(original.isEmpty() || currentText!=rendered) original=currentText;
    const QString target=i18n->text(original);
    widget->setProperty(originalKey.constData(),original);
    widget->setProperty(renderedKey.constData(),target);
    if(currentText!=target) setter(target);
}

template<typename Item,typename Getter,typename Setter>
void updateItem(Item *item,Getter getter,Setter setter,const UiI18n *i18n) {
    if(item->data(UiI18n::PreserveDataRole).toBool()) return;
    const QString currentText=getter();
    QString original=item->data(originalRole).toString();
    const QString rendered=item->data(renderedRole).toString();
    if(original.isEmpty() || currentText!=rendered) original=currentText;
    const QString target=i18n->text(original);
    item->setData(originalRole,original);
    item->setData(renderedRole,target);
    if(currentText!=target) setter(target);
}
}

UiI18n *UiI18n::instance() { return current; }

UiI18n::UiI18n(QApplication *app):QObject(app) {
    current=this;
    loadWords(words_,":/i18n-en.json");
    loadWords(words_,":/i18n-qt-en.json");
    loadWords(words_,":/client-i18n-en.json");
    app->installEventFilter(this);
}

void UiI18n::setEnglish(bool english) {
    if(english_==english) { refresh(); return; }
    english_=english;
    QLocale::setDefault(english ? QLocale(QLocale::English,QLocale::UnitedStates) : QLocale(QLocale::Chinese,QLocale::China));
    refresh();
    emit languageChanged();
}

QString UiI18n::text(const QString &source) const {
    if(!english_ || source.isEmpty()) return source;
    const QString raw=source.trimmed();
    QString translated=words_.value(raw);
    if(translated.isEmpty() && raw.contains("</span>")) {
        const int end=raw.lastIndexOf("</span>")+7;
        translated=raw.left(end)+text(raw.mid(end));
    }
    if(translated.isEmpty()) {
        static const QRegularExpression count("^共 (\\d+) 条$");
        static const QRegularExpression pages("^(\\d+)条/页$");
        static const QRegularExpression current("^当前范围：(.+)，共 (\\d+) 个账号$");
        static const QRegularExpression devices("^当前范围：(.+)，共 (\\d+) 台设备，未分组设备会以浅红底提示。$");
        static const QRegularExpression month("^(\\d+) 年 (\\d+) 月$");
        static const QRegularExpression summary("^HTTP API 端口 (\\d+)\\s*·\\s*设备 TCP 端口 (\\d+)\\s*·\\s*最近刷新 (.+)$");
        static const QRegularExpression managementPort("^管理端口 (\\d+)$");
        static const QRegularExpression selectedDevices("^确定删除选中的 (\\d+) 台设备吗？$");
        static const QRegularExpression transferProgress("^(正在下发|正在回传) (\\d+)/(\\d+)$");
        static const QRegularExpression uploadRejected("^设备拒绝或中止回传，状态(\\d+)$");
        static const QRegularExpression frameRejected("^设备拒绝第(\\d+)帧$");
        static const QRegularExpression numbered("^(\\d+)\\. (.+)$");
        static const QRegularExpression lastRefreshed("^最后刷新 (.+)$");
        static const QRegularExpression hoursMinutes("^(\\d+)小时(\\d+)分$");
        static const QRegularExpression enterField("^请(?:填写|输入)(.+)$");
        static const QRegularExpression uploadCurrent("^正在上传第 (\\d+)/(\\d+) 个文件$");
        static const QRegularExpression uploadDone("^已上传 (\\d+) 个花型文件$");
        static const QRegularExpression selectedCatalog("^确定清理选中的 (\\d+) 个条目吗？关联数据会保留。$");
        auto match=count.match(raw);
        if(match.hasMatch()) translated=QString("Total %1 records").arg(match.captured(1));
        else if((match=pages.match(raw)).hasMatch()) translated=QString("%1/page").arg(match.captured(1));
        else if((match=current.match(raw)).hasMatch()) translated=QString("Current scope: %1, %2 accounts").arg(text(match.captured(1)),match.captured(2));
        else if((match=devices.match(raw)).hasMatch()) translated=QString("Current scope: %1, %2 devices. Ungrouped devices are highlighted in light red.").arg(text(match.captured(1)),match.captured(2));
        else if((match=month.match(raw)).hasMatch()) translated=QString("%1 / %2").arg(match.captured(1),match.captured(2));
        else if((match=summary.match(raw)).hasMatch()) translated=QString("HTTP API Port %1  ·  Device TCP Port %2  ·  Last refreshed %3").arg(match.captured(1),match.captured(2),match.captured(3));
        else if((match=managementPort.match(raw)).hasMatch()) translated=QString("Management Port %1").arg(match.captured(1));
        else if((match=selectedDevices.match(raw)).hasMatch()) translated=QString("Delete the selected %1 devices?").arg(match.captured(1));
        else if((match=transferProgress.match(raw)).hasMatch())
            translated=QString("%1 %2/%3")
                .arg(match.captured(1)=="正在下发" ? "Downloading" : "Uploading",
                     match.captured(2),match.captured(3));
        else if((match=uploadRejected.match(raw)).hasMatch())translated=QString("Device refused or stopped upload (status %1)").arg(match.captured(1));
        else if((match=frameRejected.match(raw)).hasMatch())translated=QString("Device refused frame %1").arg(match.captured(1));
        else if((match=numbered.match(raw)).hasMatch())
            translated=match.captured(1)+". "+text(match.captured(2));
        else if((match=lastRefreshed.match(raw)).hasMatch())
            translated="Last refreshed "+match.captured(1);
        else if((match=hoursMinutes.match(raw)).hasMatch())
            translated=QString("%1h %2m").arg(match.captured(1),match.captured(2));
        else if((match=enterField.match(raw)).hasMatch()) translated="Please enter "+text(match.captured(1));
        else if((match=uploadCurrent.match(raw)).hasMatch())
            translated=QString("Uploading file %1/%2").arg(match.captured(1),match.captured(2));
        else if((match=uploadDone.match(raw)).hasMatch())
            translated=QString("Uploaded %1 pattern files").arg(match.captured(1));
        else if((match=selectedCatalog.match(raw)).hasMatch())
            translated=QString("Clear the %1 selected entries? Related data will remain.").arg(match.captured(1));
        else if(raw.startsWith("当前选择：")) translated="Current selection: "+text(raw.mid(5));
        else if(raw.startsWith("删除设备文件失败: "))translated="Failed to delete device file: "+text(raw.mid(QString("删除设备文件失败: ").size()));
        else if(raw.startsWith("✓")) translated="✓  "+text(raw.mid(1).trimmed());
        else {
            const QList<QPair<QString,QString>> prefixes={
                {"版本：","Version: "},{"设备 TCP 端口：","Device TCP Port: "},{"系统环境：","System Environment: "},
                {"工作目录：","Working Directory: "},{"数据目录：","Data Directory: "},
                {"数据文件：","Data File: "},{"数据库类型：","Database Type: "},
                {"设备 IP：","Device IP: "},{"设备名称：","Device Name: "}
            };
            for(const auto &entry:prefixes) if(raw.startsWith(entry.first)) { translated=entry.second+raw.mid(entry.first.size()); break; }
        }
    }
    if(translated.isEmpty()) return source;
    QString result=source;
    result.replace(raw,translated);
    return result;
}

QString UiI18n::sourceText(const QComboBox *combo) const {
    if(!combo || combo->currentIndex()<0) return combo ? combo->currentText() : QString();
    if(combo->isEditable() && combo->currentText()!=combo->itemText(combo->currentIndex())) return combo->currentText();
    const QString original=combo->itemData(combo->currentIndex(),originalRole).toString();
    return original.isEmpty() ? combo->currentText() : original;
}

void UiI18n::refresh() {
    if(refreshing_) return;
    refreshing_=true;
    for(QWidget *widget:QApplication::allWidgets()) refreshWidget(widget);
    refreshing_=false;
}

void UiI18n::refreshWidget(QWidget *widget) {
    if(widget->property("i18nSkip").toBool()) return;
    if(!widget->windowTitle().isEmpty())
        updateWidgetText(widget,"windowTitle",widget->windowTitle(),this,[widget](const QString &value){ widget->setWindowTitle(value); });
    const auto *tooltipCombo=qobject_cast<QComboBox*>(widget);const bool businessTooltip=tooltipCombo && (tooltipCombo->itemData(tooltipCombo->currentIndex(),PreserveDataRole).toBool() || (tooltipCombo->isEditable() && tooltipCombo->currentText()!=tooltipCombo->itemText(tooltipCombo->currentIndex())));
    if(!widget->toolTip().isEmpty() && !businessTooltip)
        updateWidgetText(widget,"toolTip",widget->toolTip(),this,[widget](const QString &value){ widget->setToolTip(value); });
    if(auto *label=qobject_cast<QLabel*>(widget)) {
        if(label->property("i18nDataKind").isValid()) {
            const QString kind=label->property("i18nDataKind").toString();
            const QString name=label->property("i18nDataName").toString();
            const int count=label->property("i18nDataCount").toInt();
            QString target;
            if(kind=="selection") target=(english_ ? "Current selection: " : "当前选择：")+name;
            else if(kind=="accounts") target=english_ ? QString("Current scope: %1, %2 accounts").arg(name).arg(count)
                                                         : QString("当前范围：%1，共 %2 个账号").arg(name).arg(count);
            else if(kind=="devices") target=english_ ? QString("Current scope: %1, %2 devices. Ungrouped devices are highlighted in light red.").arg(name).arg(count)
                                                        : QString("当前范围：%1，共 %2 台设备，未分组设备会以浅红底提示。").arg(name).arg(count);
            if(!target.isEmpty() && label->text()!=target) label->setText(target);
        } else updateWidgetText(widget,"text",label->text(),this,[label](const QString &value){ label->setText(value); });
        if(label->objectName().startsWith("statusPill"))
            label->setFixedWidth(qMax(48,label->fontMetrics().horizontalAdvance(label->text())+20));
    }
    if(auto *button=qobject_cast<QAbstractButton*>(widget))
        updateWidgetText(widget,"text",button->text(),this,[button](const QString &value){ button->setText(value); });
    if(auto *line=qobject_cast<QLineEdit*>(widget))
        updateWidgetText(widget,"placeholder",line->placeholderText(),this,[line](const QString &value){ line->setPlaceholderText(value); });
    if(auto *edit=qobject_cast<QTextEdit*>(widget))
        updateWidgetText(widget,"placeholder",edit->placeholderText(),this,[edit](const QString &value){ edit->setPlaceholderText(value); });
    if(auto *edit=qobject_cast<QPlainTextEdit*>(widget))
        updateWidgetText(widget,"placeholder",edit->placeholderText(),this,[edit](const QString &value){ edit->setPlaceholderText(value); });
    if(auto *box=qobject_cast<QGroupBox*>(widget))
        updateWidgetText(widget,"title",box->title(),this,[box](const QString &value){ box->setTitle(value); });
    if(auto *combo=qobject_cast<QComboBox*>(widget)) {
        QSignalBlocker blocker(combo);
        for(int i=0;i<combo->count();++i) {
            if(combo->itemData(i,PreserveDataRole).toBool()) continue;
            const QString currentText=combo->itemText(i);
            QString original=combo->itemData(i,originalRole).toString();
            const QString rendered=combo->itemData(i,renderedRole).toString();
            if(original.isEmpty() || currentText!=rendered) original=currentText;
            const QString target=text(original);
            combo->setItemData(i,original,originalRole);
            combo->setItemData(i,target,renderedRole);
            if(currentText!=target) combo->setItemText(i,target);
        }
    }
    if(auto *list=qobject_cast<QListWidget*>(widget)) {
        QSignalBlocker blocker(list);
        for(int i=0;i<list->count();++i) if(auto *item=list->item(i))
            updateItem(item,[item]{ return item->text(); },[item](const QString &value){ item->setText(value); },this);
    }
    if(auto *tree=qobject_cast<QTreeWidget*>(widget)) {
        QSignalBlocker blocker(tree);
        for(QTreeWidgetItemIterator it(tree);*it;++it) for(int column=0;column<tree->columnCount();++column) {
            auto *item=*it; const QString currentText=item->text(column);
            if(item->data(column,PreserveDataRole).toBool()) continue;
            QString original=item->data(column,originalRole).toString();
            const QString rendered=item->data(column,renderedRole).toString();
            if(original.isEmpty() || currentText!=rendered) original=currentText;
            const QString target=text(original);
            item->setData(column,originalRole,original); item->setData(column,renderedRole,target);
            if(currentText!=target) item->setText(column,target);
        }
    }
    if(auto *table=qobject_cast<QTableWidget*>(widget)) {
        QSignalBlocker blocker(table);
        for(int column=0;column<table->columnCount();++column) {
            if(auto *item=table->horizontalHeaderItem(column))
                updateItem(item,[item]{ return item->text(); },[item](const QString &value){ item->setText(value); },this);
            for(int row=0;row<table->rowCount();++row) if(auto *item=table->item(row,column))
                updateItem(item,[item]{ return item->text(); },[item](const QString &value){ item->setText(value); },this);
        }
    }
    if(auto *menu=qobject_cast<QMenu*>(widget)) for(QAction *action:menu->actions()) {
        const QString currentText=action->text();
        QString original=action->property("_i18n_original").toString();
        const QString rendered=action->property("_i18n_rendered").toString();
        if(original.isEmpty() || currentText!=rendered) original=currentText;
        const QString target=text(original);
        action->setProperty("_i18n_original",original); action->setProperty("_i18n_rendered",target);
        if(currentText!=target) action->setText(target);
    }
}

bool UiI18n::eventFilter(QObject *watched,QEvent *event) {
    if(event->type()==QEvent::Show)if(auto *tip=qobject_cast<QLabel*>(watched);tip && tip->objectName()=="qtooltip_label") {
        // Bound long business tooltips without changing their stored text.
        tip->setProperty("i18nSkip",true);tip->setWordWrap(true);
        if(auto *screen=tip->screen()){const QRect area=screen->availableGeometry().adjusted(8,8,-8,-8);tip->setMaximumSize(qMin(520,area.width()),area.height());tip->adjustSize();tip->move(qBound(area.left(),tip->x(),area.right()-tip->width()+1),qBound(area.top(),tip->y(),area.bottom()-tip->height()+1));}
    }
    if(event->type()==QEvent::Show && qobject_cast<QWidget*>(watched) && !refreshing_ && !scheduled_) {
        scheduled_=true;
        QTimer::singleShot(0,this,[this]{ scheduled_=false; refresh(); });
    }
    return false;
}
