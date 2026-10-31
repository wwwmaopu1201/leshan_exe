#include "ui_settings.h"
#include "client_window.h"
#include "list_page.h"
#include "pages.h"
#include "ui_controls.h"
#include "ui_dialogs.h"
#include "ui_i18n.h"

#include <QApplication>
#include <QAction>
#include <QHostAddress>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QCompleter>
#include <QDateTime>
#include <QFrame>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QNetworkReply>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSettings>
#include <QStringListModel>
#include <QStyledItemDelegate>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
QString menuIconFont() {
    static const QString family=[] {
        const int id=QFontDatabase::addApplicationFont(":/element-icons.ttf");
        const QStringList names=QFontDatabase::applicationFontFamilies(id);
        return names.isEmpty() ? QString() : names.first();
    }();
    return family;
}
QIcon clientMenuIcon(quint16 glyph) {
    const QString family=menuIconFont();
    if(family.isEmpty()) return {};
    QIcon icon;
    for(const auto &entry:{QPair<QIcon::Mode,QColor>{QIcon::Normal,QColor("#64748b")},
                           QPair<QIcon::Mode,QColor>{QIcon::Active,QColor("#3388ff")},
                           QPair<QIcon::Mode,QColor>{QIcon::Selected,QColor("#3388ff")}}) {
        QPixmap pixmap(36,36); pixmap.setDevicePixelRatio(2.0); pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap); painter.setRenderHint(QPainter::TextAntialiasing);
        QFont font(family); font.setPixelSize(16); painter.setFont(font);
        painter.setPen(entry.second);
        painter.drawText(QRect(0,0,18,18),Qt::AlignCenter,QString(QChar(glyph)));
        painter.end(); icon.addPixmap(pixmap,entry.first);
    }
    return icon;
}
class LoginCompletionDelegate : public QStyledItemDelegate {
public:
    LoginCompletionDelegate(QLineEdit *input,QObject *parent):QStyledItemDelegate(parent),input_(input) {}
    QSize sizeHint(const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        QSize size=QStyledItemDelegate::sizeHint(option,index);
        size.setHeight(qMax(36,input_->height()));
        return size;
    }
private:
    QLineEdit *input_;
};
void attachLoginCompleter(QLineEdit *input,const QStringList &history) {
    auto *completer=new QCompleter(history,input);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setMaxVisibleItems(8);
    auto *popup=new QListView;
    popup->setObjectName("clientLoginCompleter");
    popup->setUniformItemSizes(false);
    popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    popup->setItemDelegate(new LoginCompletionDelegate(input,popup));
    completer->setPopup(popup);
    input->setCompleter(completer);
}
class LoginCanvas : public QWidget {
public:
    LoginCanvas() { setObjectName("clientLogin"); background_=QPixmap(":/login-background-client.png"); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if(!background_.isNull()) {
            const QPixmap scaled=background_.scaled(size(),Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);
            painter.drawPixmap((width()-scaled.width())/2,(height()-scaled.height())/2,scaled);
        }
    }
private:
    QPixmap background_;
};
QLabel *muted(const QString &text) {
    auto *label=new QLabel(text); label->setObjectName("clientMuted"); label->setWordWrap(true); return label;
}
QJsonObject normalizedPermissions(const QJsonObject &user) {
    QJsonValue raw=user.value("permissions");
    if(raw.isString()) {
        const QJsonDocument parsed=QJsonDocument::fromJson(raw.toString().toUtf8());
        raw=parsed.isObject() ? QJsonValue(parsed.object()) : parsed.isArray() ? QJsonValue(parsed.array()) : QJsonValue();
    }
    QJsonObject permissions;
    if(raw.isObject()) permissions=raw.toObject();
    else if(raw.isArray()) for(const QJsonValue &value:raw.toArray())
        if(!value.toString().isEmpty()) permissions[value.toString()]=true;
    if(permissions.isEmpty()) for(const QString &key:{"home","dashboard","deviceManagement",
        "fileManagement","statistics","employeeManagement","remoteMonitoring"}) permissions[key]=true;
    const QHash<QString,QStringList> children={
        {"deviceManagement",{"deviceInfo","remoteMonitoring"}},
        {"fileManagement",{"patternFiles","devicePatternFiles","downloadLog"}},
        {"statistics",{"salaryStatistics","statusStatistics"}}};
    for(auto it=children.cbegin();it!=children.cend();++it) {
        bool detailed=false;
        for(const QString &child:it.value()) if(permissions.contains(child)) {
            if(it.key()=="deviceManagement" && child=="remoteMonitoring" &&
               !permissions.contains("deviceInfo") && permissions.value(it.key()).toBool()) continue;
            detailed=true;
        }
        for(const QString &child:it.value())
            permissions[child]=detailed ? permissions.value(child).toBool() : permissions.value(it.key()).toBool();
        bool any=false; for(const QString &child:it.value()) any|=permissions.value(child).toBool();
        permissions[it.key()]=any;
    }
    return permissions;
}
}

