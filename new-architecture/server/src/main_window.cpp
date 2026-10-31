#include "ui_settings.h"
#include "permission_labels.h"
#include "main_window.h"
#include "path_validation.h"
#include "api_server.h"
#include "database.h"
#include "device_server.h"
#include "editor_dialog.h"
#include "external_db.h"
#include "pattern_pages.h"
#include "ui_controls.h"
#include "ui_i18n.h"
#include "ui_dialogs.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QComboBox>
#include <QColor>
#include <QCheckBox>
#include <QClipboard>
#include <QDate>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFrame>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QIcon>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QNetworkInterface>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QScrollBar>
#include <QSqlQuery>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QShortcut>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTimer>
#include <QThread>
#include <QTime>
#include <QSysInfo>
#include <QVBoxLayout>
#include <functional>
#include <memory>

namespace {
int tagHeight(const QStringList &tags,const QFont &font,int availableWidth) {
    if(tags.isEmpty()) return 38;
    const QFontMetrics metrics(font); int x=4,y=4; const int limit=qMax(60,availableWidth-4);
    for(const QString &source:tags) {
        const QString tag=UiI18n::instance() ? UiI18n::instance()->text(source) : source;
        const int width=qMax(46,metrics.horizontalAdvance(tag)+16);
        if(x>4 && x+width>limit) { x=4; y+=24; }
        x+=width+5;
    }
    return y+24;
}

class PermissionTagDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        QStyleOptionViewItem background=option; background.text.clear(); background.state &= ~QStyle::State_HasFocus;
        QStyledItemDelegate::paint(painter,background,index);
        const QStringList tags=index.data(Qt::UserRole+1).toStringList();
        if(tags.isEmpty()) return;
        painter->save(); painter->setRenderHint(QPainter::Antialiasing);
        painter->setClipRect(option.rect.adjusted(2,2,-2,-2)); painter->setFont(option.font);
        const QFontMetrics metrics(option.font); int x=option.rect.left()+4,y=option.rect.top()+4;
        const int limit=option.rect.right()-4;
        for(const QString &source:tags) {
            const QString tag=UiI18n::instance() ? UiI18n::instance()->text(source) : source;
            const int width=qMax(46,metrics.horizontalAdvance(tag)+16);
            if(x>option.rect.left()+4 && x+width>limit) { x=option.rect.left()+4; y+=24; }
            painter->setPen(QPen(QColor("#b3d8ff"),1)); painter->setBrush(Qt::white);
            painter->drawRoundedRect(QRectF(x+0.5,y+0.5,width-1,19),2,2);
            painter->setPen(QColor("#409eff")); painter->drawText(QRect(x,y,width,20),Qt::AlignCenter,tag);
            x+=width+5;
        }
        painter->restore();
    }
};
class RowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        QStyleOptionViewItem rowOption=option; rowOption.state &= ~QStyle::State_HasFocus;
        QStyledItemDelegate::paint(painter,rowOption,index);
    }
};
class DeviceDragTree : public QTreeWidget {
public:
    std::function<void(qint64,qint64)> onDeviceDrop;
protected:
    void startDrag(Qt::DropActions supported) override {
        if(!currentItem() || currentItem()->data(0,Qt::UserRole+1).toString()!="device") return;
        QTreeWidget::startDrag(supported);
    }
    void dragEnterEvent(QDragEnterEvent *event) override {
        if(event->source()==this && currentItem() &&
           currentItem()->data(0,Qt::UserRole+1).toString()=="device")
            event->acceptProposedAction();
        else event->ignore();
    }
    void dragMoveEvent(QDragMoveEvent *event) override {
        if(event->source()!=this || !currentItem() ||
           currentItem()->data(0,Qt::UserRole+1).toString()!="device") { event->ignore(); return; }
        auto *target=itemAt(event->position().toPoint());
        if(target && QStringList{"group","ungrouped","device"}.contains(
            target->data(0,Qt::UserRole+1).toString()) && target!=currentItem())
            event->acceptProposedAction();
        else event->ignore();
    }
    void dropEvent(QDropEvent *event) override {
        auto *source=currentItem(), *target=itemAt(event->position().toPoint());
        if(!source || !target || source==target || source->data(0,Qt::UserRole+1).toString()!="device") {
            event->ignore(); return;
        }
        const QString kind=target->data(0,Qt::UserRole+1).toString();
        if(!QStringList{"group","ungrouped","device"}.contains(kind)) { event->ignore(); return; }
        const qint64 groupId=kind=="group" ? target->data(0,Qt::UserRole).toLongLong() :
            kind=="device" ? target->data(0,Qt::UserRole+2).toLongLong() : 0;
        if(onDeviceDrop) onDeviceDrop(source->data(0,Qt::UserRole).toLongLong(),groupId);
        event->acceptProposedAction();
    }
};
class EmptyStateWidget : public QWidget {
public:
    explicit EmptyStateWidget(QWidget *parent):QWidget(parent) { setAttribute(Qt::WA_TransparentForMouseEvents); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        const qreal x=(width()-58)/2.0, y=5;
        painter.setPen(QPen(QColor("#8393a7"),2.2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter.setBrush(QColor("#f7faff"));
        QPainterPath page;
        page.moveTo(x+9,y+3); page.lineTo(x+39,y+3); page.lineTo(x+51,y+15);
        page.lineTo(x+51,y+54); page.quadTo(x+51,y+58,x+47,y+58);
        page.lineTo(x+11,y+58); page.quadTo(x+7,y+58,x+7,y+54);
        page.lineTo(x+7,y+7); page.quadTo(x+7,y+3,x+9,y+3);
        painter.drawPath(page);
        painter.setBrush(QColor("#e9f0f8"));
        QPainterPath fold; fold.moveTo(x+39,y+3); fold.lineTo(x+39,y+15); fold.lineTo(x+51,y+15);
        painter.drawPath(fold);
        painter.setPen(QPen(QColor("#9aa9ba"),2,Qt::SolidLine,Qt::RoundCap));
        painter.drawLine(QPointF(x+17,y+27),QPointF(x+40,y+27));
        painter.drawLine(QPointF(x+17,y+35),QPointF(x+40,y+35));
        painter.drawLine(QPointF(x+17,y+43),QPointF(x+33,y+43));
        painter.setPen(QColor("#66768a"));
        QFont labelFont=font(); labelFont.setPixelSize(13); painter.setFont(labelFont);
        painter.drawText(QRect(0,77,width(),24),Qt::AlignCenter,UiI18n::instance() ? UiI18n::instance()->text("暂无数据") : "暂无数据");
    }
};

QString elementFontFamily() {
    static const QString family=[] {
        const int id=QFontDatabase::addApplicationFont(":/element-icons.ttf");
        const QStringList names=QFontDatabase::applicationFontFamilies(id);
        return names.isEmpty() ? QString() : names.first();
    }();
    return family;
}
QIcon elementIcon(quint16 code,const QColor &normal=QColor("#4d596a"),const QColor &selected=QColor("#3388ff")) {
    QIcon icon;
    for(const auto &state:{QPair<QIcon::Mode,QColor>{QIcon::Normal,normal},QPair<QIcon::Mode,QColor>{QIcon::Selected,selected}}) {
        QPixmap pixmap(36,36); pixmap.setDevicePixelRatio(2.0); pixmap.fill(Qt::transparent); QPainter painter(&pixmap); painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setPen(state.second); QFont font(elementFontFamily()); font.setPixelSize(15); painter.setFont(font);
        painter.drawText(QRect(0,0,18,18),Qt::AlignCenter,QString(QChar(code))); painter.end(); icon.addPixmap(pixmap,state.first);
    }
    return icon;
}
class StyledTable : public QTableWidget {
public:
    StyledTable() {
        empty_=new EmptyStateWidget(viewport()); empty_->hide();
    }
    void showEmpty(bool visible) { empty_->setVisible(visible); positionEmpty(); }
    void setBaseWidths(const QVector<int> &widths) { baseWidths_=widths; adjustColumns(); }
protected:
    void resizeEvent(QResizeEvent *event) override { QTableWidget::resizeEvent(event); adjustColumns(); positionEmpty(); }
private:
    void positionEmpty() { empty_->setGeometry((viewport()->width()-160)/2,(viewport()->height()-110)/2,160,110); empty_->raise(); }
    void adjustColumns() {
        if(adjusting_ || baseWidths_.size()!=columnCount()) return;
        adjusting_=true;
        int total=0; QVector<int> flexible;
        for(int i=0;i<baseWidths_.size();++i) {
            total+=baseWidths_[i];
            if(baseWidths_[i]>=100 && (!horizontalHeaderItem(i) || horizontalHeaderItem(i)->text()!="操作")) flexible << i;
        }
        const int extra=qMax(0,viewport()->width()-total);
        int remaining=extra;
        for(int i=0;i<baseWidths_.size();++i) {
            int addition=0;
            if(flexible.contains(i)) { addition=flexible.last()==i ? remaining : extra/flexible.size(); remaining-=addition; }
            const int desired=baseWidths_[i]+addition;
            if(columnWidth(i)!=desired) setColumnWidth(i,desired);
        }
        for(int row=0;row<rowCount();++row) {
            int desired=38;
            for(int column=0;column<columnCount();++column) {
                if(auto *cell=item(row,column)) {
                    const QStringList tags=cell->data(Qt::UserRole+1).toStringList();
                    if(!tags.isEmpty()) desired=qMax(desired,qMax(80,tagHeight(tags,font(),columnWidth(column)-6)+4));
                }
            }
            if(rowHeight(row)!=desired) setRowHeight(row,desired);
        }
        adjusting_=false;
    }
    EmptyStateWidget *empty_;
    QVector<int> baseWidths_;
    bool adjusting_=false;
};
class ResponsiveFilterPanel : public QFrame {
public:
    ResponsiveFilterPanel() {
        setObjectName("filterPanel");
        grid_=new QGridLayout(this);
        grid_->setContentsMargins(10,8,10,8);
        grid_->setHorizontalSpacing(8);
        grid_->setVerticalSpacing(6);
    }
    void setGroups(const QList<QWidget*> &groups) {
        groups_=groups;
        updateMode(true);
    }
    void updateMode(bool force=false) {
        if(groups_.size()!=4) return;
        int required=grid_->contentsMargins().left()+grid_->contentsMargins().right()+3*grid_->horizontalSpacing();
        for(QWidget *group:groups_) required+=group->sizeHint().width();
        const bool compact=width()<required;
        if(!force && compact==compact_) return;
        compact_=compact;
        for(QWidget *group:groups_) grid_->removeWidget(group);
        for(int column=0;column<5;++column) grid_->setColumnStretch(column,0);
        if(compact) {
            grid_->addWidget(groups_[0],0,0);
            grid_->addWidget(groups_[1],0,1);
            grid_->addWidget(groups_[2],1,0);
            grid_->addWidget(groups_[3],1,1);
            grid_->setColumnStretch(2,1);
        } else {
            for(int i=0;i<groups_.size();++i) grid_->addWidget(groups_[i],0,i);
            grid_->setColumnStretch(4,1);
        }
        grid_->invalidate(); updateGeometry();
    }
protected:
    void resizeEvent(QResizeEvent *event) override { QFrame::resizeEvent(event); updateMode(); }
private:
    QGridLayout *grid_;
    QList<QWidget*> groups_;
    bool compact_=false;
};
struct PageSpec { QString title, table; QStringList fields, labels; };
const QList<PageSpec> specs = {
    {"权限角色","roles",{"name","remark","permissions"},{"角色名","备注","权限 JSON"}},
    {"账号管理","users",{"username","password","nickname","email","phone","role","groupId","groupIds","disabled"},{"账号","密码","姓名","邮箱","手机号","角色","所属分组 ID","可见分组 JSON","禁用 (0/1)"}},
    {"操作员管理","operators",{"username","password","nickname","groupId","disabled"},{"账号","密码","姓名","分组 ID","限制登录 (0/1)"}},
    {"分组管理","groups",{"name","parentId","sortOrder"},{"名称","父分组 ID","排序"}},
    {"设备管理","devices",{"code","name","ip","type","electricControlType","groupId","remark"},{"编号","名称","IP","设备类型","电控类型","分组 ID","备注"}},
    {"员工管理","employees",{"code","name","department","position","phone","groupId","remark"},{"工号","姓名","部门","岗位","手机号","分组 ID","备注"}},
    {"花型目录","patterns",{"name","fileName","patternType","filePath","fileSize","stitches","unitPrice","orderNo"},{"名称","文件名","类型","路径","大小","针数","工价","订单号"}},
    {"登录日志","login_logs",{},{}}
};
QString display(const QJsonValue &v) {
    if(v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if(v.isArray()) return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    return v.toVariant().toString();
}
QFrame *surface(const QString &name="surfaceCard") {
    auto *frame=new QFrame; frame->setObjectName(name); return frame;
}
QLabel *sectionTitle(const QString &text) {
    auto *label=new QLabel(text); label->setObjectName("sectionTitle"); return label;
}
QLabel *muted(const QString &text) {
    auto *label=new QLabel(text); label->setObjectName("mutedText"); return label;
}
QString primaryIPv4() {
    for(const QHostAddress &address:QNetworkInterface::allAddresses())
        if(address.protocol()==QAbstractSocket::IPv4Protocol && !address.isLoopback()) return address.toString();
    return "127.0.0.1";
}
QString runTool(const QString &command,const QStringList &arguments) {
    QProcess process; process.start(command,arguments);
    if(!process.waitForFinished(5000)) { process.kill(); return "执行超时"; }
    const QString output=QString::fromLocal8Bit(process.readAllStandardOutput()+process.readAllStandardError()).trimmed();
    return output.isEmpty() ? "无输出" : output;
}
#ifdef Q_OS_WIN
QString windowsPortReport(int port) {
    const QString output=runTool("netstat",{"-ano"});
    const QRegularExpression pattern(QString("[:.]%1(?:\\s|$)").arg(port));
    QStringList matched;
    QSet<QString> pids;
    for(const QString &line:output.split(QRegularExpression("[\\r\\n]+"),Qt::SkipEmptyParts))
        if(pattern.match(line).hasMatch()) {
            matched << line;
            const QString pid=line.trimmed().section(QRegularExpression("\\s+"),-1);
            if(QRegularExpression("^[0-9]+$").match(pid).hasMatch()) pids.insert(pid);
        }
    QStringList sections{matched.isEmpty() ? QString("未匹配到端口 %1 占用记录").arg(port) :
        QString("端口 %1 占用记录（共 %2 行）").arg(port).arg(matched.size()),""};
    sections << (matched.isEmpty() ? output : matched.join('\n'));
    if(!pids.isEmpty()) {
        QStringList processes;
        for(const QString &pid:pids) processes << runTool("tasklist",{"/FI","PID eq "+pid});
        sections << "" << "[进程信息]" << processes.join('\n');
    }
    return sections.join('\n');
}
#endif
void showOutput(QWidget *parent,const QString &title,const QString &content) {
    UiMessageBox::output(parent,title,content);
}
QList<qint64> chosenIds(QTableWidget *table) {
    QSet<qint64> ids;
    for(int row=0;row<table->rowCount();++row) {
        auto *item=table->item(row,0);
        auto *check=table->cellWidget(row,0) ? table->cellWidget(row,0)->findChild<QCheckBox*>() : nullptr;
        if(item && check && check->isChecked() && item->data(Qt::UserRole).toLongLong()>0) ids.insert(item->data(Qt::UserRole).toLongLong());
    }
    return ids.values();
}
}

MainWindow::MainWindow(Database *db, ApiServer *server, DeviceServer *deviceServer, QWidget *parent)
    : QMainWindow(parent), db_(db), server_(server), deviceServer_(deviceServer) {
    setWindowTitle("博尔物联网管理软件服务端"); resize(1600,1000); setMinimumSize(1120,700);
    auto *shell=new QWidget; auto *shellLayout=new QVBoxLayout(shell); shellLayout->setContentsMargins(0,0,0,0); shellLayout->setSpacing(0);
    auto *top=surface("topBar"); top->setFixedHeight(46);
    auto *topLayout=new QHBoxLayout(top);topLayout->setObjectName("windowChromeLayout"); topLayout->setContentsMargins(12,0,12,0); topLayout->setSpacing(10);
    auto *logo=new QLabel; QPixmap logoPixmap=QPixmap(":/logo-server.png").scaled(40,40,Qt::KeepAspectRatio,Qt::SmoothTransformation); logoPixmap.setDevicePixelRatio(2.0); logo->setPixmap(logoPixmap); topLayout->addWidget(logo);
    auto *topTitle=new QLabel("博尔物联网管理软件服务端"); topTitle->setObjectName("topTitle");bindVersionedProductTitle(topTitle,"博尔物联网管理软件服务端","Bohr IoT Management Server"); topLayout->addWidget(topTitle); topLayout->addStretch();
    auto chip=[](const QString &label,const QString &value) {
        auto *frame=surface("topChip"); frame->setMinimumWidth(118); auto *layout=new QVBoxLayout(frame); layout->setContentsMargins(8,3,8,3); layout->setSpacing(0);
        auto *caption=new QLabel(label); caption->setObjectName("chipLabel"); layout->addWidget(caption);
        auto *text=new QLabel(value); text->setObjectName("chipValue"); layout->addWidget(text); return frame;
    };
    topLayout->addWidget(chip("服务器 IP",primaryIPv4()));
    topLayout->addWidget(chip("设备 TCP 端口",QString::number(deviceServer_->port())));
    auto *languageGroup=new QWidget(top); languageGroup->setObjectName("mainLanguageSwitch");
    auto *languageLayout=new QHBoxLayout(languageGroup); languageLayout->setContentsMargins(2,2,2,2); languageLayout->setSpacing(2);
    languageZhButton_=new QPushButton("中",languageGroup); languageEnButton_=new QPushButton("EN",languageGroup);
    for(auto *button:{languageZhButton_,languageEnButton_}) {
        button->setObjectName("mainLanguageOption"); button->setFixedSize(28,22); button->setProperty("i18nSkip",true); languageLayout->addWidget(button);
    }
    topLayout->addWidget(languageGroup);
    connect(languageZhButton_,&QPushButton::clicked,this,[]{ uiSettings("QtServer").setValue("loginLanguage","zh-CN"); UiI18n::instance()->setEnglish(false); });
    connect(languageEnButton_,&QPushButton::clicked,this,[]{ uiSettings("QtServer").setValue("loginLanguage","en-US"); UiI18n::instance()->setEnglish(true); });
    connect(UiI18n::instance(),&UiI18n::languageChanged,this,&MainWindow::applyLanguageButtons);
    auto *logout=new QPushButton("退出登录"); logout->setObjectName("topButton"); topLayout->addWidget(logout);
    connect(logout,&QPushButton::clicked,this,&MainWindow::logoutRequested);
    shellLayout->addWidget(top);

    auto *body=new QWidget; auto *bodyLayout=new QHBoxLayout(body); bodyLayout->setContentsMargins(0,0,0,0); bodyLayout->setSpacing(0);
    auto *sidebar=surface("sidebar"); sidebar->setFixedWidth(206); auto *sidebarLayout=new QVBoxLayout(sidebar); sidebarLayout->setContentsMargins(0,0,0,0);
    navigation_=new QListWidget; navigation_->setObjectName("navigation");
    const QStringList menuLabels={"主界面","辅助工具","连接数据库","权限角色","账号管理","设备管理","登录日志","花型列表","花型统计"};
    const QList<quint16> menuIcons={0xe766,0xe7ac,0xe736,0xe7a7,0xe6e3,0xe775,0xe785,0xe784,0xe766};
    navigation_->setIconSize(QSize(18,18));
    for(int i=0;i<menuLabels.size();++i) { auto *item=new QListWidgetItem(elementIcon(menuIcons[i]),menuLabels[i]); navigation_->addItem(item); }
    sidebarLayout->addWidget(navigation_); bodyLayout->addWidget(sidebar);
    pages_=new QStackedWidget; pages_->setObjectName("pages"); bodyLayout->addWidget(pages_,1); shellLayout->addWidget(body,1); setCentralWidget(shell);
    UiWindowGeometry::attach(this);
    feedbackLabel_=new QLabel(this); feedbackLabel_->setObjectName("actionToast"); feedbackLabel_->setAlignment(Qt::AlignCenter);
    feedbackLabel_->setAttribute(Qt::WA_TransparentForMouseEvents); feedbackLabel_->setFixedHeight(38); feedbackLabel_->hide();
    auto *feedbackShadow=new QGraphicsDropShadowEffect(feedbackLabel_); feedbackShadow->setBlurRadius(16);
    feedbackShadow->setOffset(0,3); feedbackShadow->setColor(QColor(43,72,35,45)); feedbackLabel_->setGraphicsEffect(feedbackShadow);
    feedbackTimer_=new QTimer(this); feedbackTimer_->setSingleShot(true);
    connect(feedbackTimer_,&QTimer::timeout,feedbackLabel_,&QWidget::hide);

    auto *home=new QWidget; home->setObjectName("page"); auto *homeLayout=new QVBoxLayout(home);
    homeLayout->setContentsMargins(8,8,8,8); homeLayout->setSpacing(8);
    auto *statsRow=new QHBoxLayout; statsRow->setSpacing(8);
    const QStringList statTitles={"设备总数","开机设备","客户端账号数","员工数","分组数"};
    const QStringList statHints={"当前已接入后台的全部设备数量。","包含空闲和缝纫状态设备。","已创建的客户端登录账号。","员工档案的总数量。","用于管理账号和设备。"};
    const QList<quint16> statIcons={0xe775,0xe720,0xe6e3,0xe7ab,0xe784};
    for(int i=0;i<statTitles.size();++i) {
        auto *card=surface("summaryCard"); card->setMinimumHeight(108); auto *layout=new QVBoxLayout(card); layout->setContentsMargins(10,10,10,10); layout->setSpacing(4);
        auto *icon=new QLabel; icon->setObjectName(i==1 ? "statIconSuccess" : i==3 ? "statIconWarning" : "statIcon"); icon->setFixedSize(24,24); icon->setAlignment(Qt::AlignCenter);
        const QColor iconColor=i==1 ? QColor("#67c23a") : i==3 ? QColor("#e6a23c") : QColor("#409eff");
        icon->setPixmap(elementIcon(statIcons[i],iconColor,iconColor).pixmap(18,18)); layout->addWidget(icon);
        layout->addWidget(muted(statTitles[i]));
        auto *value=new QLabel("0"); value->setObjectName("statValue"); layout->addWidget(value); statValues_ << value;
        layout->addWidget(muted(statHints[i])); statsRow->addWidget(card,1);
    }
    homeLayout->addLayout(statsRow);
    auto *logCard=surface(); auto *logLayout=new QVBoxLayout(logCard); logLayout->setContentsMargins(12,10,12,10); logLayout->setSpacing(8);
    auto *logHeader=new QHBoxLayout; auto *logHeading=new QVBoxLayout; logHeading->setSpacing(3); logHeading->addWidget(sectionTitle("调试信息")); logHeading->addWidget(muted("按时间显示最近调试日志，可用于核对设备接入和协议通信。")); logHeader->addLayout(logHeading); logHeader->addStretch();
    auto *level=new UiComboBox; level->addItem("全部级别",QString()); level->addItem("INFO","info");
    level->addItem("WARN","warn"); level->addItem("ERROR","error"); level->setFixedWidth(104);
    level->setObjectName("debugLevelFilter");
    logLevel_=level; logHeader->addWidget(level);
    connect(level,&QComboBox::currentIndexChanged,this,[this](int){ refreshLogs(); });
    auto *refresh=new QPushButton("刷新"); logHeader->addWidget(refresh);
    connect(refresh,&QPushButton::clicked,this,[this]{ refreshHome(); showFeedback("调试信息已刷新"); });
    auto *clear=new QPushButton("清空日志"); clear->setObjectName("dangerButton"); logHeader->addWidget(clear);
    logLayout->addLayout(logHeader);
    summary_=muted("HTTP API 端口 " + QString::number(server_->port()) + " · 设备 TCP 端口 " + QString::number(deviceServer_->port())); logLayout->addWidget(summary_);
    console_=new QListWidget; console_->setObjectName("logView");
    console_->setFrameShape(QFrame::NoFrame); console_->setSelectionMode(QAbstractItemView::NoSelection);
    console_->setFocusPolicy(Qt::NoFocus); logLayout->addWidget(console_,1);
    connect(clear,&QPushButton::clicked,this,[this]{
        if(UiMessageBox::question(this,"清空日志","确定清空所有调试日志吗？")==UiMessageBox::Yes) {
            QSqlQuery(db_->connection()).exec("DELETE FROM debug_logs");
            refreshLogs(); showFeedback("调试日志已清空");
        }
    });
    connect(deviceServer_,&DeviceServer::eventLogged,this,[this](const QString &){ refreshLogs(); });
    homeLayout->addWidget(logCard,1); pages_->addWidget(home);

    auto *tools=new QWidget; tools->setObjectName("page"); auto *toolPage=new QHBoxLayout(tools); toolPage->setContentsMargins(8,8,8,8); toolPage->setSpacing(8);
    auto *toolMain=new QWidget; auto *toolMainLayout=new QVBoxLayout(toolMain); toolMainLayout->setContentsMargins(0,0,0,0); toolMainLayout->setSpacing(8);
    auto *networkCard=surface(); auto *networkLayout=new QVBoxLayout(networkCard); networkLayout->setContentsMargins(12,10,12,10); networkLayout->setSpacing(8);
    networkLayout->addWidget(sectionTitle("网络诊断工具")); networkLayout->addWidget(muted("快速查看本机 IP、主机名、ARP 缓存以及目标设备连通性。"));
    auto *networkButtons=new QGridLayout; networkButtons->setSpacing(8);
    auto *showIp=new QPushButton("查看本机 IP 信息"), *ping=new QPushButton("Ping 设备"), *arp=new QPushButton("查看 ARP 缓存"), *host=new QPushButton("查看主机名");
    networkButtons->addWidget(showIp,0,0); networkButtons->addWidget(ping,0,1); networkButtons->addWidget(arp,1,0); networkButtons->addWidget(host,1,1); networkLayout->addLayout(networkButtons);
    connect(showIp,&QPushButton::clicked,this,[this] {
        QStringList lines; for(const QNetworkInterface &iface:QNetworkInterface::allInterfaces()) for(const auto &entry:iface.addressEntries())
            if(entry.ip().protocol()==QAbstractSocket::IPv4Protocol) lines << iface.humanReadableName()+"  "+entry.ip().toString();
        showOutput(this,"本机 IP 信息",lines.join('\n'));
    });
    connect(ping,&QPushButton::clicked,this,[this] {
        const auto entered=UiMessageBox::prompt(this,"Ping 设备","设备 IP");
        if(!entered) return; const QString ip=*entered; QHostAddress address;
        if(!address.setAddress(ip) || address.protocol()!=QAbstractSocket::IPv4Protocol) { UiMessageBox::warning(this,"Ping 设备","请输入有效的 IPv4 地址"); return; }
#ifdef Q_OS_WIN
        showOutput(this,"Ping 设备",runTool("ping",{"-n","1","-w","2000",ip}));
#else
        showOutput(this,"Ping 设备",runTool("ping",{"-c","1","-W","2",ip}));
#endif
    });
    connect(arp,&QPushButton::clicked,this,[this]{ showOutput(this,"ARP 缓存",runTool("arp",{"-a"})); });
    connect(host,&QPushButton::clicked,this,[this]{ showOutput(this,"主机名",QSysInfo::machineHostName()); });
    toolMainLayout->addWidget(networkCard);

    auto *portsCard=surface(); auto *portsLayout=new QVBoxLayout(portsCard); portsLayout->setContentsMargins(12,10,12,10); portsLayout->setSpacing(8);
    portsLayout->addWidget(sectionTitle("端口占用诊断")); portsLayout->addWidget(muted("检查服务端口是否已被程序占用。"));
    auto *portRow=new QHBoxLayout; auto *portInput=new PortStepper; portInput->setProperty("toolField","diagnosticPort");portInput->setRange(1,65535); portInput->setValue(deviceServer_->port()>0 ? deviceServer_->port() : 38400); portInput->setFixedWidth(124); portRow->addWidget(portInput);
    auto *checkPort=new QPushButton("查看端口占用"), *allPorts=new QPushButton("查看全部端口"), *serverPorts=new QPushButton("查看当前服务端口进程");
    for(auto *button:{checkPort,allPorts,serverPorts}) button->setFixedHeight(28);
    portRow->addWidget(checkPort); portRow->addWidget(allPorts); portRow->addWidget(serverPorts); portRow->addStretch(); portsLayout->addLayout(portRow);
    portsLayout->addWidget(muted("端口修改后需要重启服务器程序生效。"));
    auto portOutput=[this](int port) {
        auto result=std::make_shared<QString>();
        QThread *worker=QThread::create([result,port] {
#ifdef Q_OS_WIN
            *result=windowsPortReport(port);
#else
            *result=runTool("lsof",{"-nP","-iTCP:"+QString::number(port),"-sTCP:LISTEN"});
#endif
        });
        connect(worker,&QThread::finished,this,[this,worker,result] {
            showOutput(this,"端口占用",*result); worker->deleteLater();
        });
        worker->start(); showFeedback("正在检查端口");
    };
    connect(checkPort,&QPushButton::clicked,this,[portInput,portOutput]{ portOutput(portInput->value()); });
    connect(serverPorts,&QPushButton::clicked,this,[this,portOutput]{ portOutput(deviceServer_->port()); });
    connect(allPorts,&QPushButton::clicked,this,[this]{
        auto result=std::make_shared<QString>();
        QThread *worker=QThread::create([result] {
#ifdef Q_OS_WIN
            *result=runTool("netstat",{"-ano"});
#else
            *result=runTool("lsof",{"-nP","-iTCP","-sTCP:LISTEN"});
#endif
        });
        connect(worker,&QThread::finished,this,[this,worker,result] {
            showOutput(this,"全部端口",*result); worker->deleteLater();
        });
        worker->start(); showFeedback("正在检查端口");
    });
    toolMainLayout->addWidget(portsCard);
    auto *firewallCard=surface(); auto *firewallLayout=new QVBoxLayout(firewallCard); firewallLayout->setContentsMargins(12,10,12,10); firewallLayout->setSpacing(8);
    firewallLayout->addWidget(sectionTitle("局域网访问设置")); firewallLayout->addWidget(muted("只检查本软件使用的端口，不关闭系统防火墙。"));
    firewallLayout->addWidget(muted("Windows 下可检查并修复仅限本地子网的入站 TCP 规则。"));
    auto *firewallButtons=new QHBoxLayout;
    auto *openFirewall=new QPushButton("打开防火墙配置"), *checkFirewall=new QPushButton("检查放行状态"),
         *repairFirewallButton=new QPushButton("一键修复局域网访问");
    for(auto *button:{openFirewall,checkFirewall,repairFirewallButton}) firewallButtons->addWidget(button);
#ifdef Q_OS_WIN
    connect(openFirewall,&QPushButton::clicked,this,[] { QProcess::startDetached("control.exe",{"/name","Microsoft.WindowsFirewall"}); });
#else
    openFirewall->setEnabled(false); repairFirewallButton->setEnabled(false);
#endif
    auto *firewallNetwork=new QNetworkAccessManager(this);
    connect(checkFirewall,&QPushButton::clicked,this,[this,firewallNetwork] {
        const QUrl url(QString("http://127.0.0.1:%1/api/system/firewall/status").arg(server_->port()));
        QNetworkReply *reply=firewallNetwork->get(QNetworkRequest(url));
        connect(reply,&QNetworkReply::finished,this,[this,reply] {
            const QJsonObject body=QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            if(body.value("code").toInt()!=0) { UiMessageBox::warning(this,"防火墙状态",body.value("message").toString()); return; }
            const QJsonObject status=body.value("data").toObject();
            const auto translate=[](const QString &source) {
                return UiI18n::instance() ? UiI18n::instance()->text(source) : source;
            };
            if(!status.value("supported").toBool()) {
                showOutput(this,"防火墙状态",translate(status.value("message").toString())); return;
            }
            const QJsonObject management=status.value("management").toObject(), device=status.value("device").toObject();
            showOutput(this,"防火墙状态",
                translate(status.value("message").toString())+"\n"+
                management.value("name").toString()+": "+translate(management.value("exists").toBool() ? "已放行" : "未放行")+"\n"+
                device.value("name").toString()+": "+translate(device.value("exists").toBool() ? "已放行" : "未放行"));
        });
    });
    connect(repairFirewallButton,&QPushButton::clicked,this,[this,firewallNetwork,repairFirewallButton] {
        repairFirewallButton->setEnabled(false);
        const QUrl url(QString("http://127.0.0.1:%1/api/system/firewall/repair").arg(server_->port()));
        QNetworkReply *reply=firewallNetwork->post(QNetworkRequest(url),QByteArray());
        connect(reply,&QNetworkReply::finished,this,[this,reply,repairFirewallButton] {
            repairFirewallButton->setEnabled(true);
            const QJsonObject body=QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            if(body.value("code").toInt()==0) showFeedback("局域网防火墙规则修复成功");
            else UiMessageBox::warning(this,"修复防火墙",body.value("message").toString());
        });
    });
    firewallButtons->addStretch(); firewallLayout->addLayout(firewallButtons); toolMainLayout->addWidget(firewallCard); toolMainLayout->addStretch(); toolPage->addWidget(toolMain,2);

    auto *toolSide=new QWidget; auto *sideLayout=new QVBoxLayout(toolSide); sideLayout->setContentsMargins(0,0,0,0); sideLayout->setSpacing(8);
    auto *settingsCard=surface(); auto *settingsLayout=new QVBoxLayout(settingsCard); settingsLayout->setContentsMargins(12,10,12,10); settingsLayout->setSpacing(8);
    settingsLayout->addWidget(sectionTitle("服务器配置")); settingsLayout->addWidget(muted("维护共享目录和调试输出开关。"));
    auto *settingsForm=new QFormLayout; auto *sharedFolder=new QLineEdit(db_->config("shared_folder")); sharedFolder->setPlaceholderText("可为空，建议填写绝对路径"); settingsForm->addRow("共享文件夹目录",sharedFolder);
    sharedFolder->setProperty("toolField","sharedFolder");
    auto *sharedHint=muted("预留配置：当前不会启用系统共享或自动导入花型。请在花型列表中上传文件。");sharedHint->setWordWrap(true);sharedHint->setObjectName("sharedFolderUsageHint");settingsForm->addRow("",sharedHint);

    auto *folderError=new QLabel; folderError->setObjectName("promptError"); folderError->setProperty("toolField","folderError"); folderError->hide(); settingsForm->addRow("",folderError);
    auto *debugToggle=new QCheckBox("开启"); debugToggle->setChecked(db_->config("debug_output_enabled","true")=="true"); settingsForm->addRow("调试输出",debugToggle); settingsLayout->addLayout(settingsForm);
    auto *settingsButtons=new QHBoxLayout; auto *saveSettings=new QPushButton("保存配置"), *clearLogs=new QPushButton("清空调试日志"); saveSettings->setObjectName("primaryButton"); clearLogs->setObjectName("dangerButton"); settingsButtons->addWidget(saveSettings); settingsButtons->addWidget(clearLogs); settingsButtons->addStretch(); settingsLayout->addLayout(settingsButtons);
    saveSettings->setProperty("toolAction","saveSettings");
    connect(saveSettings,&QPushButton::clicked,this,[this,sharedFolder,debugToggle,folderError]{
        const QString invalid=sharedFolderError(sharedFolder->text()); folderError->setText(invalid); folderError->setVisible(!invalid.isEmpty());
        if(!invalid.isEmpty()) return;
        const bool savedFolder=db_->setConfig("shared_folder",sharedFolder->text().trimmed());
        const bool savedDebug=db_->setConfig("debug_output_enabled",debugToggle->isChecked() ? "true" : "false");
        if(savedFolder && savedDebug) showFeedback("配置已保存");
        else UiMessageBox::warning(this,"服务器配置","保存配置失败");
    });
    connect(clearLogs,&QPushButton::clicked,this,[this]{ if(UiMessageBox::question(this,"清空日志","确定清空调试日志吗？")==UiMessageBox::Yes) { QSqlQuery(db_->connection()).exec("DELETE FROM debug_logs"); refreshLogs(); showFeedback("调试日志已清空"); } });
    sideLayout->addWidget(settingsCard);
    auto *environmentCard=surface(); auto *environmentLayout=new QVBoxLayout(environmentCard); environmentLayout->setContentsMargins(12,10,12,10); environmentLayout->setSpacing(8);
    environmentLayout->addWidget(sectionTitle("运行环境信息")); environmentLayout->addWidget(muted("当前服务端的工作目录和系统环境。"));
    environmentLayout->addWidget(new QLabel("系统环境："+QSysInfo::productType()+" / "+QSysInfo::currentCpuArchitecture()));
    environmentLayout->addWidget(new QLabel("设备 TCP 端口："+QString::number(deviceServer_->port())));
    const QString workPath=QDir::currentPath(), dataPath=db_->dataDirectory();
    auto *workDir=new QLabel("工作目录："+workPath); workDir->setWordWrap(true);
    auto *workRow=new QHBoxLayout; workRow->addWidget(workDir,1); auto *copyWork=new QPushButton("复制");
    workRow->addWidget(copyWork); environmentLayout->addLayout(workRow);
    auto *dataDir=new QLabel("数据目录："+dataPath); dataDir->setWordWrap(true);
    auto *dataRow=new QHBoxLayout; dataRow->addWidget(dataDir,1); auto *copyData=new QPushButton("复制");
    dataRow->addWidget(copyData); environmentLayout->addLayout(dataRow);
    connect(copyWork,&QPushButton::clicked,this,[this,workPath] {
        QApplication::clipboard()->setText(workPath); showFeedback("已复制工作目录");
    });
    connect(copyData,&QPushButton::clicked,this,[this,dataPath] {
        QApplication::clipboard()->setText(dataPath); showFeedback("已复制数据目录");
    });
    sideLayout->addWidget(environmentCard); sideLayout->addStretch(); toolPage->addWidget(toolSide,1); pages_->addWidget(tools);

    auto *database=new QWidget; database->setObjectName("page"); auto *dbLayout=new QVBoxLayout(database); dbLayout->setContentsMargins(8,8,8,8); dbLayout->setSpacing(8);
    auto *dbStats=new QHBoxLayout; dbStats->setSpacing(8);
    const QStringList dbTitles={"服务器地址","同步状态","下次同步"};
    const QStringList dbValues={primaryIPv4(),"未启用","-"};
    const QStringList dbHints={"管理端口 "+QString::number(server_->port()),"外部数据库同步状态","配置保存后按周期调度"};
    QList<QLabel*> dbStatValues;
    for(int i=0;i<3;++i) { auto *card=surface("summaryCard"); auto *layout=new QVBoxLayout(card); layout->setContentsMargins(10,10,10,10); layout->addWidget(muted(dbTitles[i])); auto *value=new QLabel(dbValues[i]); value->setObjectName("statValue"); layout->addWidget(value); dbStatValues << value; layout->addWidget(muted(dbHints[i])); dbStats->addWidget(card,1); }
    dbLayout->addLayout(dbStats);
    auto *dbColumns=new QHBoxLayout; dbColumns->setSpacing(8);
    auto *dbInfo=surface(); auto *infoLayout=new QVBoxLayout(dbInfo); infoLayout->setContentsMargins(12,10,12,10); infoLayout->setSpacing(10);
    infoLayout->addWidget(sectionTitle("连接说明")); infoLayout->addWidget(muted("本地 SQLite 正常运行；外部连接需要对应的 Qt SQL 驱动。"));
    auto *externalEnabledInfo=new QLabel("是否启用：未启用"), *externalTypeInfo=new QLabel("数据库类型：MySQL");
    infoLayout->addWidget(externalEnabledInfo); infoLayout->addWidget(externalTypeInfo);
    auto *dbPath=new QLabel(db_->isMySql() ? "数据库：MySQL / "+db_->connection().databaseName() :
        "数据文件："+db_->connection().databaseName());
    dbPath->setTextInteractionFlags(Qt::TextSelectableByMouse); dbPath->setWordWrap(true); infoLayout->addWidget(dbPath); infoLayout->addStretch(); dbColumns->addWidget(dbInfo,1);
    auto *dbConfig=surface(); auto *configLayout=new QVBoxLayout(dbConfig); configLayout->setContentsMargins(12,10,12,10); configLayout->setSpacing(10);
    configLayout->addWidget(sectionTitle("外部数据库连接配置")); configLayout->addWidget(muted("保存连接参数后可测试连接，并按配置的间隔同步。"));
    auto *dbForm=new QFormLayout; auto *dbEnabled=new QCheckBox("启用"); dbForm->addRow("启用外部连接",dbEnabled);
    auto *dbType=new UiComboBox; dbType->addItems({"MySQL","MSSQL"}); dbForm->addRow("数据库类型",dbType);
    auto *dbHost=new QLineEdit, *dbUser=new QLineEdit, *dbPassword=new QLineEdit, *dbName=new QLineEdit, *dbCharset=new QLineEdit;
    dbType->setProperty("externalField","dbType"); dbEnabled->setProperty("externalField","enabled");
    dbHost->setProperty("externalField","host"); dbUser->setProperty("externalField","username");
    dbPassword->setProperty("externalField","password"); dbName->setProperty("externalField","database"); dbCharset->setProperty("externalField","charset");
    auto *dbPort=new PortStepper, *dbInterval=new PortStepper;
    dbPort->setProperty("externalField","port"); dbInterval->setProperty("externalField","interval");
    dbPort->setRange(1,65535); dbInterval->setRange(1,1440);
    dbPassword->setEchoMode(QLineEdit::Password);
    auto *dbVisibility=dbPassword->addAction(passwordVisibilityIcon(false),QLineEdit::TrailingPosition); dbVisibility->setCheckable(true);
    connect(dbVisibility,&QAction::toggled,dbPassword,[=](bool visible) { dbPassword->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password); dbVisibility->setIcon(passwordVisibilityIcon(visible)); });
    dbForm->addRow("服务器地址",dbHost);
    auto *hostError=new QLabel; hostError->setObjectName("promptError"); hostError->setProperty("externalField","hostError"); hostError->hide(); dbForm->addRow("",hostError);
    connect(dbHost,&QLineEdit::textEdited,this,[=] {
        const QString invalid=ExternalDb::validateHost(dbHost->text()); hostError->setText(invalid); hostError->setVisible(!invalid.isEmpty());
        dbHost->setProperty("invalid",!invalid.isEmpty()); dbHost->style()->unpolish(dbHost); dbHost->style()->polish(dbHost);
    });
    dbForm->addRow("端口",dbPort); dbForm->addRow("登录名",dbUser);
    dbForm->addRow("密码",dbPassword); dbForm->addRow("数据库名",dbName); dbForm->addRow("字符集",dbCharset);
    dbForm->addRow("同步间隔(分钟)",dbInterval);
    configLayout->addLayout(dbForm);
    auto *dirtyLabel=new QLabel("配置有未保存的修改"); dirtyLabel->setProperty("externalField","dirty"); dirtyLabel->setStyleSheet("color: #e6a23c;"); dirtyLabel->hide(); configLayout->addWidget(dirtyLabel);
    configLayout->addStretch();
    auto *dbActions=new QHBoxLayout; dbActions->addStretch();
    auto *dbReload=new QPushButton("重新加载"), *dbTest=new QPushButton("测试连接"),
         *dbSync=new QPushButton("立即同步"), *dbSave=new QPushButton("保存配置");
    dbSave->setObjectName("primaryButton");
    for(auto *button:{dbReload,dbTest,dbSync,dbSave}) dbActions->addWidget(button);
    dbTest->setToolTip("检查连接以及同步所需的建表和写入权限");
    dbTest->setProperty("externalAction","test"); dbSync->setProperty("externalAction","sync");
    dbSave->setProperty("externalAction","save"); dbReload->setProperty("externalAction","reload");
    configLayout->addLayout(dbActions);
    auto collectExternal=[=] {
        return QJsonObject{{"dbType",dbType->currentIndex()==0 ? "mysql" : "mssql"},
            {"host",dbHost->text().trimmed()},{"port",dbPort->value()},
            {"username",dbUser->text().trimmed()},{"password",dbPassword->text()},
            {"database",dbName->text().trimmed()},{"charset",dbCharset->text().trimmed()},
            {"syncIntervalMinutes",dbInterval->value()},{"enabled",dbEnabled->isChecked()}};
    };
    struct ExternalUiState { bool loading=false,testing=false,syncing=false,writable=false; QByteArray tested,saved; };
    auto externalState=std::make_shared<ExternalUiState>();
    auto fullKey=[](const QJsonObject &value) {
        const auto config=ExternalDb::normalize(value);
        return ExternalDb::connectionFingerprint(config)+":"+QByteArray::number(config.value("enabled").toBool())+":"+QByteArray::number(config.value("syncIntervalMinutes").toInt());
    };
    auto updateExternalGate=[=] {
        if(externalState->loading) return;
        const auto current=ExternalDb::normalize(collectExternal());
        const bool dirty=fullKey(current)!=externalState->saved;
        dirtyLabel->setVisible(dirty);
        dbSync->setEnabled(!externalState->testing && !externalState->syncing && current.value("enabled").toBool() && !dirty &&
            externalState->writable && externalState->tested==ExternalDb::connectionFingerprint(current));
        dbTest->setEnabled(current.value("enabled").toBool() && !externalState->testing && !externalState->syncing);
        dbSave->setEnabled(!externalState->syncing); dbReload->setEnabled(!externalState->syncing);
        for(QWidget *field:QList<QWidget*>{dbEnabled,dbType,dbHost,dbPort,dbUser,dbPassword,dbName,dbInterval}) field->setEnabled(!externalState->syncing);
        dbCharset->setEnabled(!externalState->syncing && dbType->currentIndex()==0);
    };
    auto loadExternal=[=] {
        externalState->loading=true;
        const QJsonObject config=ExternalDb::normalize(
            QJsonDocument::fromJson(db_->config("external_db_config").toUtf8()).object());
        dbEnabled->setChecked(config.value("enabled").toBool());
        dbType->setCurrentIndex(config.value("dbType").toString()=="mssql" ? 1 : 0);
        dbHost->setText(config.value("host").toString()); dbPort->setValue(config.value("port").toInt());
        dbUser->setText(config.value("username").toString()); dbPassword->setText(config.value("password").toString());
        dbName->setText(config.value("database").toString()); dbCharset->setText(dbType->currentIndex()==0 ? config.value("charset").toString() : QString());
        dbCharset->setPlaceholderText(dbType->currentIndex()==0 ? QString() : "不适用");
        dbVisibility->setChecked(false); hostError->hide(); dbHost->setProperty("invalid",false);
        dbHost->style()->unpolish(dbHost); dbHost->style()->polish(dbHost);
        dbInterval->setValue(config.value("syncIntervalMinutes").toInt());
        externalEnabledInfo->setText(config.value("enabled").toBool() ? "是否启用：已启用" : "是否启用：未启用");
        externalTypeInfo->setText("数据库类型："+config.value("dbType").toString().toUpper());
        const QString last=db_->config("external_db_last_sync_at");
        const QDateTime previous=QDateTime::fromString(last,"yyyy-MM-dd HH:mm:ss");
        const QDateTime next=previous.isValid() ? previous.addSecs(config.value("syncIntervalMinutes").toInt()*60) : QDateTime{};
        dbStatValues[1]->setText(!config.value("enabled").toBool() ? "未启用" : !previous.isValid() ? "等待首次同步" :
            next<=QDateTime::currentDateTime() ? "待同步" : "已计划");
        dbStatValues[2]->setText(next.isValid() && config.value("enabled").toBool() ? next.toString("yyyy-MM-dd HH:mm") : "-");
        externalState->saved=fullKey(config); externalState->loading=false; updateExternalGate();
    };
    for(auto *input:{dbHost,dbUser,dbPassword,dbName,dbCharset}) connect(input,&QLineEdit::textChanged,this,[updateExternalGate](const QString&){updateExternalGate();});
    connect(dbEnabled,&QCheckBox::toggled,this,[updateExternalGate](bool){updateExternalGate();});
    connect(dbPort,&PortStepper::valueChanged,this,[updateExternalGate](int){updateExternalGate();});
    connect(dbInterval,&PortStepper::valueChanged,this,[updateExternalGate](int){updateExternalGate();});
    connect(dbType,&QComboBox::currentIndexChanged,this,[=](int type) {
        if(externalState->loading) return;
        dbHost->clear(); dbUser->clear(); dbPassword->clear(); dbName->clear();
        dbPort->setValue(type==0 ? 3306 : 1433); dbInterval->setValue(30);
        dbCharset->setText(type==0 ? "utf8mb4" : QString()); dbCharset->setPlaceholderText(type==0 ? QString() : "不适用");
        dbVisibility->setChecked(false); hostError->hide(); dbHost->setProperty("invalid",false);
        dbHost->style()->unpolish(dbHost); dbHost->style()->polish(dbHost); updateExternalGate();
    });
    connect(dbReload,&QPushButton::clicked,this,[this,loadExternal] { loadExternal(); showFeedback("配置已重新加载"); });
    connect(dbSave,&QPushButton::clicked,this,[this,collectExternal,loadExternal] {
        QJsonObject config=ExternalDb::normalize(collectExternal());
        const QString invalid=ExternalDb::validate(config,config.value("enabled").toBool());
        if(!invalid.isEmpty()) { UiMessageBox::warning(this,"保存配置",invalid); return; }
        config["updatedAt"]=QDateTime::currentSecsSinceEpoch();
        if(!db_->setConfig("external_db_config",QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)))) {
            UiMessageBox::warning(this,"保存配置","保存失败"); return;
        }
        loadExternal(); showFeedback("配置已保存");
    });
    connect(dbTest,&QPushButton::clicked,this,[this,collectExternal,dbTest,externalState,updateExternalGate] {
        const QJsonObject config=ExternalDb::normalize(collectExternal());
        const QString invalid=ExternalDb::validate(config,true);
        if(!invalid.isEmpty()) { showFeedback(invalid,true); return; }
        externalState->testing=true; updateExternalGate(); dbTest->setText("连接中...");
        struct TestOutcome { bool connected=false,writable=false; QString error,writeError; };
        auto result=std::make_shared<TestOutcome>();
        QThread *worker=QThread::create([config,result] { result->connected=ExternalDb::test(config,&result->error,&result->writable,&result->writeError); });
        connect(worker,&QThread::finished,this,[this,worker,result,dbTest,externalState,updateExternalGate,collectExternal,config] {
            externalState->testing=false;
            externalState->writable=result->connected && result->writable;
            externalState->tested=result->connected ? ExternalDb::connectionFingerprint(config) : QByteArray{};
            dbTest->setText("测试连接"); updateExternalGate();
            if(ExternalDb::connectionFingerprint(config)!=ExternalDb::connectionFingerprint(collectExternal()))
                showFeedback("配置已变更，请重新测试连接",true);
            else if(result->connected && result->writable) showFeedback("数据库连接成功，可进行同步");
            else if(result->connected) showFeedback("数据库连接成功，但未通过同步权限检查",true);
            else showFeedback(result->error,true);
            worker->deleteLater();
        });
        worker->start();
    });
    connect(dbSync,&QPushButton::clicked,this,[this,dbSync,loadExternal,externalState,collectExternal,fullKey,updateExternalGate] {
        const QJsonObject config=ExternalDb::normalize(
            QJsonDocument::fromJson(db_->config("external_db_config").toUtf8()).object());
        if(!config.value("enabled").toBool() || fullKey(collectExternal())!=externalState->saved || !externalState->writable ||
           externalState->tested!=ExternalDb::connectionFingerprint(collectExternal())) {
            showFeedback("请先保存配置并通过可写连接测试",true); return;
        }
        externalState->syncing=true; updateExternalGate(); dbSync->setText("同步中...");
        auto result=std::make_shared<QPair<bool,QString>>();
        const QJsonObject source=db_->connectionOptions();
        QThread *worker=QThread::create([source,config,result] { result->first=ExternalDb::sync(source,config,true,&result->second); });
        connect(worker,&QThread::finished,this,[this,worker,result,dbSync,loadExternal,externalState] {
            externalState->syncing=false; dbSync->setText("立即同步"); loadExternal();
            if(result->first) showFeedback("同步任务已执行");
            else UiMessageBox::warning(this,"立即同步",result->second);
            worker->deleteLater();
        });
        worker->start();
    });
    loadExternal();
    dbColumns->addWidget(dbConfig,2); dbLayout->addLayout(dbColumns,1); pages_->addWidget(database);

    for(int i=0;i<specs.size();++i) {
        const PageSpec &spec=specs[i];
        auto *page=new QWidget; page->setObjectName("page"); QVBoxLayout *vertical=nullptr;
        if(spec.table=="users" || spec.table=="devices") {
            auto *panelLayout=new QHBoxLayout(page); panelLayout->setContentsMargins(8,8,8,8); panelLayout->setSpacing(8);
            auto *treeCard=surface(); treeCard->setFixedWidth(280); auto *treeLayout=new QVBoxLayout(treeCard); treeLayout->setContentsMargins(10,10,10,10); treeLayout->setSpacing(8);
            auto *treeHeader=new QHBoxLayout; treeHeader->addWidget(sectionTitle(spec.table=="users" ? "分组管理" : "设备树与分组")); treeHeader->addStretch();
            auto *treeRefresh=new QPushButton("刷新"); treeHeader->addWidget(treeRefresh); treeLayout->addLayout(treeHeader);
            if(spec.table=="devices") {
                auto *legend=new QHBoxLayout;
                for(const auto &state:{QPair<QString,QString>{"空闲","#67c23a"},{"缝纫","#f56c6c"},{"关机","#909399"}}) {
                    auto *label=new QLabel(state.first);label->setObjectName("nativeDeviceStatusLegend");label->setStyleSheet("color:"+state.second+";");legend->addWidget(label);
                }
                legend->addStretch();treeLayout->addLayout(legend);
            }
            auto *treeSearch=new QLineEdit; treeSearch->setPlaceholderText(spec.table=="users" ? "搜索分组" : "搜索设备或分组"); treeLayout->addWidget(treeSearch);
            treeSearch->addAction(elementIcon(0xe778,QColor("#a0acb9")),QLineEdit::LeadingPosition);
            auto *scopeStrip=surface("selectionStrip"); auto *scopeLayout=new QHBoxLayout(scopeStrip); scopeLayout->setContentsMargins(8,6,8,6);
            auto *scope=muted(spec.table=="users" ? "当前选择：全部账号" : "当前选择：全部分组"); scopeLayout->addWidget(scope,1);
            scope->setProperty("customerScope",spec.table);
            auto *clearScope=new QPushButton("清空"); scopeLayout->addWidget(clearScope); treeLayout->addWidget(scopeStrip); scopeLabels_[i]=scope;
            clearScope->setProperty("customerClearScope",spec.table);
            QTreeWidget *tree=spec.table=="devices" ? static_cast<QTreeWidget*>(new DeviceDragTree) : new QTreeWidget;
            tree->setHeaderHidden(true); tree->setObjectName("groupTree"); tree->setContextMenuPolicy(Qt::CustomContextMenu);
            tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);tree->header()->setStretchLastSection(false);
            tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
            if(spec.table=="devices") {
                tree->setDragEnabled(true); tree->setAcceptDrops(true);
                tree->setDragDropMode(QAbstractItemView::InternalMove);
                tree->setDefaultDropAction(Qt::MoveAction);
                static_cast<DeviceDragTree*>(tree)->onDeviceDrop=[this,i](qint64 deviceId,qint64 groupId) {
                    const QJsonObject device=db_->row("devices",deviceId);
                    if(device.isEmpty()) return;
                    if(device.value("groupId").toInteger()==groupId) { showFeedback("设备已在目标分组"); return; }
                    QString error;
                    if(!db_->moveDevices({deviceId},groupId>0 ? QJsonValue(groupId) : QJsonValue(QJsonValue::Null),&error)) {
                        UiMessageBox::warning(this,"移动设备",error); return;
                    }
                    refreshGroupTree(i); refreshTable(i);
                    showFeedback(groupId>0 ? "设备已移动到目标分组" : "设备已移出分组");
                };
            }
            treeLayout->addWidget(tree,1); trees_[i]=tree;
            connect(treeRefresh,&QPushButton::clicked,this,[this,i]{ refreshGroupTree(i); refreshTable(i); showFeedback("分组已刷新"); });
            connect(clearScope,&QPushButton::clicked,this,[this,i,tree,scope]{ ungroupedModes_[i]=false; tree->clearSelection(); scope->setProperty("i18nDataKind",QVariant()); scope->setText(specs[i].table=="users" ? "当前选择：全部账号" : "当前选择：全部分组"); refreshTable(i); });
            connect(tree,&QTreeWidget::itemClicked,this,[this,i,scope](QTreeWidgetItem *item,int){
                ungroupedModes_[i]=false;
                const bool custom=item->data(0,Qt::UserRole+1).toString()=="device" ||
                    (item->data(0,Qt::UserRole+1).toString()=="group" && item->data(0,Qt::UserRole).toLongLong()!=1);
                if(custom) { scope->setProperty("i18nDataKind","selection"); scope->setProperty("i18nDataName",item->text(0)); }
                else scope->setProperty("i18nDataKind",QVariant());
                scope->setText("当前选择："+item->text(0)); currentPages_[i]=1; refreshTable(i);
            });
            connect(treeSearch,&QLineEdit::textChanged,tree,[tree](const QString &keyword){
                std::function<bool(QTreeWidgetItem*)> filter=[&](QTreeWidgetItem *item) {
                    bool match=keyword.isEmpty() || item->text(0).contains(keyword,Qt::CaseInsensitive);
                    for(int n=0;n<item->childCount();++n) match=filter(item->child(n)) || match;
                    item->setHidden(!match); return match;
                };
                for(int n=0;n<tree->topLevelItemCount();++n) filter(tree->topLevelItem(n));
            });
            connect(tree,&QTreeWidget::customContextMenuRequested,this,[this,i,tree](const QPoint &point){
                auto *item=tree->itemAt(point);if(!item)return;
                if(item->data(0,Qt::UserRole+1)=="device") {
                    const auto deviceId=item->data(0,Qt::UserRole).toLongLong();QMenu menu(tree);menu.setObjectName("groupContextMenu");
                    auto *rename=menu.addAction("重命名");auto *remove=menu.addAction("删除设备");auto *move=db_->row("devices",deviceId).value("groupId").toInteger()>0 ? menu.addAction("移出分组") : nullptr;
                    auto *chosen=menu.exec(tree->viewport()->mapToGlobal(point));if(!chosen)return;QString error;
                    if(chosen==rename) {
                        const auto current=db_->row("devices",deviceId);const auto name=UiMessageBox::prompt(this,"重命名设备","请输入新名称",current.value("name").toString(),100);if(!name)return;
                        if(db_->count("devices","name=? AND COALESCE(group_id,0)=? AND id<>?",{*name,current.value("groupId").toInteger(),deviceId})>0)UiMessageBox::warning(this,"修改失败","同组设备名称已存在");
                        else if(!db_->update("devices",deviceId,{{"name",*name}},&error))UiMessageBox::warning(this,"修改失败",error);
                    } else if(chosen==remove){if(UiMessageBox::question(this,"删除设备","确定删除该设备吗？")!=UiMessageBox::Yes)return;if(!db_->remove("devices",deviceId,&error))UiMessageBox::warning(this,"删除失败",error);else {deviceServer_->disconnectDevice(deviceId);selectedDevices_.remove(deviceId);}}
                    else if(chosen==move && !db_->moveDevices({deviceId},QJsonValue(QJsonValue::Null),&error))UiMessageBox::warning(this,"移动失败",error);
                    refreshGroupTree(i);refreshTable(i);return;
                }
                if(!QStringList{"group","ungrouped","all"}.contains(item->data(0,Qt::UserRole+1).toString()))return;
                const qint64 id=item->data(0,Qt::UserRole).toLongLong();
                QMenu menu(tree); menu.setObjectName("groupContextMenu"); menu.setMinimumWidth(170);
                auto *addRoot=id==0 ? menu.addAction("新增分组") : nullptr;
                auto *addChild=id>0 ? menu.addAction("新增子分组") : nullptr;
                auto *moveHere=specs[i].table=="devices" && id>0 && !selectedDevices_.isEmpty() ? menu.addAction("移动已选设备到当前组") : nullptr;
                QAction *rename=nullptr, *remove=nullptr;
                if(id>1) { rename=menu.addAction("重命名"); remove=menu.addAction("删除分组"); }
                auto *chosen=menu.exec(tree->viewport()->mapToGlobal(point));
                if(!chosen)return;
                if(chosen==moveHere) {
                    QString error;if(!db_->moveDevices(selectedDevices_.values(),id,&error))UiMessageBox::warning(this,"移动失败",error);
                    else {selectedDevices_.clear();showFeedback("设备已移动到目标分组");}refreshGroupTree(i);refreshTable(i);
                } else if(chosen==addRoot) {
                    const auto body=showEditorDialog(db_,"groups",{},true,this);
                    if(body){qint64 created=0;QString error;if(!db_->insert("groups",*body,&created,&error))UiMessageBox::warning(this,"创建失败",error);refreshGroupTree(1);refreshGroupTree(4);}
                } else if(chosen && chosen==addChild) {
                    const auto name=UiMessageBox::prompt(this,"新增子分组","请输入分组名称",{},50);
                    if(name) { qint64 created=0; QString error; if(!db_->insert("groups",{{"name",*name},{"parentId",id}},&created,&error)) UiMessageBox::warning(this,"创建失败",error); refreshGroupTree(1); refreshGroupTree(4); }
                } else if(chosen==rename) {
                    const QString currentName=db_->row("groups",id).value("name").toString();
                    const auto value=UiMessageBox::prompt(this,"重命名分组","请输入新的分组名称",currentName,50);
                    if(value) { QString error; if(!db_->update("groups",id,{{"name",*value}},&error)) UiMessageBox::warning(this,"保存失败",error); refreshGroupTree(i); }
                } else if(chosen==remove) {
                    if(UiMessageBox::question(this,"确认删除","确定删除该分组吗？组内设备将移至未分组设备。")==UiMessageBox::Yes) {
                        QString error; if(!db_->removeGroup(id,&error)) UiMessageBox::warning(this,"不能删除",error);
                        refreshGroupTree(i); refreshTable(i);
                    }
                }
            });
            panelLayout->addWidget(treeCard);
            auto *right=new QWidget; right->setObjectName("page"); vertical=new QVBoxLayout(right); vertical->setContentsMargins(0,0,0,0); vertical->setSpacing(8); panelLayout->addWidget(right,1);
        } else {
            vertical=new QVBoxLayout(page); vertical->setContentsMargins(8,8,8,8); vertical->setSpacing(8);
        }
        const bool groupedFilters=spec.table=="users" || spec.table=="devices";
        auto *filter=groupedFilters ? static_cast<QFrame*>(new ResponsiveFilterPanel) : surface("filterPanel");
        const QString filterLabel=spec.table=="roles" ? "角色名称" : spec.table=="users" ? "账号/账号姓名" : spec.table=="devices" ? "关键字" : spec.table=="login_logs" ? "账号/IP" : "关键词";
        auto *keywordLabel=new QLabel(filterLabel); auto *search=new QLineEdit;
        search->setProperty("customerFilter",spec.table);
        search->setPlaceholderText(spec.table=="devices" ? "设备名/编号/类型/员工/主板号" : spec.table=="users" ? "输入账号/账号姓名/手机号" : spec.table=="roles" ? "输入角色名称或备注" : spec.table=="login_logs" ? "输入账号或登录 IP" : "搜索"+spec.title);
        if(groupedFilters) search->setFixedWidth(220); else search->setMaximumWidth(220);
        QLabel *optionLabel=nullptr; UiComboBox *option=nullptr;
        if(spec.table=="users" || spec.table=="devices" || spec.table=="login_logs") {
            option=new UiComboBox;
            if(spec.table=="users") {
                optionLabel=new QLabel("角色"); option->addItem("全部角色",QString());
                for(const auto &value:db_->rows("roles")) {
                    option->addItem(value.toObject().value("name").toString(),value.toObject().value("name").toString());
                    option->setItemData(option->count()-1,true,UiI18n::PreserveDataRole);
                }
            } else if(spec.table=="devices") { optionLabel=new QLabel("状态"); option->addItem("全部状态",QString()); option->addItem("空闲","idle"); option->addItem("缝纫","working"); option->addItem("关机","offline"); }
            else { optionLabel=new QLabel("登录状态"); option->addItem("全部状态",QString()); option->addItem("成功","成功"); option->addItem("失败","失败"); }
            option->setFixedWidth(110); optionFilters_[i]=option;
        }
        QLabel *dateLabel=nullptr; DateRangeWidget *range=nullptr;
        if(spec.table=="roles" || spec.table=="users" || spec.table=="devices" || spec.table=="login_logs") {
            dateLabel=new QLabel(spec.table=="login_logs" ? "登录时间" : spec.table=="devices" ? "添加时间" : "创建时间");
            range=new DateRangeWidget; range->setFixedWidth(246); dateRanges_[i]=range;
        }
        auto *searchButton=new QPushButton("搜索"); searchButton->setObjectName("primaryButton"); searchButton->setIcon(elementIcon(0xe778,Qt::white,Qt::white));
        auto *reset=new QPushButton("重置"); reset->setIcon(elementIcon(0xe6d0));
        if(groupedFilters) {
            auto pair=[](const QString &name,QWidget *label,QWidget *control) {
                auto *group=new QWidget; group->setObjectName(name);
                auto *layout=new QHBoxLayout(group); layout->setContentsMargins(0,0,0,0); layout->setSpacing(8);
                layout->addWidget(label); layout->addWidget(control); return group;
            };
            auto *actions=new QWidget; actions->setObjectName("filterActionsGroup");
            auto *actionsLayout=new QHBoxLayout(actions); actionsLayout->setContentsMargins(0,0,0,0); actionsLayout->setSpacing(8);
            actionsLayout->addWidget(searchButton); actionsLayout->addWidget(reset);
            static_cast<ResponsiveFilterPanel*>(filter)->setGroups({
                pair("filterKeywordGroup",keywordLabel,search),
                pair("filterOptionGroup",optionLabel,option),
                pair("filterDateGroup",dateLabel,range),actions
            });
        } else {
            auto *filterLayout=new QHBoxLayout(filter); filterLayout->setContentsMargins(10,8,10,8); filterLayout->setSpacing(8);
            filterLayout->addWidget(keywordLabel); filterLayout->addWidget(search);
            if(option) { filterLayout->addWidget(optionLabel); filterLayout->addWidget(option); }
            if(range) { filterLayout->addWidget(dateLabel); filterLayout->addWidget(range); }
            filterLayout->addWidget(searchButton); filterLayout->addWidget(reset); filterLayout->addStretch();
        }
        filters_ << search; vertical->addWidget(filter);
        connect(searchButton,&QPushButton::clicked,this,[this,i]{ currentPages_[i]=1; refreshTable(i); showFeedback("筛选已更新"); });
        connect(search,&QLineEdit::returnPressed,this,[this,i]{ currentPages_[i]=1; refreshTable(i); showFeedback("筛选已更新"); });
        connect(reset,&QPushButton::clicked,this,[this,i,search]{
            search->clear(); if(optionFilters_.contains(i)) optionFilters_[i]->setCurrentIndex(0);
            if(dateRanges_.contains(i)) dateRanges_[i]->clear();
            currentPages_[i]=1; refreshTable(i); showFeedback("筛选已重置");
        });

        auto *card=surface(); auto *cardLayout=new QVBoxLayout(card); cardLayout->setContentsMargins(12,10,12,10); cardLayout->setSpacing(8);
        const bool groupedToolbar=spec.table=="users" || spec.table=="devices";
        auto *toolbar=new QHBoxLayout; toolbar->addWidget(sectionTitle(spec.title));
        if(spec.table=="users") {
            auto *allAccounts=new QPushButton("全部账号"), *ungroupedAccounts=new QPushButton("未分组账号");
            toolbar->addWidget(allAccounts); toolbar->addWidget(ungroupedAccounts);
            connect(allAccounts,&QPushButton::clicked,this,[this,i]{ ungroupedModes_[i]=false; trees_[i]->clearSelection(); scopeLabels_[i]->setProperty("i18nDataKind",QVariant()); scopeLabels_[i]->setText("当前选择：全部账号"); currentPages_[i]=1; refreshTable(i); });
            connect(ungroupedAccounts,&QPushButton::clicked,this,[this,i]{ ungroupedModes_[i]=true; trees_[i]->clearSelection(); scopeLabels_[i]->setProperty("i18nDataKind",QVariant()); scopeLabels_[i]->setText("当前选择：未分组账号"); currentPages_[i]=1; refreshTable(i); });
        }
        toolbar->addStretch();
        auto *toolbarActions=groupedToolbar ? new QHBoxLayout : toolbar;
        if(groupedToolbar) toolbarActions->addStretch();
        auto *add=new QPushButton("新建"), *edit=new QPushButton("编辑"), *remove=new QPushButton("删除"), *reload=new QPushButton("刷新");
        add->setObjectName("primaryButton"); remove->setObjectName("dangerButton");
        add->setIcon(elementIcon(0xe6d9,Qt::white,Qt::white)); remove->setIcon(elementIcon(0xe6d7,QColor("#f56c6c"))); reload->setIcon(elementIcon(0xe6d0));
        if(spec.table=="roles") { add->setText("新增角色"); edit->hide(); remove->hide(); }
        if(spec.table=="users") { add->setText("新建账号"); edit->hide(); remove->hide(); }
        if(spec.table=="devices") { add->hide(); edit->hide(); remove->setText("批量删除设备"); remove->setEnabled(false); batchDeleteButtons_[i]=remove; }
        if(spec.table=="login_logs") { add->hide(); edit->hide(); remove->hide(); }
        if(spec.table=="users" || spec.table=="devices") {
            auto *move=new QPushButton("批量移动分组"); move->setEnabled(false); toolbarActions->addWidget(move); moveButtons_[i]=move;
            connect(move,&QPushButton::clicked,this,[this,i]{ moveSelected(i); });
        }
        if(spec.table=="devices") {auto *clear=new QPushButton("取消选择");clear->setObjectName("clearDeviceSelection");toolbarActions->addWidget(clear);connect(clear,&QPushButton::clicked,this,[this,i]{selectedDevices_.clear();refreshTable(i);});}
        for(auto *button:{add,edit,remove,reload}) toolbarActions->addWidget(button);
        if(groupedToolbar) {
            auto *toolbarBlock=new QVBoxLayout; toolbarBlock->setSpacing(5); toolbarBlock->addLayout(toolbar); toolbarBlock->addLayout(toolbarActions); cardLayout->addLayout(toolbarBlock);
        } else cardLayout->addLayout(toolbar);
        if(spec.table=="roles" || spec.table=="users" || spec.table=="devices") {
            QString note=spec.table=="roles" ? "角色权限用于约束客户端和服务端的功能可见范围，建议按工厂或业务场景拆分。"
                : spec.table=="users" ? "当前范围：全部账号，共 0 个账号"
                : "当前范围：总分组，共 0 台设备，未分组设备会以浅红底提示。";
            auto *hint=muted(note); hint->setObjectName("softNote"); cardLayout->addWidget(hint); contextNotes_[i]=hint;
        }
        auto *table=new StyledTable; table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setProperty("customerTable",spec.table);
        table->setItemDelegate(new RowDelegate(table));
        table->setSelectionMode((spec.table=="users" || spec.table=="devices") ? QAbstractItemView::ExtendedSelection : QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setAlternatingRowColors(true); table->setShowGrid(true);
        table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(36); table->horizontalHeader()->setFixedHeight(34); table->horizontalHeader()->setStretchLastSection(false);
        table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        if(spec.table=="roles") table->setItemDelegateForColumn(4,new PermissionTagDelegate(table));
        if(spec.table=="users") table->setItemDelegateForColumn(8,new PermissionTagDelegate(table));
        if(spec.table=="users" || spec.table=="devices") {
            auto update=[this,i,table] {
                const bool selected=!selectedIds(i).isEmpty();
                if(moveButtons_.contains(i)) moveButtons_[i]->setEnabled(selected);
                if(batchDeleteButtons_.contains(i)) batchDeleteButtons_[i]->setEnabled(selected);
            };
            connect(table,&QTableWidget::itemChanged,this,[update](QTableWidgetItem*){ update(); });
        }
        if(spec.table=="users" || spec.table=="devices") connect(table->horizontalHeader(),&QHeaderView::sectionClicked,table,[table](int section){
            if(section!=0) return;
            bool allChecked=true; for(int r=0;r<table->rowCount();++r) { auto *check=table->cellWidget(r,0) ? table->cellWidget(r,0)->findChild<QCheckBox*>() : nullptr; if(check && !check->isChecked()) allChecked=false; }
            for(int r=0;r<table->rowCount();++r) if(auto *check=table->cellWidget(r,0) ? table->cellWidget(r,0)->findChild<QCheckBox*>() : nullptr) check->setChecked(!allChecked);
        });
        cardLayout->addWidget(table,1);
        auto *footerRow=new QHBoxLayout; footerRow->setSpacing(6); if(spec.table=="devices") footerRow->addStretch();
        auto *footer=muted("共 0 条"); footerRow->addWidget(footer);
        auto *size=new UiComboBox; size->addItems({"10条/页","20条/页","50条/页","100条/页"}); size->setCurrentIndex(1); size->setFixedWidth(94); footerRow->addWidget(size);
        size->setProperty("customerPageSize",spec.table);
        auto *previous=new QPushButton("‹"), *pageButton=new QPushButton("1"), *next=new QPushButton("›");
        next->setProperty("customerNext",spec.table); previous->setProperty("customerPrevious",spec.table);
        previous->setFixedSize(28,28); pageButton->setFixedSize(28,28); pageButton->setObjectName("currentPage"); next->setFixedSize(28,28);
        footerRow->addWidget(previous); footerRow->addWidget(pageButton); footerRow->addWidget(next);
        pageSizes_[i]=size; previousButtons_[i]=previous; nextButtons_[i]=next; pageButtons_[i]=pageButton; currentPages_[i]=1;
        if(spec.table=="devices") {
            footerRow->addWidget(muted("前往")); auto *jump=new PortStepper; jump->setRange(1,1); jump->setFixedWidth(66); footerRow->addWidget(jump); footerRow->addWidget(muted("页")); jumpers_[i]=jump;
            connect(jump,&PortStepper::valueChanged,this,[this,i](int page){ if(currentPages_.value(i,1)!=page) { currentPages_[i]=page; refreshTable(i); } });
        } else footerRow->addStretch();
        cardLayout->addLayout(footerRow);
        connect(size,&QComboBox::currentIndexChanged,this,[this,i](int){ currentPages_[i]=1; refreshTable(i); });
        connect(previous,&QPushButton::clicked,this,[this,i]{ currentPages_[i]=qMax(1,currentPages_.value(i,1)-1); refreshTable(i); });
        connect(next,&QPushButton::clicked,this,[this,i]{ ++currentPages_[i]; refreshTable(i); });
        tables_ << table; footers_ << footer; vertical->addWidget(card,1); pages_->addWidget(page);
        add->setEnabled(!spec.fields.isEmpty()); edit->setEnabled(!spec.fields.isEmpty()); remove->setEnabled(!spec.fields.isEmpty() && spec.table!="devices");
        connect(add,&QPushButton::clicked,this,[this,i]{ editRecord(i,true); });
        connect(edit,&QPushButton::clicked,this,[this,i]{ editRecord(i,false); });
        connect(remove,&QPushButton::clicked,this,[this,i]{
            if(specs[i].table!="devices") { deleteRecord(i); return; }
            const QList<qint64> ids=selectedIds(i);
            if(ids.isEmpty()) { UiMessageBox::information(this,"选择设备","请先选择设备"); return; }
            if(UiMessageBox::question(this,"批量删除",QString("确定删除选中的 %1 台设备吗？").arg(ids.size()))!=UiMessageBox::Yes) return;
            QString error; for(qint64 id:ids) if(!db_->remove("devices",id,&error)) { UiMessageBox::warning(this,"删除失败",error); break; }
            selectedDevices_.clear();refreshGroupTree(i); refreshTable(i);
        });
        connect(reload,&QPushButton::clicked,this,[this,i]{ refreshTable(i); showFeedback("列表已刷新"); });
    }
    patternFilesPage_=new PatternFilesPage(db_,deviceServer_,[this](const QString &message){ showFeedback(message); });
    pages_->addWidget(patternFilesPage_);
    patternStatsPage_=new PatternStatsPage(db_); pages_->addWidget(patternStatsPage_);
    connect(navigation_,&QListWidget::currentRowChanged,this,&MainWindow::showPage);
    for(int i=0;i<qMin(9,navigation_->count());++i) {
        auto *shortcut=new QShortcut(QKeySequence(QString("Meta+%1").arg(i+1)),this);
        connect(shortcut,&QShortcut::activated,this,[this,i]{ navigation_->setCurrentRow(i); });
    }
    navigation_->setCurrentRow(0); refreshHome(); applyLanguageButtons();
    auto *timer=new QTimer(this); timer->setInterval(5000);
    connect(timer,&QTimer::timeout,this,[this] {
        refreshHome();
        if(navigation_->currentRow()==5 && tables_.size()>4 && !QApplication::activeModalWidget() &&
           selectedIds(4).isEmpty()) {
            refreshGroupTree(4);
            refreshTable(4);
        }
    });
    timer->start();
}

