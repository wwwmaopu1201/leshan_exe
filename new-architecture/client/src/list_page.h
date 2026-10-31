#pragma once

#include "pages.h"
#include "page_widgets.h"
#include <QMap>

class ApiClient;
struct ApiResponse;
class ClientWindow;
class DateRangeWidget;
class QLineEdit;
class QComboBox;
class QHBoxLayout;
class QLayout;
class QTimer;
class QPushButton;

struct ListSpec {
    struct ExtraFilter { QString label,key; int width=150; };
    QString title,description,path,searchHint="搜索",searchHelp,searchKey="keyword",dateKey="startDate";
    QList<TableColumn> columns;
    QStringList actions;
    QString optionLabel,optionKey="status";
    QString secondaryLabel,secondaryKey;
    QList<QPair<QString,QString>> options;
    QList<ExtraFilter> extraFilters;
    bool selection=false,serverPaged=true,dates=false;
    bool showHeader=true;
    bool showFilters=true;
    int dateWidth=245;
    bool dateBeforeOption=false;
    int refreshSeconds=0;
};

class ListPage : public ClientPage {
public:
    using Action=std::function<void(ListPage*,const QJsonObject&,const QString&)>;
    explicit ListPage(ApiClient *api,ClientWindow *window,const ListSpec &spec,QWidget *parent=nullptr);
    void refresh() override;
    void setActionHandler(Action handler) { action_=std::move(handler); }
    void setResponseHandler(std::function<void(const ApiResponse&)> handler){responseLoaded_=std::move(handler);}
    void setLoadedHandler(std::function<void(const QJsonArray&)> handler) { loaded_=std::move(handler); }
    QPushButton *addToolbarButton(const QString &label,std::function<void()> callback,bool primary=false);
    void setOptionChoices(const QList<QPair<QString,QVariant>> &choices);
    void setExtraQuery(const QString &key,const QString &value);
    void clearContextQuery(){if(!extra_.isEmpty()){extra_.clear();pagination_->reset();++requestGeneration_;}}
    void setLocalFilter(std::function<bool(const QJsonObject&)> filter,bool resetPage=true) {
        localFilter_=std::move(filter); if(resetPage)pagination_->reset(); refresh();
    }
    QList<qint64> checkedIds() const { return table_->checkedIds(); }
    ClientTable *table() const { return table_; }
    QJsonArray rows() const { return rows_; }
    ClientPagination *pagination() const { return pagination_; }
    QString keyword() const;
    QMap<QString,QString> query() const;
protected:
    ApiClient *api_;
    ClientWindow *window_;
    ListSpec spec_;
    ClientTable *table_;
    ClientPagination *pagination_;
    QLayout *toolbar_;
    QLineEdit *search_;
    QLineEdit *secondary_=nullptr;
    QMap<QString,QLineEdit*> additional_;
    QComboBox *option_=nullptr;
    DateRangeWidget *dates_=nullptr;
    QJsonArray rows_;
    QMap<QString,QString> extra_;
    QString optionKey_="status";
    Action action_;
    std::function<void(const QJsonArray&)> loaded_;
    std::function<void(const ApiResponse&)> responseLoaded_;
    std::function<bool(const QJsonObject&)> localFilter_;
    quint64 requestGeneration_=0;
};