ClientWindow::ClientWindow(QWidget *parent):QMainWindow(parent),api_(this) {
    setObjectName("clientWindow"); setWindowTitle("博尔物联网管理软件客户端");
    setMinimumSize(1000,650);
    root_=new QStackedWidget(this); setCentralWidget(root_);
    createLogin();
    UiWindowGeometry::attach(this);
    connect(&api_,&ApiClient::sessionExpired,this,[this] {
        logout(false); notify("登录已过期，请重新登录",true);
    });
}

void ClientWindow::createLogin() {
    auto *canvas=new LoginCanvas; login_=canvas;
    auto *layout=new QVBoxLayout(canvas); layout->setContentsMargins(34,22,54,22); layout->setSpacing(0);
    auto *top=new QHBoxLayout; top->setObjectName("windowChromeLayout");
    auto *brand=new QLabel("Bohr  博尔物联网管理软件客户端"); brand->setObjectName("clientLoginBrand");
    bindVersionedProductTitle(brand,"Bohr  博尔物联网管理软件客户端","Bohr IoT Management Client");
    top->addWidget(brand); top->addStretch();
    auto *language=new QPushButton; language->setObjectName("languageSwitch");
    language->setCursor(Qt::PointingHandCursor); language->setFixedSize(116,30);
    auto *languageRow=new QHBoxLayout(language);
    languageRow->setContentsMargins(8,0,8,0); languageRow->setSpacing(6);
    auto *languageText=new QLabel; languageText->setObjectName("languageSwitchText");
    languageText->setProperty("i18nSkip",true);
    languageText->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *languageIcon=new QLabel; languageIcon->setObjectName("languageSwitchIcon");
    languageIcon->setPixmap(QIcon(":/language-switch.svg").pixmap(QSize(18,18),2.0));
    languageIcon->setFixedSize(18,18);
    languageIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
    languageRow->addWidget(languageText,1); languageRow->addWidget(languageIcon);
    top->addWidget(language); layout->addLayout(top);
    auto updateLanguage=[language,languageText] {
        const bool english=UiI18n::instance()->english();
        languageText->setText(english ? "English" : "简体中文");
        language->setAccessibleName(english ? "Switch to Chinese" : "切换到英语");
    };
    updateLanguage();
    connect(language,&QPushButton::clicked,this,[this] {
        const bool en=!UiI18n::instance()->english();
        uiSettings("QtClient").setValue("language",en ? "en-US" : "zh-CN");
        UiI18n::instance()->setEnglish(en);
    });
    connect(UiI18n::instance(),&UiI18n::languageChanged,this,updateLanguage);
    layout->addStretch(1);
    auto *middle=new QHBoxLayout; middle->addStretch(4);
    auto *card=new QFrame; card->setObjectName("clientLoginCard"); card->setFixedWidth(420);
    auto *form=new QVBoxLayout(card); form->setContentsMargins(34,34,34,34); form->setSpacing(14);
    auto *title=new QLabel("博尔物联网管理软件客户端"); title->setObjectName("clientLoginTitle");
    title->setWordWrap(true); form->addWidget(title); form->addSpacing(15);
    auto settings=uiSettings("QtClient");
    auto *server=new QLineEdit(settings.value("serverIp").toString()); server->setObjectName("clientLoginInput");
    server->setPlaceholderText("请输入服务器 IP 或主机名"); server->setClearButtonEnabled(true);
    attachLoginCompleter(server,settings.value("serverHistory").toStringList());
    auto *account=new QLineEdit(settings.value("rememberedUsername").toString());
    account->setObjectName("clientLoginInput"); account->setPlaceholderText("账号"); account->setClearButtonEnabled(true);
    attachLoginCompleter(account,settings.value("accountHistory").toStringList());
    auto *password=new QLineEdit(settings.value("rememberedPassword").toString());
    password->setObjectName("clientLoginInput"); password->setPlaceholderText("密码");
    password->setEchoMode(QLineEdit::Password); password->setClearButtonEnabled(true);
    auto *visibility=password->addAction(passwordVisibilityIcon(false),QLineEdit::TrailingPosition);
    visibility->setObjectName("passwordVisibility");
    visibility->setCheckable(true);
    connect(visibility,&QAction::toggled,password,[password,visibility](bool visible) {
        password->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        visibility->setIcon(passwordVisibilityIcon(visible));
    });
    form->addWidget(server); form->addWidget(account); form->addWidget(password);
    auto *loginButton=new QPushButton("登录"); loginButton->setObjectName("clientLoginSubmit");
    form->addSpacing(3); form->addWidget(loginButton);
    auto *foot=new QHBoxLayout; auto *remember=new QCheckBox("记住密码");
    remember->setChecked(settings.value("rememberPassword",false).toBool()); foot->addWidget(remember);
    auto restorePassword=[server,account,password,remember] {
        auto saved=uiSettings("QtClient");
        const QVariantMap hosts=saved.value("rememberedCredentials").toMap();
        const QVariantMap accounts=hosts.value(server->text().trimmed()).toMap();
        QString value=accounts.value(account->text().trimmed()).toString();
        if(value.isEmpty() && saved.value("serverIp").toString()==server->text().trimmed() &&
           saved.value("rememberedUsername").toString()==account->text().trimmed())
            value=saved.value("rememberedPassword").toString();
        password->setText(value); remember->setChecked(!value.isEmpty());
    };
    connect(account,&QLineEdit::textChanged,this,restorePassword);
    connect(server,&QLineEdit::textChanged,this,restorePassword);
    foot->addStretch(); auto *help=new QPushButton("无法连接？"); help->setObjectName("clientLink");
    foot->addWidget(help); form->addLayout(foot);
    middle->addWidget(card); layout->addLayout(middle); layout->addStretch(2);
    auto *footer=new QLabel("博尔物联网管理软件客户端  ·  Research Suzhou Bohr Technology Co., Ltd.");
    footer->setObjectName("clientLoginFooter"); footer->setAlignment(Qt::AlignCenter);
    layout->addWidget(footer);
    connect(help,&QPushButton::clicked,this,[this] {
        UiMessageBox::information(this,"未连接到服务端",
            "请检查服务端程序已运行、电脑在同一局域网，并输入服务端电脑显示的局域网 IP。Windows 可在服务端登录页使用一键修复防火墙。");
    });
    auto *connectionProbe=new QTimer(this); connectionProbe->setInterval(3000);
    connect(connectionProbe,&QTimer::timeout,this,[this,connectionProbe,loginButton] {
        if(root_->currentWidget()!=login_) { connectionProbe->stop(); return; }
        if(connectionProbe->property("pending").toBool()) return;
        connectionProbe->setProperty("pending",true);
        const QString checkedServer=api_.serverAddress();
        api_.get("/healthz",[this,connectionProbe,loginButton,checkedServer](const ApiResponse &result) {
            connectionProbe->setProperty("pending",false);
            if(result.ok && api_.serverAddress()==checkedServer && root_->currentWidget()==login_ && loginButton->isEnabled()) {
                connectionProbe->stop(); notify("服务器连接已恢复，请重新登录");
            }
        });
    });
    auto submit=[this,server,account,password,remember,loginButton,connectionProbe] {
        if(!loginButton->isEnabled()) return;
        const QString address=server->text().trimmed(), userName=account->text().trimmed();
        const auto match=QRegularExpression(R"(^([A-Za-z0-9.-]+)(?::([0-9]{1,5}))?$)").match(address);
        const int port=match.captured(2).isEmpty() ? 8088 : match.captured(2).toInt();
        const QString host=match.captured(1);
        bool validHost=match.hasMatch() && host.size()<=253;
        if(QRegularExpression("^[0-9.]+$").match(host).hasMatch()) {
            QHostAddress parsed; validHost=validHost && parsed.setAddress(host) && parsed.protocol()==QAbstractSocket::IPv4Protocol;
        } else {
            for(const QString &label:host.split('.'))
                validHost=validHost && QRegularExpression("^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?$").match(label).hasMatch();
        }
        if(!validHost || port<1 || port>65535) { notify("请输入有效的服务器 IP 或主机名",true); server->setFocus(); return; }
        if(userName.isEmpty()) { notify("请输入账号",true); account->setFocus(); return; }
        if(password->text().isEmpty()) { notify("请输入密码",true); password->setFocus(); return; }
        api_.setServer(match.captured(1),port);
        connectionProbe->stop();
        loginButton->setEnabled(false); loginButton->setText("连接中...");
        api_.request("POST","/auth/login",QJsonObject{{"username",userName},{"password",password->text()},
            {"clientVersion","qt-"+QCoreApplication::applicationVersion()}},[=](const ApiResponse &result) {
            loginButton->setEnabled(true); loginButton->setText("登录");
            if(!result.ok) {
                notify(result.status==0 ? "无法连接服务器，请检查地址和网络" : result.error.isEmpty() ? "登录失败" : result.error,true);
                if(result.status==0) connectionProbe->start();
                return;
            }
            const QJsonObject data=result.data.toObject();
            const QString token=data.value("token").toString();
            if(token.isEmpty()) { notify("登录失败",true); return; }
            auto settings=uiSettings("QtClient");
            settings.setValue("serverIp",address); settings.setValue("rememberPassword",remember->isChecked());
            settings.setValue("rememberedUsername",remember->isChecked() ? userName : QString());
            settings.setValue("rememberedPassword",remember->isChecked() ? password->text() : QString());
            QVariantMap rememberedHosts=settings.value("rememberedCredentials").toMap();
            QVariantMap rememberedAccounts=rememberedHosts.value(address).toMap();
            if(remember->isChecked()) rememberedAccounts[userName]=password->text();
            else rememberedAccounts.remove(userName);
            rememberedHosts[address]=rememberedAccounts;
            settings.setValue("rememberedCredentials",rememberedHosts);
            QStringList hosts=settings.value("serverHistory").toStringList(); hosts.removeAll(address); hosts.prepend(address);
            settings.setValue("serverHistory",hosts.mid(0,8));
            QStringList accounts=settings.value("accountHistory").toStringList(); accounts.removeAll(userName); accounts.prepend(userName);
            settings.setValue("accountHistory",accounts.mid(0,12));
            if(server->completer()) server->completer()->setModel(new QStringListModel(hosts.mid(0,8),server->completer()));
            if(account->completer()) account->completer()->setModel(new QStringListModel(accounts.mid(0,12),account->completer()));
            if(!remember->isChecked()) password->clear();
            enterSession(data.value("user").toObject(),token,data.value("serverVersion").toString(),data.value("deviceTcpPort").toInt(38400));
            notify("登录成功");
        },5000);
    };
    connect(loginButton,&QPushButton::clicked,this,submit);
    connect(password,&QLineEdit::returnPressed,this,submit);
    connect(account,&QLineEdit::returnPressed,this,submit);
    connect(server,&QLineEdit::returnPressed,this,submit);
    root_->addWidget(login_); root_->setCurrentWidget(login_);
}