void MainWindow::showFeedback(const QString &message,bool error) {
    feedbackLabel_->setProperty("error",error);
    feedbackLabel_->style()->unpolish(feedbackLabel_); feedbackLabel_->style()->polish(feedbackLabel_);
    feedbackLabel_->setMaximumWidth(qMax(200,width()-40)); feedbackLabel_->setWordWrap(true);
    feedbackLabel_->setText(UiI18n::instance() ? UiI18n::instance()->text(message) : message);
    feedbackLabel_->show();UiWindowGeometry::fitNotification(feedbackLabel_,this);feedbackLabel_->raise();
    feedbackTimer_->start(error ? 3200 : 1800);
}

void MainWindow::applyLanguageButtons() {
    const bool english=UiI18n::instance() && UiI18n::instance()->english();
    languageZhButton_->setProperty("active",!english);
    languageEnButton_->setProperty("active",english);
    for(auto *button:{languageZhButton_,languageEnButton_}) { button->style()->unpolish(button); button->style()->polish(button); button->update(); }
    refreshHome();
    static const QList<int> pageMap={0,1,2,3,4,7,10,11,12};
    const int navigation=navigation_->currentRow();
    if(navigation>=0 && navigation<pageMap.size() && pageMap[navigation]>=3 && pageMap[navigation]<11)
        refreshTable(pageMap[navigation]-3);
    if(navigation==7) patternFilesPage_->refresh();
    if(navigation==8) patternStatsPage_->refresh();
    if(UiI18n::instance()) UiI18n::instance()->refresh();
    for(QWidget *widget:findChildren<QWidget*>()) if(auto *panel=dynamic_cast<ResponsiveFilterPanel*>(widget)) panel->updateMode();
    if(feedbackLabel_->isVisible()) { feedbackLabel_->adjustSize(); feedbackLabel_->move((width()-feedbackLabel_->width())/2,62); }
}

