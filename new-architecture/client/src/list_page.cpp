#include "list_page.h"
#include "api_client.h"
#include "client_window.h"
#include "ui_controls.h"
#include "ui_i18n.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QWidget *filterPair(const QString &label,QWidget *control) {
    auto *pair=new QWidget; auto *layout=new QHBoxLayout(pair);
    layout->setContentsMargins(0,0,0,0); layout->setSpacing(7);
    if(!label.isEmpty()) layout->addWidget(new QLabel(label));
    layout->addWidget(control); return pair;
}
}

ListPage::ListPage(ApiClient *api,ClientWindow *window,const ListSpec &spec,QWidget *parent)
    :ClientPage(parent),api_(api),window_(window),spec_(spec) {
    setObjectName("clientPage");
    auto *layout=new QVBoxLayout(this); layout->setContentsMargins(9,9,9,9); layout->setSpacing(9);
    if(spec.showHeader) layout->addWidget(clientPageHeader(spec.title,spec.description));
    auto *filters=new QFrame; filters->setObjectName("clientFilter");
    auto *flow=new UiFlowLayout(filters,8,7); flow->setContentsMargins(12,9,12,9);
    search_=new QLineEdit; search_->setPlaceholderText(spec.searchHint);search_->setToolTip(spec.searchHelp.isEmpty() ? spec.searchHint : spec.searchHelp); search_->setFixedWidth(205);
    flow->addWidget(filterPair("关键词",search_));
    for(const auto &extra:spec.extraFilters) {
        auto *input=new QLineEdit; input->setPlaceholderText(extra.label); input->setFixedWidth(extra.width);
        additional_[extra.key]=input; flow->addWidget(filterPair(extra.label,input));
        connect(input,&QLineEdit::returnPressed,this,[this]{ pagination_->reset(); refresh(); });
    }
    auto addDate=[this,flow,&spec] {
        if(!spec.dates) return;
        dates_=new DateRangeWidget; dates_->setFixedWidth(spec.dateWidth);
        flow->addWidget(filterPair("时间范围",dates_));
    };
    if(spec.dateBeforeOption) addDate();
    if(!spec.optionLabel.isEmpty()) {
        option_=new UiComboBox; option_->setObjectName("clientFilterCombo");
        option_->setMinimumWidth(165); option_->setMaximumWidth(240);
        for(const auto &entry:spec.options) option_->addItem(entry.first,entry.second);
        optionKey_=spec.optionKey;
        flow->addWidget(filterPair(spec.optionLabel,option_));
    }
    if(!spec.secondaryLabel.isEmpty()) {
        secondary_=new QLineEdit; secondary_->setPlaceholderText(spec.secondaryLabel);
        secondary_->setFixedWidth(145);
        flow->addWidget(filterPair(spec.secondaryLabel,secondary_));
    }
    if(!spec.dateBeforeOption) addDate();
    auto *buttons=new QWidget; auto *buttonRow=new QHBoxLayout(buttons);
    buttonRow->setContentsMargins(0,0,0,0); buttonRow->setSpacing(7);
    auto *searchButton=new QPushButton("搜索"), *reset=new QPushButton("重置");
    searchButton->setObjectName("primaryButton"); buttonRow->addWidget(searchButton); buttonRow->addWidget(reset);
    flow->addWidget(buttons);
    layout->addWidget(filters); filters->setVisible(spec.showFilters);
    connect(searchButton,&QPushButton::clicked,this,[this] { pagination_->reset(); refresh(); });
    connect(search_,&QLineEdit::returnPressed,this,[this] { pagination_->reset(); refresh(); });
    connect(reset,&QPushButton::clicked,this,[this] {
        extra_.clear();
        search_->clear(); if(option_) option_->setCurrentIndex(0);
        if(secondary_) secondary_->clear();
        for(auto *input:additional_) input->clear();
        if(dates_) dates_->clear(); pagination_->reset(); refresh();
    });
    auto *card=clientCard(); auto *cardLayout=new QVBoxLayout(card);
    cardLayout->setContentsMargins(10,10,10,10); cardLayout->setSpacing(8);
    toolbar_=new UiFlowLayout(nullptr,7,7); toolbar_->addWidget(clientHeading(spec.title));
    cardLayout->addLayout(toolbar_);
    table_=new ClientTable; table_->setColumns(spec.columns,spec.selection,spec.actions,
        [this](const QJsonObject &row,const QString &action) { if(action_) action_(this,row,action); });
    cardLayout->addWidget(table_,1);
    pagination_=new ClientPagination; cardLayout->addWidget(pagination_);
    layout->addWidget(card,1);
    connect(pagination_,&ClientPagination::changed,this,&ListPage::refresh);
    if(spec.refreshSeconds>0) {
        auto *timer=new QTimer(this); timer->setInterval(spec.refreshSeconds*1000);
        connect(timer,&QTimer::timeout,this,[this] {
            QWidget *modal=QApplication::activeModalWidget();
            const bool ownModal=modal && (modal==this->window() || modal->isAncestorOf(this));
            if(isVisible() && (!modal || ownModal) && table_->checkedIds().isEmpty()) refresh();
        });
        timer->start();
    }
}