void ClientWindow::createShell() {
    shell_=new QWidget; shell_->setObjectName("clientShell");
    auto *outer=new QVBoxLayout(shell_); outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);
    auto *top=new QFrame; top->setObjectName("clientTopbar"); top->setFixedHeight(45);
    auto *topLayout=new QHBoxLayout(top);topLayout->setObjectName("windowChromeLayout"); topLayout->setContentsMargins(12,0,12,0); topLayout->setSpacing(8);
    auto *logo=new QLabel; logo->setPixmap(QIcon(":/client-logo.png").pixmap(18,18)); topLayout->addWidget(logo);
    auto *title=new QLabel("博尔物联网管理软件客户端"); title->setObjectName("clientTopTitle");title->setMinimumWidth(1);title->setToolTip(windowTitle());
    bindVersionedProductTitle(title,"博尔物联网管理软件客户端","Bohr IoT Management Client");
    topLayout->addWidget(title); topLayout->addStretch();
    serverTag_=new QLabel; serverTag_->setObjectName("clientServerTag");serverTag_->setFixedHeight(43);serverTag_->setMaximumWidth(520);serverTag_->setMinimumWidth(0);serverTag_->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);topLayout->addWidget(serverTag_);
    auto *language=new QPushButton("中 / EN"); language->setObjectName("clientTopButton");
    language->setProperty("i18nSkip",true);
    language->setFixedWidth(80);
    topLayout->addWidget(language);
    connect(language,&QPushButton::clicked,this,[] {
        const bool en=!UiI18n::instance()->english();
        uiSettings("QtClient").setValue("language",en ? "en-US" : "zh-CN");
        UiI18n::instance()->setEnglish(en);
    });
    connect(UiI18n::instance(),&UiI18n::languageChanged,this,&ClientWindow::updateServerTag);
    auto *userButton=new QPushButton(user_.value("username").toString());
    userButton->setObjectName("clientUserButton"); userButton->setProperty("i18nSkip",true);
    userButton->setIcon(QIcon(":/default-avatar.svg")); userButton->setIconSize(QSize(22,22));
    userButton->setMinimumWidth(105);
    topLayout->addWidget(userButton);
    connect(userButton,&QPushButton::clicked,this,[this,userButton] {
        QMenu menu(userButton); menu.setObjectName("groupContextMenu");
        auto *profile=menu.addAction("个人中心"), *change=menu.addAction("修改密码");
        menu.addSeparator(); auto *leave=menu.addAction("退出登录");
        QAction *choice=menu.exec(userButton->mapToGlobal(QPoint(0,userButton->height()+2)));
        if(choice==profile) navigate("/profile/info");
        else if(choice==change) navigate("/profile/password");
        else if(choice==leave && UiMessageBox::question(this,"退出登录","确定要退出登录吗？")==UiMessageBox::Yes)
            logout(true);
    });
    outer->addWidget(top);
    auto *body=new QWidget; auto *bodyLayout=new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0,0,0,0); bodyLayout->setSpacing(0);
    auto *sidebar=new QFrame; sidebar->setObjectName("clientSidebar"); sidebar->setFixedWidth(205);
    auto *sideLayout=new QVBoxLayout(sidebar); sideLayout->setContentsMargins(0,8,0,0);
    navigation_=new QTreeWidget; navigation_->setObjectName("clientNavigation");
    navigation_->setHeaderHidden(true); navigation_->setIndentation(18);
    navigation_->setIconSize(QSize(18,18));
    navigation_->setFocusPolicy(Qt::NoFocus); navigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sideLayout->addWidget(navigation_); bodyLayout->addWidget(sidebar);
    pages_=new QStackedWidget; pages_->setObjectName("clientPages"); bodyLayout->addWidget(pages_,1);
    outer->addWidget(body,1);
    root_->addWidget(shell_);
    connect(navigation_,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *item,int) {
        const QString path=item->data(0,Qt::UserRole).toString();
        if(!path.isEmpty()) navigate(path);
        else {
            const bool expand=!item->isExpanded();
            navigation_->collapseAll(); item->setExpanded(expand);
        }
    });
    rebuildNavigation();
}