void MainWindow::showPage(int index) {
    static const QList<int> pageMap={0,1,2,3,4,7,10,11,12};
    if(index<0 || index>=pageMap.size()) return;
    const int page=pageMap[index]; pages_->setCurrentIndex(page);
    if(page==0) refreshHome();
    if(page>=3 && page<11) { refreshGroupTree(page-3); refreshTable(page-3); }
    if(page==11) patternFilesPage_->refresh();
    if(page==12) patternStatsPage_->refresh();
    if(UiI18n::instance()) UiI18n::instance()->refresh();
}

void MainWindow::refreshGroupTree(int index) {
    if(!trees_.contains(index)) return;
    auto *tree=trees_[index]; const bool devices=specs[index].table=="devices";
    QString selectedType; qint64 selectedId=0;
    const auto selected=tree->selectedItems();
    if(!selected.isEmpty()) { selectedType=selected.first()->data(0,Qt::UserRole+1).toString(); selectedId=selected.first()->data(0,Qt::UserRole).toLongLong(); }
    tree->blockSignals(true); tree->clear();
    QTreeWidgetItem *ungrouped=nullptr;auto *allGroups=new QTreeWidgetItem(tree,{"全部分组"});allGroups->setData(0,Qt::UserRole+1,"all");allGroups->setIcon(0,QIcon(":/folder.svg"));
    if(devices) {
        ungrouped=new QTreeWidgetItem(tree,{"未分组设备"}); ungrouped->setData(0,Qt::UserRole+1,"ungrouped");
        ungrouped->setIcon(0,QIcon(":/folder.svg"));
    }
    QHash<qint64,QTreeWidgetItem*> groups;
    const QJsonArray groupRows=db_->rowsAll("groups",{}, {},"sort_order,id");
    for(const auto &value:groupRows) {
        QJsonObject group=value.toObject(); auto *item=new QTreeWidgetItem({group.value("name").toString()});
        const qint64 id=group.value("id").toInteger(); item->setData(0,Qt::UserRole,id); item->setData(0,Qt::UserRole+1,"group");
        if(id!=1) item->setData(0,UiI18n::PreserveDataRole,true);
        item->setIcon(0,QIcon(":/folder.svg")); groups[id]=item;
    }
    for(const auto &value:groupRows) {
        QJsonObject group=value.toObject(); const qint64 id=group.value("id").toInteger(), parent=group.value("parentId").toInteger();
        if(groups.contains(parent)) groups[parent]->addChild(groups[id]);else if(allGroups)allGroups->addChild(groups[id]);else tree->addTopLevelItem(groups[id]);
    }
    if(devices) for(const auto &value:db_->rowsAll("devices",{}, {},"sort_order,id")) {
        QJsonObject device=value.toObject(); const qint64 id=device.value("id").toInteger(), group=device.value("groupId").toInteger();
        auto *item=new QTreeWidgetItem({device.value("name").toString()}); item->setData(0,Qt::UserRole,id); item->setData(0,Qt::UserRole+1,"device");
        item->setData(0,Qt::UserRole+2,group);
        item->setData(0,UiI18n::PreserveDataRole,true); item->setIcon(0,deviceStateIcon(device.value("status").toString()));
        if(groups.contains(group)) groups[group]->addChild(item); else if(ungrouped) ungrouped->addChild(item);
    }
    tree->expandAll();
    for(QTreeWidgetItemIterator it(tree);*it;++it) if((*it)->data(0,Qt::UserRole+1).toString()==selectedType && (*it)->data(0,Qt::UserRole).toLongLong()==selectedId) { tree->setCurrentItem(*it); break; }
    tree->blockSignals(false);
}