QPushButton *ListPage::addToolbarButton(const QString &label,std::function<void()> callback,bool primary) {
    auto *button=new QPushButton(label);
    if(primary) button->setObjectName("primaryButton");
    toolbar_->addWidget(button);
    connect(button,&QPushButton::clicked,this,std::move(callback));
    return button;
}

void ListPage::setExtraQuery(const QString &key,const QString &value) {
    extra_.clear(); localFilter_={};
    search_->clear(); if(secondary_) secondary_->clear();
    for(auto *input:additional_) input->clear();
    if(option_) option_->setCurrentIndex(0);
    if(dates_) dates_->clear();
    extra_[key]=value; pagination_->reset(); refresh();
}

void ListPage::setOptionChoices(const QList<QPair<QString,QVariant>> &choices) {
    if(!option_) return;
    const QVariant selected=option_->currentData();
    option_->clear();
    for(const auto &choice:choices) {
        option_->addItem(choice.first,choice.second);
        if(!choice.second.isNull() && !choice.second.toString().isEmpty())
            option_->setItemData(option_->count()-1,true,UiI18n::PreserveDataRole);
    }
    const int index=option_->findData(selected);
    if(index>=0) option_->setCurrentIndex(index);
}

QString ListPage::keyword() const { return search_->text().trimmed(); }

QMap<QString,QString> ListPage::query() const {
    QMap<QString,QString> params=extra_;
    if(!keyword().isEmpty()) params[spec_.searchKey]=keyword();
    if(secondary_ && !secondary_->text().trimmed().isEmpty())
        params[spec_.secondaryKey]=secondary_->text().trimmed();
    for(auto it=additional_.cbegin();it!=additional_.cend();++it)
        if(!it.value()->text().trimmed().isEmpty()) params[it.key()]=it.value()->text().trimmed();
    if(option_ && !option_->currentData().toString().isEmpty())
        params[optionKey_]=option_->currentData().toString();
    if(dates_) {
        if(dates_->startDate().isValid()) params[spec_.dateKey]=dates_->startDate().toString("yyyy-MM-dd");
        if(dates_->endDate().isValid()) params["endDate"]=dates_->endDate().toString("yyyy-MM-dd");
    }
    return params;
}

void ListPage::refresh() {
    if(api_->token()=="preview-token") { table_->setRecords({}); pagination_->setTotal(0); return; }
    QMap<QString,QString> params=query();
    if(spec_.serverPaged) {
        params["page"]=QString::number(pagination_->page());
        params["pageSize"]=QString::number(pagination_->pageSize());
    }
    const quint64 generation=++requestGeneration_;table_->setEnabled(false);
    QString endpoint=spec_.path;
    const QString suffix=requestQuery(params);
    if(!suffix.isEmpty()) endpoint+=(endpoint.contains('?') ? "&"+suffix.mid(1) : suffix);
    api_->get(endpoint,[this,generation](const ApiResponse &response) {
        if(generation!=requestGeneration_) return;
        table_->setEnabled(true);
        if(!response.ok) { window_->notify(response.error,true); return; }
        if(responseLoaded_)responseLoaded_(response);
        rows_=apiRows(response.data);
        if(localFilter_) {
            QJsonArray filtered;
            for(const QJsonValue &value:rows_) if(localFilter_(value.toObject())) filtered.append(value);
            rows_=filtered;
        }
        if(loaded_) loaded_(rows_);
        if(spec_.serverPaged) {
            pagination_->setTotal(response.data.toObject().value("total").toInt(rows_.size()));
            table_->setRecords(rows_,(pagination_->page()-1)*pagination_->pageSize());
        } else {
            pagination_->setTotal(rows_.size());
            const int start=(pagination_->page()-1)*pagination_->pageSize();
            QJsonArray page;
            for(int i=start;i<qMin(start+pagination_->pageSize(),rows_.size());++i) page.append(rows_[i]);
            table_->setRecords(page,start);
        }
    });
}