void ClientWindow::rebuildNavigation() {
    navigation_->clear();
    auto item=[this](QTreeWidgetItem *parent,const QString &title,const QString &path={},const QIcon &icon={}) {
        auto *row=parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(navigation_);
        row->setText(0,title); row->setData(0,Qt::UserRole,path);
        if(!icon.isNull()) row->setIcon(0,icon);
        return row;
    };
    if(hasPermission("home")) item(nullptr,"首页","/home",clientMenuIcon(0xe7b9));
    if(hasPermission("dashboard")) item(nullptr,"数据看板","/dashboard",clientMenuIcon(0xe768));
    if(hasPermission("deviceManagement")) {
        auto *group=item(nullptr,"设备管理",{},clientMenuIcon(0xe775));
        if(hasPermission("deviceInfo")) {
            item(group,"设备列表","/device/list");
            item(group,"设备类型","/device/types"); item(group,"电控类型","/device/electric-control-types");
        }
        if(hasPermission("remoteMonitoring")) {
            item(group,"远程监控","/device/monitor");
        }
    }
    if(hasPermission("fileManagement")) {
        auto *group=item(nullptr,"花型管理",{},clientMenuIcon(0xe78a));
        if(hasPermission("patternFiles") || hasPermission("devicePatternFiles")) item(group,"花型列表","/file/pattern");
        if(hasPermission("patternFiles")) {
            item(group,"花型类型管理","/file/pattern-types"); item(group,"订单编号管理","/file/order-nos");
        }
        if(hasPermission("downloadLog")) {
            item(group,"下发队列","/file/queue"); item(group,"下发日志","/file/log");
        }
    }
    if(hasPermission("statistics")) {
        auto *group=item(nullptr,"数据统计",{},clientMenuIcon(0xe7a8));
        if(hasPermission("salaryStatistics")) item(group,"工资统计","/statistics/salary");
        if(hasPermission("statusStatistics")) {
            item(group,"加工概况","/statistics/process"); item(group,"时长统计","/statistics/duration");
            item(group,"报警统计","/statistics/alarm");
        }
    }
    if(hasPermission("employeeManagement")) {
        auto *group=item(nullptr,"员工管理",{},clientMenuIcon(0xe6e3));
        item(group,"员工列表","/employee/list");
    }
    auto *profile=item(nullptr,"个人中心",{},clientMenuIcon(0xe7a5));
    item(profile,"基础信息","/profile/info"); item(profile,"修改密码","/profile/password");
    auto *support=item(nullptr,"服务支持",{},clientMenuIcon(0xe6d4));
    item(support,"联系客服","/support/contact"); item(support,"操作说明","/support/manual");
    navigation_->collapseAll();
}