void MainWindow::refreshHome() {
    const QList<qint64> values={db_->count("devices"),db_->count("devices","status IN ('idle','working')"),db_->count("users"),db_->count("employees"),db_->count("groups")};
    for(int i=0;i<statValues_.size() && i<values.size();++i) statValues_[i]->setText(QString::number(values[i]));
    summary_->setText(QString("HTTP API 端口 %1  ·  设备 TCP 端口 %2  ·  最近刷新 %3")
        .arg(server_->port()).arg(deviceServer_->port()).arg(QTime::currentTime().toString("HH:mm:ss")));
    refreshLogs();
}

void MainWindow::refreshLogs() {
    if(!console_ || !logLevel_) return;
    const QString level=logLevel_->currentData().toString();
    const QJsonArray rows=level.isEmpty() ? db_->rows("debug_logs",{}, {},"created_at DESC,id DESC",80) :
        db_->rows("debug_logs","level=?",{level},"created_at DESC,id DESC",80);
    const int scroll=console_->verticalScrollBar()->value();
    console_->clear();
    if(rows.isEmpty()) {
        auto *empty=new QListWidgetItem(UiI18n::instance() ? UiI18n::instance()->text("暂无调试数据") : "暂无调试数据",console_);
        empty->setTextAlignment(Qt::AlignCenter); empty->setFlags(Qt::NoItemFlags);
        return;
    }
    for(const QJsonValue &value:rows) {
        const QJsonObject row=value.toObject();
        auto *item=new QListWidgetItem(console_);
        item->setSizeHint(QSize(0,68));
        auto *card=new QFrame; card->setObjectName("debugItem"); card->setProperty("i18nSkip",true);
        auto *layout=new QVBoxLayout(card); layout->setContentsMargins(12,7,12,7); layout->setSpacing(5);
        auto *meta=new QHBoxLayout; meta->setSpacing(8);
        auto *time=new QLabel(row.value("createdAt").toString().mid(11,8)); time->setObjectName("mutedText");
        auto *levelLabel=new QLabel(row.value("level").toString().toUpper());
        levelLabel->setObjectName(row.value("level").toString()=="error" ? "statusPillDanger" :
            row.value("level").toString()=="warn" ? "statusPillInfo" : "statusPillSuccess");
        levelLabel->setAlignment(Qt::AlignCenter); levelLabel->setFixedWidth(62);
        auto *source=new QLabel(row.value("source").toString()); source->setObjectName("mutedText");
        meta->addWidget(time); meta->addWidget(levelLabel); meta->addWidget(source); meta->addStretch();
        layout->addLayout(meta);
        auto *message=new QLabel(row.value("message").toString()); message->setWordWrap(true);
        message->setProperty("i18nSkip",true);
        message->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(message);
        console_->setItemWidget(item,card);
    }
    console_->verticalScrollBar()->setValue(scroll);
}