bool ClientWindow::hasPermission(const QString &key) const {
    return user_.value("role").toString()=="admin" || permissions_.value(key).toBool();
}

QString ClientWindow::firstAccessiblePath() const {
    for(const QString &path:{"/home","/dashboard","/device/list","/device/monitor","/file/pattern","/file/queue",
        "/statistics/salary","/statistics/process","/employee/list","/profile/info"}) {
        if(path=="/file/pattern" &&
           (hasPermission("patternFiles") || hasPermission("devicePatternFiles"))) return path;
        const QString permission=path=="/home" ? "home" : path=="/dashboard" ? "dashboard" :
            path=="/device/monitor" ? "remoteMonitoring" :
            path.startsWith("/device") ? "deviceInfo" : path=="/file/pattern" ? "patternFiles" :
            path=="/file/queue" ? "downloadLog" : path=="/statistics/salary" ? "salaryStatistics" :
            path.startsWith("/statistics") ? "statusStatistics" : path.startsWith("/employee") ?
            "employeeManagement" : QString();
        if(permission.isEmpty() || hasPermission(permission)) return path;
    }
    return "/profile/info";
}

bool ClientWindow::allowedPath(const QString &path) const {
    if(path.startsWith("/profile/") || path.startsWith("/support/")) return true;
    if(path=="/home") return hasPermission("home");
    if(path=="/dashboard") return hasPermission("dashboard");
    if(path=="/device/monitor") return hasPermission("remoteMonitoring");
    if(path.startsWith("/device/")) return hasPermission("deviceInfo");
    if(path=="/file/pattern") return hasPermission("patternFiles") || hasPermission("devicePatternFiles");
    if(path=="/file/queue" || path=="/file/log") return hasPermission("downloadLog");
    if(path.startsWith("/file/")) return hasPermission("patternFiles");
    if(path=="/statistics/salary") return hasPermission("salaryStatistics");
    if(path.startsWith("/statistics/")) return hasPermission("statusStatistics");
    if(path.startsWith("/employee/")) return hasPermission("employeeManagement");
    return false;
}