void MainWindow::refreshTable(int index) {
    if(index<0 || index>=specs.size()) return;
    const QJsonArray all=db_->rowsAll(specs[index].table,{}, {},specs[index].table=="login_logs" ? "login_time DESC,id DESC" : "id DESC");
    QJsonArray rows; const QString keyword=filters_[index]->text().trimmed();
    QString treeType; qint64 treeId=0;
    if(trees_.contains(index)) {
        const auto selected=trees_[index]->selectedItems();
        if(!selected.isEmpty()) { treeType=selected.first()->data(0,Qt::UserRole+1).toString(); treeId=selected.first()->data(0,Qt::UserRole).toLongLong(); }
    }
    if(specs[index].table=="users" && ungroupedModes_.value(index,false)) treeType="ungrouped";
    QSet<qint64> groupScope;
    if(treeType=="group" && trees_.contains(index) && !trees_[index]->selectedItems().isEmpty()) {
        std::function<void(QTreeWidgetItem*)> collect=[&](QTreeWidgetItem *node){
            if(node->data(0,Qt::UserRole+1)=="group")groupScope.insert(node->data(0,Qt::UserRole).toLongLong());
            for(int n=0;n<node->childCount();++n)collect(node->child(n));
        };collect(trees_[index]->selectedItems().first());
    }
    for(const auto &value:all) {
        const QJsonObject item=value.toObject();
        if(treeType=="group" && !groupScope.contains(item.value("groupId").toInteger())) continue;
        if(treeType=="ungrouped" && !item.value("groupId").isNull()) continue;
        if(treeType=="device" && item.value("id").toInteger()!=treeId) continue;
        if(optionFilters_.contains(index)) {
            const QString desired=optionFilters_[index]->currentData().toString();
            const QString field=specs[index].table=="users" ? "role" : "status";
            if(!desired.isEmpty() && item.value(field).toString()!=desired) continue;
        }
        if(dateRanges_.contains(index)) {
            const QString date=item.value(specs[index].table=="login_logs" ? "loginTime" : "createdAt").toString().left(10);
            if(dateRanges_[index]->startDate().isValid() && date<dateRanges_[index]->startDate().toString("yyyy-MM-dd")) continue;
            if(dateRanges_[index]->endDate().isValid() && date>dateRanges_[index]->endDate().toString("yyyy-MM-dd")) continue;
        }
        const QString deviceName=item.value("name").toString();
        const bool reverseName=specs[index].table=="devices" && !deviceName.isEmpty() && keyword.contains(deviceName,Qt::CaseInsensitive);
        if(keyword.isEmpty() || reverseName || QString::fromUtf8(QJsonDocument(item).toJson(QJsonDocument::Compact)).contains(keyword,Qt::CaseInsensitive)) rows.append(value);
    }
    const int pageSize=pageSizes_.contains(index) ? (pageSizes_[index]->currentIndex()==0 ? 10 : pageSizes_[index]->currentIndex()==1 ? 20 : pageSizes_[index]->currentIndex()==2 ? 50 : 100) : 20;
    const int pageCount=qMax(1,(rows.size()+pageSize-1)/pageSize);
    const int current=qBound(1,currentPages_.value(index,1),pageCount); currentPages_[index]=current;
    QJsonArray shown; for(int i=(current-1)*pageSize;i<qMin(current*pageSize,rows.size());++i) shown.append(rows[i]);
    auto *table=tables_[index]; table->clear(); table->clearSpans(); table->setRowCount(shown.size());
    QStringList headers,fields;
    const QString kind=specs[index].table;
    if(kind=="roles") { headers={"序号","角色名称","创建时间","父子联动","权限模块","操作","备注"}; fields={"#","name","createTime","parentChildLink","permissions","@","remark"}; }
    else if(kind=="users") { headers={"","序号","账号","账号姓名","手机号","角色","创建时间","可见分组","权限","状态","操作"}; fields={"$","#","username","nickname","phone","role","createTime","groupName","permissions","disabled","@"}; }
    else if(kind=="devices") { headers={"","序号","设备名称","初始名称","员工工号","员工姓名","设备编号","设备类型","电控类型","主板编号","识别方式","状态","当前花型","报警码","速度","当前/总生产量(件)","最近协议时间","设备 IP","分组","添加时间","备注","操作"};
        fields={"$","#","name","initialName","employeeCode","employeeName","code","type","electricControlType","mainboardSn","identifiedBy","status","currentPatternName","alarmCode","speed","production","lastProtocolAt","ip","groupName","createTime","remark","@"}; }
    else if(kind=="login_logs") { headers={"序号","账号","登录 IP","客户端版本","登录状态","登录时间","客户端信息"}; fields={"#","username","ip","clientVersion","status","loginTime","device"}; }
    else { headers={"序号"}; fields={"#"}; for(int i=0;i<specs[index].fields.size();++i) if(specs[index].fields[i]!="password") { fields << specs[index].fields[i]; headers << specs[index].labels[i]; } if(!specs[index].fields.isEmpty()) { headers << "操作"; fields << "@"; } }
    table->setColumnCount(headers.size()); table->setHorizontalHeaderLabels(headers);
    static_cast<StyledTable*>(table)->showEmpty(shown.isEmpty());
    for(int r=0;r<shown.size();++r) {
        const auto item=shown[r].toObject();
        for(int c=0;c<fields.size();++c) {
            const QString key=fields[c];
            if(key=="$") {
                auto *cell=new QTableWidgetItem; cell->setData(Qt::UserRole,item.value("id").toInteger()); table->setItem(r,c,cell);
                auto *holder=new QWidget; auto *layout=new QHBoxLayout(holder); layout->setContentsMargins(0,0,0,0); layout->setAlignment(Qt::AlignCenter);
                auto *check=new QCheckBox(holder); check->setObjectName("rowCheckBox"); check->setFocusPolicy(Qt::NoFocus); layout->addWidget(check);
                const qint64 recordId=item.value("id").toInteger();
                if(kind=="devices")check->setChecked(selectedDevices_.contains(recordId));
                connect(check,&QCheckBox::toggled,this,[this,index,recordId](bool checked) {
                    if(specs[index].table=="devices"){if(checked)selectedDevices_.insert(recordId);else selectedDevices_.remove(recordId);}
                    auto *current=tables_.value(index,nullptr); if(!current) return;
                    const bool selected=!selectedIds(index).isEmpty();
                    if(moveButtons_.contains(index)) moveButtons_[index]->setEnabled(selected);
                    if(batchDeleteButtons_.contains(index)) batchDeleteButtons_[index]->setEnabled(selected);
                });
                table->setCellWidget(r,c,holder); continue;
            }
            if(key=="@") {
                auto *actions=new QWidget; auto *actionLayout=new QHBoxLayout(actions); actionLayout->setContentsMargins(4,2,4,2); actionLayout->setSpacing(4);
                auto *edit=new QPushButton("编辑"), *remove=new QPushButton("删除"); remove->setObjectName("rowDangerButton"); actionLayout->addWidget(edit);
                if(kind=="devices") {
                    auto *ping=new QPushButton("Ping"); actionLayout->addWidget(ping);
                    const QString address=item.value("ip").toString();
                    connect(ping,&QPushButton::clicked,this,[this,address]{
                        QHostAddress parsed; if(!parsed.setAddress(address) || parsed.protocol()!=QAbstractSocket::IPv4Protocol) { UiMessageBox::warning(this,"Ping 设备","设备 IP 无效"); return; }
                        QProcess process;
#ifdef Q_OS_WIN
                        process.start("ping",QStringList{"-n","1","-w","2000",address});
#else
                        process.start("ping",QStringList{"-c","1","-W","2",address});
#endif
                        if(!process.waitForFinished(4000)) { process.kill(); UiMessageBox::warning(this,"Ping 设备","请求超时"); return; }
                        UiMessageBox::information(this,"Ping 设备",QString::fromLocal8Bit(process.readAllStandardOutput()+process.readAllStandardError()));
                    });
                }
                actionLayout->addWidget(remove); actionLayout->addStretch();
                connect(edit,&QPushButton::clicked,this,[this,index,r]{ tables_[index]->selectRow(r); editRecord(index,false); });
                connect(remove,&QPushButton::clicked,this,[this,index,r]{ tables_[index]->selectRow(r); deleteRecord(index); });
                table->setCellWidget(r,c,actions); continue;
            }
            QString value;
            if(key=="#") value=QString::number((current-1)*pageSize+r+1);
            else if(key=="groupName") { const auto group=db_->row("groups",item.value("groupId").toInteger()); value=group.isEmpty() ? "未分组" : group.value("name").toString(); }
            else if(key=="parentChildLink") value=item.value(key).toBool() ? "开启" : "关闭";
            else if(key=="disabled") value=item.value(key).toBool() ? "已禁用" : "正常";
            else if(key=="status" && kind=="devices") { const QString status=item.value(key).toString(); value=status=="working" ? "缝纫" : status=="idle" ? "空闲" : "关机"; }
            else if(key=="speed") value=QString::number(item.value("currentSpeed").toInteger())+"/"+QString::number(item.value("maxSpeedValue").toInteger());
            else if(key=="production") value=QString::number(item.value("productionCurrent").toInteger())+"/"+QString::number(item.value("productionTotal").toInteger());
            else if(key=="permissions") {
                const QJsonObject perms=QJsonDocument::fromJson(item.value(key).toString().toUtf8()).object();
                const QStringList enabled=permissionLabels(perms);
                value=enabled.isEmpty() ? "未配置" : enabled.join("  ·  ");
            } else value=display(item.value(key));
            if(key=="permissions") {
                const QStringList tags=value=="未配置" ? QStringList{} : value.split("  ·  ");
                auto *cell=new QTableWidgetItem(tags.isEmpty() ? "未配置" : QString());
                cell->setData(Qt::UserRole+1,tags); cell->setToolTip(tags.join("、")); table->setItem(r,c,cell); continue;
            }
            if(key=="status" || key=="disabled" || key=="parentChildLink") {
                const bool positive=value=="正常" || value=="空闲" || value=="缝纫" || value=="开启" || value=="成功";
                auto *wrap=new QWidget; auto *layout=new QHBoxLayout(wrap); layout->setContentsMargins(3,3,3,3); layout->addStretch();
                auto *pill=new QLabel(value); pill->setObjectName(value=="关机" ? "statusPillInfo" : value=="缝纫" ? "statusPillDanger" : positive ? "statusPillSuccess" : "statusPillDanger"); pill->setAlignment(Qt::AlignCenter);
                const QString displayValue=UiI18n::instance() ? UiI18n::instance()->text(value) : value;
                pill->setFixedSize(qMax(48,pill->fontMetrics().horizontalAdvance(displayValue)+20),22);
                layout->addWidget(pill,0,Qt::AlignVCenter); layout->addStretch(); table->setItem(r,c,new QTableWidgetItem); table->setCellWidget(r,c,wrap); continue;
            }
            auto *cell=new QTableWidgetItem(value); cell->setToolTip(value); cell->setData(UiI18n::PreserveDataRole,true);
            if(c==0) cell->setData(Qt::UserRole,item.value("id").toInteger());
            if(key=="#") cell->setTextAlignment(Qt::AlignCenter);
            table->setItem(r,c,cell);
        }
    }
    QVector<int> baseWidths;
    for(int c=0;c<headers.size();++c) {
        int width=120;
        if(headers[c].isEmpty()) width=48;
        else if(headers[c]=="序号") width=66;
        else if(headers[c]=="操作") width=kind=="devices" ? 230 : 160;
        else if(headers[c].contains("时间")) width=165;
        else if(headers[c].contains("权限")) width=260;
        else if(headers[c].contains("备注") || headers[c].contains("名称")) width=155;
        if(!headers[c].isEmpty()) {
            const QString heading=UiI18n::instance() ? UiI18n::instance()->text(headers[c]) : headers[c];
            width=qMax(width,QFontMetrics(table->font()).horizontalAdvance(heading)+24);
        }
        for(int row=0;row<table->rowCount();++row) if(auto *holder=table->cellWidget(row,c)) {
            for(auto *pill:holder->findChildren<QLabel*>()) if(pill->objectName().startsWith("statusPill")) {
                const QString text=UiI18n::instance() ? UiI18n::instance()->text(pill->text()) : pill->text();
                width=qMax(width,pill->fontMetrics().horizontalAdvance(text)+44);
            }
        }
        baseWidths << width;
    }
    static_cast<StyledTable*>(table)->setBaseWidths(baseWidths);
    footers_[index]->setText(QString("共 %1 条").arg(rows.size()));
    pageButtons_[index]->setText(QString::number(current));
    if(jumpers_.contains(index)) { QSignalBlocker blocker(jumpers_[index]); jumpers_[index]->setRange(1,pageCount); jumpers_[index]->setValue(current); }
    if(moveButtons_.contains(index)) moveButtons_[index]->setEnabled(!selectedIds(index).isEmpty());
    if(batchDeleteButtons_.contains(index)) batchDeleteButtons_[index]->setEnabled(!selectedIds(index).isEmpty());
    QString selectedLabel;
    if(trees_.contains(index) && trees_[index]->currentItem()) {
        auto *selectedItem=trees_[index]->currentItem();
        selectedLabel=selectedItem->data(0,UiI18n::OriginalRole).toString();
        if(selectedLabel.isEmpty()) selectedLabel=selectedItem->text(0);
    }
    if(contextNotes_.contains(index) && kind=="users") {
        auto *note=contextNotes_[index];
        if(treeType=="group" && treeId!=1 && !ungroupedModes_.value(index,false)) {
            note->setProperty("i18nDataKind","accounts"); note->setProperty("i18nDataName",selectedLabel); note->setProperty("i18nDataCount",rows.size());
        } else note->setProperty("i18nDataKind",QVariant());
        note->setText(QString("当前范围：%1，共 %2 个账号").arg(ungroupedModes_.value(index,false) ? "未分组账号" : treeType=="group" ? selectedLabel : "全部账号").arg(rows.size()));
    }
    if(contextNotes_.contains(index) && kind=="devices") {
        auto *note=contextNotes_[index];
        if(treeType=="group" && treeId!=1) {
            note->setProperty("i18nDataKind","devices"); note->setProperty("i18nDataName",selectedLabel); note->setProperty("i18nDataCount",rows.size());
        } else note->setProperty("i18nDataKind",QVariant());
        note->setText(QString("当前范围：%1，共 %2 台设备，未分组设备会以浅红底提示。").arg(treeType=="group" ? selectedLabel : treeType=="ungrouped" ? "未分组设备" : "全部设备").arg(rows.size()));
    }
    previousButtons_[index]->setEnabled(current>1); nextButtons_[index]->setEnabled(current<pageCount);
    if(UiI18n::instance()) UiI18n::instance()->refresh();
}

void MainWindow::editRecord(int index,bool creating) {
    const PageSpec &spec=specs[index]; QJsonObject old;
    qint64 id=0;
    if(!creating) {
        int row=tables_[index]->currentRow(); if(row<0) { UiMessageBox::information(this,"选择记录","请先选择一行"); return; }
        id=tables_[index]->item(row,0)->data(Qt::UserRole).toLongLong();
        if(id<1) { UiMessageBox::information(this,"选择记录","请先选择一条有效记录"); return; }
        old=db_->row(spec.table,id);
    }
    auto edited=showEditorDialog(db_,spec.table,old,creating,this);
    if(!edited) return;
    QJsonObject body=*edited;
    if(body.contains("password")) body["password"]=Database::hashPassword(body.value("password").toString());
    QString error; qint64 newId=0;
    const bool result=creating ? db_->insert(spec.table,body,&newId,&error) : db_->update(spec.table,id,body,&error);
    if(!result) UiMessageBox::warning(this,"保存失败",error);
    else {
        if(spec.table=="devices" && !creating &&
           (body.contains("employeeCode") || body.contains("employeeName"))) {
            const QJsonObject updated=db_->row("devices",id);
            deviceServer_->sendCurrentUser(id,updated.value("employeeCode").toString(),
                updated.value("employeeName").toString());
        }
        if(!creating && spec.table=="roles") {
            const QString oldName=old.value("name").toString(), newName=body.value("name").toString();
            QSqlQuery query(db_->connection()); query.prepare("UPDATE users SET role=?,permissions=? WHERE role=? AND deleted_at IS NULL");
            query.addBindValue(newName); query.addBindValue(body.value("permissions").toString()); query.addBindValue(oldName); query.exec();
        }
        if(spec.table=="groups") { refreshGroupTree(1); refreshGroupTree(4); }
        if(spec.table=="users") refreshGroupTree(1);
        if(spec.table=="devices") refreshGroupTree(4);
        refreshTable(index);
    }
}