void ClientWindow::enterSession(const QJsonObject &user,const QString &token,const QString &serverVersion,int deviceTcpPort) {
    deviceTcpPort_=deviceTcpPort;
    user_=user; permissions_=normalizedPermissions(user);
    api_.setToken(token); serverVersion_=serverVersion;
    if(!shell_) createShell(); else rebuildNavigation();
    updateUser(user);
    updateServerTag();
    root_->setCurrentWidget(shell_);
    navigate(firstAccessiblePath());
}

void ClientWindow::previewSession(const QJsonObject &user) {
    enterSession(user,"preview-token","qt-preview");
}

void ClientWindow::testSession(const QString &host,int port,const QJsonObject &user,const QString &token) {
    api_.setServer(host,port); enterSession(user,token,"qt-test");
}

void ClientWindow::updateUser(const QJsonObject &user) {
    user_=user; permissions_=normalizedPermissions(user);
    if(shell_) if(auto *button=shell_->findChild<QPushButton*>("clientUserButton")) {
        button->setText(user.value("username").toString());
        button->setIcon(QIcon(":/default-avatar.svg"));
        const QString avatar=user.value("avatar").toString().trimmed();
        if(!avatar.isEmpty() && api_.token()!="preview-token") {
            const QUrl url=avatar.startsWith("http://") || avatar.startsWith("https://") ?
                QUrl(avatar) : QUrl(api_.baseUrl()+avatar);
            auto *reply=avatarNetwork_.get(QNetworkRequest(url));
            const QPointer<QPushButton> safeButton(button);
            connect(reply,&QNetworkReply::finished,this,[this,reply,safeButton,avatar] {
                if(safeButton && user_.value("avatar").toString().trimmed()==avatar &&
                   reply->error()==QNetworkReply::NoError) {
                    QPixmap picture;
                    if(picture.loadFromData(reply->readAll())) safeButton->setIcon(QIcon(picture));
                }
                reply->deleteLater();
            });
        }
    }
}