void MainWindow::deleteRecord(int index) {
    auto *table=tables_[index]; int row=table->currentRow();
    if(row<0) { UiMessageBox::information(this,"选择记录","请先选择一行"); return; }
    qint64 id=table->item(row,0)->data(Qt::UserRole).toLongLong();
    if(id<1) { UiMessageBox::information(this,"选择记录","请先选择一条有效记录"); return; }
    if((specs[index].table=="users" || specs[index].table=="groups") && id==1) { UiMessageBox::warning(this,"禁止删除","不能删除默认管理员或总分组"); return; }
    if(specs[index].table=="roles") {
        const QString name=db_->row("roles",id).value("name").toString();
        if(name=="admin" || name=="user" || db_->count("users","role=?",{name})>0) { UiMessageBox::warning(this,"禁止删除","系统角色或仍有账号使用的角色不能删除"); return; }
    }
    if(UiMessageBox::question(this,"确认删除",specs[index].table=="groups" ? "确定删除该分组吗？组内设备将移至未分组设备。" : "确定删除所选记录吗？")!=UiMessageBox::Yes) return;
    QString error;
    if(!(specs[index].table=="groups" ? db_->removeGroup(id,&error) : db_->remove(specs[index].table,id,&error))) UiMessageBox::warning(this,"删除失败",error);
    else {
        if(specs[index].table=="users") refreshGroupTree(1);
        if(specs[index].table=="devices") { deviceServer_->disconnectDevice(id); refreshGroupTree(4); }
        if(specs[index].table=="groups") { refreshGroupTree(1); refreshGroupTree(4); }
        refreshTable(index);
    }
}

QList<qint64> MainWindow::selectedIds(int index) const {
    if(specs[index].table!="devices")return chosenIds(tables_[index]);
    QList<qint64> ids;for(qint64 id:selectedDevices_)if(!db_->row("devices",id).isEmpty())ids<<id;return ids;
}

void MainWindow::moveSelected(int index) {
    if(index<0 || index>=tables_.size()) return;
    const QList<qint64> ids=selectedIds(index);
    if(ids.isEmpty()) { UiMessageBox::information(this,"选择记录","请先选择账号或设备"); return; }
    auto group=showMoveGroupDialog(db_,ids.size(),this);
    if(!group) return;
    QString error;
    if(specs[index].table=="devices") {
        if(!db_->moveDevices(ids,*group,&error)) { UiMessageBox::warning(this,"移动失败",error); return; }
        selectedDevices_.clear();refreshGroupTree(index); refreshTable(index); return;
    }
    for(qint64 id:ids) if(!db_->update(specs[index].table,id,{{"groupId",*group}},&error)) { UiMessageBox::warning(this,"移动失败",error); return; }
    refreshGroupTree(index); refreshTable(index);
}