void ClientWindow::updateServerTag() {
    if(!serverTag_) return;
    const bool english=UiI18n::instance() && UiI18n::instance()->english();
    serverTag_->setProperty("i18nSkip",true);
    serverTag_->setWordWrap(false);
    serverTag_->setText(QString("%1: %2\n%3: %4")
        .arg(english ? "Server Version" : "服务器版本",
             serverVersion_.isEmpty() ? (english ? "Unknown" : "未知") : serverVersion_,
             english ? "Server Address" : "服务器地址",api_.serverAddress()));
    serverTag_->setToolTip(serverTag_->text()+QString("\n%1: %2").arg(english ? "Device TCP Port" : "设备TCP端口").arg(deviceTcpPort_));
}

void ClientWindow::navigate(const QString &path) {
    if(!pages_) return;
    if(!allowedPath(path)) { notify("没有权限访问",true); return; }
    ClientPage *page=pageMap_.value(path,nullptr);
    if(!page) {
        page=createClientPage(path,&api_,this);
        if(!page) return;
        pageMap_.insert(path,page); pages_->addWidget(page);
    }
    if(auto *list=dynamic_cast<ListPage*>(page))list->clearContextQuery();for(auto *widget:page->findChildren<QWidget*>())if(auto *list=dynamic_cast<ListPage*>(widget))list->clearContextQuery();
    currentPath_=path; pages_->setCurrentWidget(page);
    for(QTreeWidgetItemIterator it(navigation_);*it;++it)
        if((*it)->data(0,Qt::UserRole).toString()==path) {
            navigation_->collapseAll();
            if((*it)->parent()) (*it)->parent()->setExpanded(true);
            navigation_->setCurrentItem(*it); break;
        }
    page->refresh();
}

void ClientWindow::navigateFiltered(const QString &path,const QString &key,const QString &value) {
    navigate(path);
    ClientPage *page=pageMap_.value(path,nullptr);
    if(!page) return;
    if(path=="/device/monitor" && key=="deviceId") {page->setProperty("pendingDeviceId",value.toLongLong());page->refresh();return;}
    for(auto *button:page->findChildren<QPushButton*>()) if(button->property("clearGroupSelection").toBool()) { button->click(); break; }
    for(QWidget *widget:page->findChildren<QWidget*>())
        if(auto *list=dynamic_cast<ListPage*>(widget)) {
            list->setExtraQuery(key,value);
            break;
        }
}

void ClientWindow::logout(bool notifyServer) {
    if(notifyServer && !api_.token().isEmpty()) api_.post("/auth/logout",QJsonObject{},[](const ApiResponse&){});
    api_.clearToken(); user_={}; permissions_={}; currentPath_.clear();
    root_->setCurrentWidget(login_);
    // Invalidate old API callbacks before deleting pages that captured their widgets.
    if(shell_) { root_->removeWidget(shell_); shell_->deleteLater(); }
    shell_=nullptr; pages_=nullptr; navigation_=nullptr; serverTag_=nullptr; pageMap_.clear();
}

void ClientWindow::notify(const QString &message,bool error) {
    if(!toast_) {
        toast_=new QLabel(this); toast_->setObjectName("clientToast");
        toast_->setAttribute(Qt::WA_TransparentForMouseEvents); toast_->setAlignment(Qt::AlignCenter);
        toastTimer_=new QTimer(toast_);toastTimer_->setSingleShot(true);
        connect(toastTimer_,&QTimer::timeout,toast_,&QWidget::hide);
    }
    toast_->setText(UiI18n::instance() ? UiI18n::instance()->text(message) : message);
    toast_->setProperty("error",error);
    toast_->style()->unpolish(toast_); toast_->style()->polish(toast_);
    toast_->show();UiWindowGeometry::fitNotification(toast_,this,63);toast_->raise();
    QTimer::singleShot(0,toast_,[this]{if(toast_->isVisible())UiWindowGeometry::fitNotification(toast_,this,63);});
    toastTimer_->start(error ? 3200 : 2000);
}

void ClientWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if(toast_ && toast_->isVisible()) UiWindowGeometry::fitNotification(toast_,this,63);
}
