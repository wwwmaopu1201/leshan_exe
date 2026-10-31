#include "profile_support_pages.h"
#include "api_client.h"
#include "client_window.h"
#include "client_file_dialog.h"
#include "page_widgets.h"
#include "ui_controls.h"
#include "ui_i18n.h"

#include <QDesktopServices>
#include <QCoreApplication>
#include <QAction>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QTextEdit>
#include <QToolBox>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QWidget *scrollContent(QWidget *body,QWidget *parent) {
    auto *scroll=new QScrollArea(parent); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true); scroll->setWidget(body);
    if(parent->property("personalCenter").toBool()) {
        scroll->setStyleSheet("QScrollArea { background:white; border:0; }");
        scroll->viewport()->setObjectName("personalCenterViewport");
        scroll->viewport()->setStyleSheet("QWidget#personalCenterViewport { background:white; }");
        body->setObjectName("personalCenterBody");
    }
    return scroll;
}
class BasicInfoPage : public ClientPage {
public:
    BasicInfoPage(ApiClient *api,ClientWindow *window):api_(api),window_(window) {
        setObjectName("clientPage");setProperty("personalCenter",true);setAttribute(Qt::WA_StyledBackground,true);
        auto *outer=new QVBoxLayout(this); outer->setContentsMargins(0,0,0,0);
        auto *body=new QWidget; auto *layout=new QVBoxLayout(body);
        layout->setContentsMargins(9,9,9,9); layout->setSpacing(9);
        auto *topRow=new QHBoxLayout; topRow->setSpacing(18);
        auto *overview=clientCard(); overview->setFixedWidth(360); overview->setMinimumHeight(420);
        auto *overviewLayout=new QVBoxLayout(overview); overviewLayout->setContentsMargins(12,12,12,12);
        auto *overviewHead=new QHBoxLayout;
        avatar_=new QPushButton; avatar_->setIcon(QIcon(":/default-avatar.svg"));
        avatar_->setIconSize(QSize(88,88)); avatar_->setFixedSize(96,96);
        overviewHead->addWidget(avatar_);
        auto *identity=new QVBoxLayout;
        name_=new QLabel("-"); name_->setWordWrap(true); name_->setObjectName("clientPageTitle"); name_->setProperty("i18nSkip",true);
        identity->addWidget(name_);
        summary_=clientHint("账号：-   ·   角色：-"); summary_->setProperty("i18nSkip",true);
        identity->addWidget(summary_); identity->addStretch(); overviewHead->addLayout(identity,1);
        overviewLayout->addLayout(overviewHead);
        auto *summaryGrid=new QGridLayout; summaryGrid->setSpacing(9);
        const QStringList captions={"账号姓名","手机号","创建时间","角色名称"};
        for(int i=0;i<captions.size();++i) {
            auto *cell=clientCard(); auto *box=new QVBoxLayout(cell);
            box->setContentsMargins(12,9,12,9);
            box->addWidget(clientHint(captions[i]));
            auto *value=new QLabel("-"); value->setWordWrap(true); value->setObjectName("profileValue");
            value->setProperty("i18nSkip",true); box->addWidget(value);
            profileSummary_ << value; summaryGrid->addWidget(cell,i/2,i%2);
        }
        overviewLayout->addLayout(summaryGrid);
        overviewLayout->addStretch();
        overviewLayout->addWidget(clientHint("此页面仅维护当前账号基本信息；密码修改和语言切换已分别收口到标题栏与修改密码页面。"));
        topRow->addWidget(overview);
        connect(avatar_,&QPushButton::clicked,this,&BasicInfoPage::uploadAvatar);
        auto *edit=clientCard(); edit->setMinimumHeight(420); auto *editLayout=new QVBoxLayout(edit);
        editLayout->addWidget(clientHeading("基本资料"));
        editLayout->addWidget(clientHint("修改昵称和手机号后会立即同步到当前登录账号。"));
        auto *form=new QGridLayout; form->setSpacing(11);
        username_=new QLineEdit; username_->setReadOnly(true);
        nickname_=new QLineEdit; nickname_->setMaxLength(50);
        phone_=new QLineEdit; phone_->setMaxLength(20);
        nickname_->setProperty("profileField","nickname"); phone_->setProperty("profileField","phone");
        role_=new QLineEdit; role_->setReadOnly(true);
        created_=new QLineEdit; created_->setReadOnly(true);
        for(auto *input:{username_,nickname_,phone_,role_,created_}) input->setMinimumHeight(34);
        form->addWidget(new QLabel("账号"),0,0); form->addWidget(username_,0,1);
        form->addWidget(new QLabel("账号姓名"),0,2); form->addWidget(nickname_,0,3);
        nicknameError_=new QLabel; phoneError_=new QLabel;
        for(auto *label:{nicknameError_,phoneError_}) {label->setObjectName("promptError");label->setWordWrap(true);label->hide();}
        nicknameError_->setProperty("profileField","nicknameError");phoneError_->setProperty("profileField","phoneError");
        form->addWidget(nicknameError_,1,3);
        form->addWidget(new QLabel("手机号"),2,0); form->addWidget(phone_,2,1);
        form->addWidget(new QLabel("角色名称"),2,2); form->addWidget(role_,2,3);
        form->addWidget(phoneError_,3,1);
        form->addWidget(new QLabel("创建时间"),4,0); form->addWidget(created_,4,1,1,3);
        editLayout->addLayout(form);
        auto *buttons=new QHBoxLayout;
        auto *save=new QPushButton("更新资料"), *reset=new QPushButton("重置");
        save->setObjectName("primaryButton"); buttons->addWidget(save); buttons->addWidget(reset);
        buttons->addStretch(); editLayout->addLayout(buttons); editLayout->addStretch();
        topRow->addWidget(edit,1); layout->addLayout(topRow);
        connect(save,&QPushButton::clicked,this,[this] {
            const QString nickname=nickname_->text().trimmed(); QString phone=phone_->text(); phone.remove(QRegularExpression("\\s+"));
            const QString nameError=nickname.isEmpty() ? "账号姓名不能为空" : QString();
            const QString phoneError=!QRegularExpression("^1[3-9][0-9]{9}$").match(phone).hasMatch() ? "手机号格式需正确" : QString();
            nicknameError_->setText(nameError); nicknameError_->setVisible(!nameError.isEmpty()); phoneError_->setText(phoneError); phoneError_->setVisible(!phoneError.isEmpty());
            if(!nameError.isEmpty() || !phoneError.isEmpty()) return;
            api_->put("/auth/profile",QJsonObject{{"nickname",nickname},{"phone",phone}},[this,nickname,phone](const ApiResponse &result) {
                if(!result.ok) { window_->notify(result.error,true); return; }
                QJsonObject updated=window_->user();
                updated["nickname"]=nickname; updated["phone"]=phone;
                window_->updateUser(updated); applyUser(updated);
                window_->notify("资料已更新");
            });
        });
        connect(reset,&QPushButton::clicked,this,[this] { applyUser(window_->user()); });
        auto *logs=clientCard(); auto *logsLayout=new QVBoxLayout(logs);
        logsLayout->addWidget(clientHeading("登录记录"));
        logsLayout->addWidget(clientHint("用于核对近期登录时间、终端信息和登录结果。"));
        logTable_=new ClientTable;
        logTable_->setColumns({{"登录时间","loginTime",180},{"IP地址","ip",150},
            {"设备","device",200},{"状态","status",110}});
        logTable_->setMinimumHeight(230); logsLayout->addWidget(logTable_,1);
        layout->addWidget(logs,1);
        outer->addWidget(scrollContent(body,this));
        connect(UiI18n::instance(),&UiI18n::languageChanged,this,[this]{ updateLabels(); });
    }
    void refresh() override {
        applyUser(window_->user());
        if(api_->token()=="preview-token") return;
        api_->get("/auth/userinfo",[this](const ApiResponse &result) {
            if(!result.ok) return;
            window_->updateUser(result.data.toObject()); applyUser(result.data.toObject());
        });
        api_->get("/auth/login-logs?page=1&pageSize=20",[this](const ApiResponse &result) {
            if(result.ok) logTable_->setRecords(apiRows(result.data));
        });
    }
private:
    void applyUser(const QJsonObject &user) {
        name_->setText(user.value("nickname").toString(user.value("username").toString()));
        username_->setText(user.value("username").toString());
        nickname_->setText(user.value("nickname").toString());
        phone_->setText(user.value("phone").toString());
        created_->setText(user.value("createdAt").toString());
        profileSummary_[0]->setText(nickname_->text().isEmpty() ? "-" : nickname_->text());
        profileSummary_[1]->setText(phone_->text().isEmpty() ? "-" : phone_->text());
        profileSummary_[2]->setText(created_->text().isEmpty() ? "-" : created_->text());
        name_->setToolTip(name_->text()); for(auto *value:profileSummary_) value->setToolTip(value->text());
        nicknameError_->hide(); phoneError_->hide();
        updateLabels();
        const QString path=user.value("avatar").toString();
        if(!path.isEmpty() && api_->token()!="preview-token") {
            auto *reply=avatarNetwork_.get(QNetworkRequest(QUrl(api_->baseUrl()+path)));
            connect(reply,&QNetworkReply::finished,this,[this,reply] {
                if(reply->error()==QNetworkReply::NoError) {
                    QPixmap image; if(image.loadFromData(reply->readAll()))
                        avatar_->setIcon(QIcon(image.scaled(88,88,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation)));
                }
                reply->deleteLater();
            });
        }
    }
    void uploadAvatar() {
        const QString path=ClientFileDialog::getOpenFileName(this,"选择头像",{},"Images (*.png *.jpg *.jpeg *.webp)");
        if(path.isEmpty()) return;
        if(QFileInfo(path).size()>2*1024*1024) { window_->notify("头像文件不能超过 2MB",true); return; }
        api_->uploadFile("/auth/avatar",path,{},[this](const ApiResponse &result) {
            if(!result.ok) { window_->notify(result.error,true); return; }
            QJsonObject user=window_->user(); user["avatar"]=result.data.toObject().value("avatar");
            window_->updateUser(user); applyUser(user); window_->notify("头像更新成功");
        });
    }
    void updateLabels() {
        const bool english=UiI18n::instance() && UiI18n::instance()->english();
        role_->setText(window_->user().value("role").toString()=="admin" ?
            (english ? "Administrator" : "管理员") : window_->user().value("role").toString());
        if(profileSummary_.size()>3) profileSummary_[3]->setText(role_->text());
        summary_->setText(QString("%1: %2   ·   %3: %4")
            .arg(english ? "Account" : "账号",username_->text(),
                 english ? "Role" : "角色",role_->text()));
    }
    ApiClient *api_; ClientWindow *window_;
    QNetworkAccessManager avatarNetwork_;
    QPushButton *avatar_; QLabel *name_,*summary_;
    QLineEdit *username_,*nickname_,*phone_,*role_,*created_;
    QLabel *nicknameError_,*phoneError_;
    QList<QLabel*> profileSummary_;
    ClientTable *logTable_;
};

class PasswordCard : public QFrame {
public:
    PasswordCard(){setObjectName("clientCard");setProperty("passwordPanel",true);setMaximumWidth(640);}
    QSize sizeHint() const override {QSize preferred=QFrame::sizeHint();preferred.setWidth(640);return preferred;}
};
class PasswordPage : public ClientPage {
public:
    PasswordPage(ApiClient *api,ClientWindow *window):api_(api),window_(window) {
        setObjectName("clientPage");setProperty("personalCenter",true);setAttribute(Qt::WA_StyledBackground,true);
        auto *outer=new QVBoxLayout(this);outer->setContentsMargins(24,24,24,24);outer->setSpacing(20);
        auto *header=clientPageHeader("个人中心","修改当前账号的登录密码。");header->layout()->setContentsMargins(0,0,0,8);outer->addWidget(header);
        auto *panel=new PasswordCard;
        auto *layout=new QVBoxLayout(panel); layout->setContentsMargins(0,0,0,12); layout->setSpacing(18);
        layout->addWidget(clientHeading("修改密码"));
        avatar_=new QPushButton;
        avatar_->setObjectName("profilePasswordAvatar");
        avatar_->setIcon(QIcon(":/default-avatar.svg")); avatar_->setIconSize(QSize(88,88));
        avatar_->setFixedSize(96,96);
        avatar_->setToolTip("点击更换头像");
        connect(avatar_,&QPushButton::clicked,this,&PasswordPage::uploadAvatar);
        auto *form=new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::DontWrapRows);
        form->setLabelAlignment(Qt::AlignRight|Qt::AlignVCenter);
        form->setHorizontalSpacing(16);form->setVerticalSpacing(16);
        form->addRow(QString(),avatar_);
        old_=new QLineEdit; newer_=new QLineEdit; confirm_=new QLineEdit;
        for(auto *line:{old_,newer_,confirm_}) {
            line->setObjectName("profilePasswordInput");
            line->setEchoMode(QLineEdit::Password);line->setMinimumWidth(240);line->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
            auto *visibility=line->addAction(passwordVisibilityIcon(false),QLineEdit::TrailingPosition);visibility->setCheckable(true);
            connect(visibility,&QAction::toggled,line,[line,visibility](bool visible) {line->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);visibility->setIcon(passwordVisibilityIcon(visible));});
        }
        old_->setProperty("profilePasswordField","old");newer_->setProperty("profilePasswordField","new");confirm_->setProperty("profilePasswordField","confirm");
        old_->setPlaceholderText("请输入旧密码"); newer_->setPlaceholderText("请输入新密码");
        confirm_->setPlaceholderText("请确认新密码");
        form->addRow("旧密码",old_); form->addRow("新密码",newer_); form->addRow("确认密码",confirm_);
        layout->addLayout(form);
        auto *actionRow=new QWidget;actionRow->setMinimumHeight(40);auto *actions=new QHBoxLayout(actionRow);actions->setContentsMargins(0,0,0,0);actions->setSpacing(12);
        auto *save=new QPushButton("保存"), *close=new QPushButton("关闭");
        save->setObjectName("primaryButton");
        for(auto *button:{save,close}){button->setProperty("passwordAction",true);button->setFixedHeight(36);}
        save->setProperty("profilePasswordAction","save");close->setProperty("profilePasswordAction","close");
        // "关闭" is also a system-state word (Off); use this action's context.
        close->setProperty("i18nSkip",true);
        if(auto *i18n=UiI18n::instance()) {
            auto translate=[i18n,close]{close->setText(i18n->english() ? "Close" : "关闭");};
            connect(i18n,&UiI18n::languageChanged,close,translate);translate();
        }
        actions->addWidget(save);actions->addWidget(close);actions->addStretch();
        form->addRow(QString(),actionRow);
        outer->addWidget(panel,0,Qt::AlignLeft|Qt::AlignTop);outer->addStretch();
        connect(close,&QPushButton::clicked,this,[this]{ window_->navigate("/profile/info"); });
        connect(save,&QPushButton::clicked,this,[this] {
            const QString old=old_->text(), next=newer_->text();
            if(old.isEmpty() || next.size()<6 || next.size()>32 || next==old || next!=confirm_->text()) {
                window_->notify("请检查旧密码、新密码和确认密码",true); return;
            }
            api_->put("/auth/password",QJsonObject{{"oldPassword",old},{"newPassword",next}},
                [this](const ApiResponse &result) {
                    if(!result.ok) { window_->notify(result.error,true); return; }
                    old_->clear(); newer_->clear(); confirm_->clear();
                    window_->notify("密码修改成功"); window_->navigate("/profile/info");
                });
        });
    }
    void refresh() override {
        avatar_->setIcon(QIcon(":/default-avatar.svg"));
        const QString path=window_->user().value("avatar").toString().trimmed();
        if(path.isEmpty() || api_->token()=="preview-token") return;
        const QUrl url=path.startsWith("http://") || path.startsWith("https://") ?
            QUrl(path) : QUrl(api_->baseUrl()+path);
        auto *reply=avatarNetwork_.get(QNetworkRequest(url));
        const QPointer<QPushButton> safe(avatar_);
        connect(reply,&QNetworkReply::finished,this,[this,reply,safe,path] {
            if(safe && window_->user().value("avatar").toString().trimmed()==path &&
               reply->error()==QNetworkReply::NoError) {
                QPixmap image;
                if(image.loadFromData(reply->readAll())) safe->setIcon(QIcon(image));
            }
            reply->deleteLater();
        });
    }
private:
    void uploadAvatar() {
        const QString path=ClientFileDialog::getOpenFileName(this,"选择头像",{},"Images (*.png *.jpg *.jpeg *.webp)");
        if(path.isEmpty()) return;
        if(QFileInfo(path).size()>2*1024*1024) { window_->notify("头像文件不能超过 2MB",true); return; }
        api_->uploadFile("/auth/avatar",path,{},[this](const ApiResponse &result) {
            if(!result.ok) { window_->notify(result.error,true); return; }
            QJsonObject user=window_->user(); user["avatar"]=result.data.toObject().value("avatar");
            window_->updateUser(user); refresh(); window_->notify("头像更新成功");
        });
    }
    ApiClient *api_; ClientWindow *window_;
    QLineEdit *old_,*newer_,*confirm_;
    QPushButton *avatar_;
    QNetworkAccessManager avatarNetwork_;
};

class ContactPage : public ClientPage {
public:
    ContactPage(ClientWindow *window):window_(window) {
        setObjectName("clientPage");
        auto *outer=new QVBoxLayout(this); outer->setContentsMargins(0,0,0,0);
        auto *body=new QWidget; auto *layout=new QVBoxLayout(body);
        layout->setContentsMargins(9,9,9,9); layout->setSpacing(9);
        auto *top=new QHBoxLayout; top->setSpacing(9);
        auto *info=clientCard(); auto *infoLayout=new QVBoxLayout(info);
        infoLayout->addWidget(clientHeading("联系客服"));
        infoLayout->addWidget(clientHint("需要协助时可通过电话、邮件或在线留言联系我们。"));
        int contactIndex=0;
        for(const auto &pair:QList<QPair<QString,QString>>{
                {"客服热线","18913754308"},{"电子邮箱","contact@bohr123.com"},
                {"工作时间","周一至周五 8:30--17:30"},
                {"地址","常熟高新技术产业开发区东南大道788号1幢"}}) {
            auto *entry=new QFrame; entry->setObjectName("contactItem");
            auto *row=new QHBoxLayout(entry); row->setContentsMargins(16,10,16,10); row->setSpacing(13);
            auto *icon=new QLabel; icon->setObjectName("contactIcon"+QString::number(contactIndex));
            icon->setFixedSize(48,48); icon->setAlignment(Qt::AlignCenter);
            const QStringList icons={":/contact-phone.svg",":/contact-mail.svg",":/contact-clock.svg",":/contact-location.svg"};
            icon->setPixmap(QIcon(icons[contactIndex]).pixmap(22,22)); row->addWidget(icon);
            auto *copy=new QVBoxLayout; copy->setSpacing(5);
            copy->addWidget(clientHint(pair.first));
            auto *value=new QLabel(pair.second);
            value->setWordWrap(true);
            if(pair.first=="客服热线" || pair.first=="电子邮箱") value->setProperty("i18nSkip",true);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse); copy->addWidget(value);
            if(contactIndex==3) {
                auto *map=new QPushButton("查看地图"); map->setObjectName("clientLink");
                copy->addWidget(map,0,Qt::AlignLeft);
                connect(map,&QPushButton::clicked,this,[] {
                    QDesktopServices::openUrl(QUrl("https://uri.amap.com/search?keyword="+
                        QString::fromLatin1(QUrl::toPercentEncoding("常熟高新技术产业开发区东南大道788号1幢"))));
                });
            }
            row->addLayout(copy,1); infoLayout->addWidget(entry);
            ++contactIndex;
        }
        infoLayout->addStretch(); info->setMinimumHeight(485); top->addWidget(info,1);
        auto *message=clientCard(); auto *messageLayout=new QVBoxLayout(message);
        messageLayout->addWidget(clientHeading("在线留言"));
        messageLayout->addWidget(clientHint("填写后将在邮件程序中打开草稿，由你确认发送。"));
        auto *form=new QFormLayout; form->setSpacing(10);
        name_=new QLineEdit; contact_=new QLineEdit; type_=new UiComboBox; description_=new QTextEdit;
        name_->setProperty("contactField","name"); contact_->setProperty("contactField","contact");
        type_->setProperty("contactField","type"); description_->setProperty("contactField","description");
        type_->addItem("请选择类型",QString());
        type_->addItem("使用咨询","usage"); type_->addItem("意见建议","suggestion");
        type_->addItem("故障反馈","bug"); type_->addItem("其他","other");
        description_->setMinimumHeight(160);
        form->addRow("姓名",name_); form->addRow("联系方式",contact_);
        form->addRow("问题类型",type_); form->addRow("问题描述",description_);
        messageLayout->addLayout(form);
        auto *buttons=new QHBoxLayout;
        auto *submit=new QPushButton("提交留言"), *reset=new QPushButton("重置");
        submit->setObjectName("primaryButton"); buttons->addWidget(submit); buttons->addWidget(reset);
        buttons->addStretch(); messageLayout->addLayout(buttons); messageLayout->addStretch();
        message->setMinimumHeight(485); top->addWidget(message,1);
        layout->addLayout(top);
        connect(reset,&QPushButton::clicked,this,[this]{ name_->clear(); contact_->clear(); type_->setCurrentIndex(0); description_->clear(); });
        connect(submit,&QPushButton::clicked,this,[this] {
            if(name_->text().trimmed().isEmpty() || contact_->text().trimmed().isEmpty() ||
               type_->currentData().toString().isEmpty() || description_->toPlainText().trimmed().isEmpty()) {
                window_->notify("请填写完整留言信息",true); return;
            }
            const QString subject="客户留言 - "+name_->text().trimmed();
            const QString body="姓名："+name_->text()+"\n联系方式："+contact_->text()+
                "\n类型："+type_->currentText()+"\n\n问题描述：\n"+description_->toPlainText();
            const QUrl mail("mailto:chengjing@bohr123.com?subject="+
                QString::fromLatin1(QUrl::toPercentEncoding(subject))+"&body="+
                QString::fromLatin1(QUrl::toPercentEncoding(body)));
            if(QDesktopServices::openUrl(mail))
                window_->notify("请在邮件程序中完成发送，当前尚未提交留言。");
            else window_->notify("无法打开邮件程序",true);
        });
        auto *faq=clientCard(); auto *faqLayout=new QVBoxLayout(faq);
        faqLayout->addWidget(clientHeading("常见问题"));
        const QList<QPair<QString,QString>> questions={
            {"无法连接服务器怎么办？","请检查局域网、服务端运行状态和 IP 地址。"},
            {"设备显示关机怎么处理？","确认设备已开机、网线和地址配置正常。"},
            {"花型下发失败怎么办？","检查设备连接、花型格式，并在下发队列中重试。"},
            {"如何导出统计？","进入统计页面，点击右上角的导出按钮。"}};
        for(int index=0;index<questions.size();++index) {
            const auto &entry=questions[index];
            auto *question=new QPushButton(QString("%1. %2").arg(index+1).arg(entry.first));
            question->setObjectName("contactFaqQuestion"); question->setCheckable(true);
            faqLayout->addWidget(question);
            auto *answer=clientHint(entry.second); answer->setObjectName("contactFaqAnswer");
            answer->setVisible(false); faqLayout->addWidget(answer);
            connect(question,&QPushButton::toggled,this,[question,answer,faq](bool checked) {
                answer->setVisible(checked);
                if(checked) for(auto *other:faq->findChildren<QPushButton*>("contactFaqQuestion"))
                    if(other!=question) other->setChecked(false);
            });
        }
        faq->setMinimumHeight(310); faqLayout->addStretch(); layout->addWidget(faq);
        outer->addWidget(scrollContent(body,this));
    }
private:
    ClientWindow *window_; QLineEdit *name_,*contact_;
    UiComboBox *type_; QTextEdit *description_;
};

class ManualPage : public ClientPage {
public:
    ManualPage(ClientWindow *window):window_(window) {
        setObjectName("clientPage");
        auto *layout=new QHBoxLayout(this); layout->setContentsMargins(9,9,9,9); layout->setSpacing(9);
        auto *nav=clientCard(); nav->setFixedWidth(220); auto *navLayout=new QVBoxLayout(nav);
        navLayout->addWidget(clientHeading("操作目录"));
        auto *sections=new QListWidget;
        const QStringList titles={"软件概述","安装指南","登录使用","设备管理","花型管理","数据统计","常见问题"};
        for(int i=0;i<titles.size();++i) sections->addItem(QString::number(i+1)+". "+titles[i]);
        navLayout->addWidget(sections); layout->addWidget(nav);
        auto *scroll=new QScrollArea; scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true);
        auto *body=new QWidget; auto *bodyLayout=new QVBoxLayout(body);
        bodyLayout->setContentsMargins(0,0,0,0); bodyLayout->setSpacing(9);
        auto *head=clientCard(); auto *headLayout=new QHBoxLayout(head);
        auto *heading=new QVBoxLayout;
        heading->addWidget(clientHeading("操作说明"));
        heading->addWidget(clientHint("帮助安装、登录、设备管理和日常排查快速上手。"));
        headLayout->addLayout(heading); headLayout->addStretch();
        headLayout->addWidget(clientHint("版本：qt-"+QCoreApplication::applicationVersion()));
        auto *download=new QPushButton("下载说明"); download->setObjectName("primaryButton");
        headLayout->addWidget(download); bodyLayout->addWidget(head);
        connect(download,&QPushButton::clicked,this,[this] {
            const QString target=ClientFileDialog::getSaveFileName(this,"下载操作说明","client-user-manual.pdf","PDF (*.pdf)");
            if(target.isEmpty()) return;
            QFile resource(":/client-manual.pdf"); QSaveFile output(target);
            if(!resource.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
                window_->notify("下载说明失败",true); return;
            }
            while(!resource.atEnd()) if(output.write(resource.read(65536))<0) {
                window_->notify("下载说明失败",true); return;
            }
            window_->notify(output.commit() ? "说明已下载" : "下载说明失败",!QFileInfo::exists(target));
        });
        const QList<QStringList> sectionsText={
            {"博尔物联网管理软件客户端是一款面向工业缝制设备的物联网客户端，用于设备状态监控、花型文件管理、统计分析和日常维护。",
             "查看设备状态、转速、累计针数和报警信息","花型文件上传、下发和回传","工资、效率、时长、报警多维统计"},
            {"操作系统：以当前 Qt 发行包说明为准","运行环境：请使用与操作系统匹配的 Qt 发行包。","磁盘空间：为花型文件和导出数据预留足够空间。",
             "网络要求：与服务器和设备处于同一局域网","启动发行包中的 Qt 客户端程序。",
             "Windows 可执行程序名为 bohr-qt-client.exe。","首次运行前，确认服务端地址可访问。"},
            {"输入服务器 IP 地址。","填写账号和密码，点击登录。","客户端默认连接管理端口 8088。",
             "需要时可勾选“记住密码”。","如果登录失败，请先确认客户端与服务端网络是否互通。"},
            {"进入“设备管理”，在左侧设备树中查看分组与设备。",
             "可直接在设备树中新增、重命名、移动或删除分组。",
             "点击设备行的“编辑”按钮修改设备信息。","未分组设备显示在“未分组设备”范围，可移入目标组。"},
            {"上传花型时可填写类型、针数、工价和订单编号。订单编号只允许字母和数字。",
             "选择目标设备后可批量下发花型。","回传后可在服务器花型文件中维护类型、针数、工价和订单编号。"},
            {"工资统计：查看员工工资、加工件数和趋势。",
             "加工概况：查看设备产量、用线量和效率分布。",
             "时长统计：统计运行、空闲、报警时长。","报警统计：分析报警类型与时间趋势。"},
            {"无法连接服务器怎么办？请检查网络连接，确认服务器 IP 是否正确，客户端默认使用 8088 端口。",
             "关机状态表示当前无设备连接，请检查设备电源、网线及服务端地址。",
             "花型下发失败怎么办？检查设备是否开机连接，确认花型格式正确，并重试下发。"}};
        for(int i=0;i<titles.size();++i) {
            auto *card=clientCard(); auto *sectionLayout=new QVBoxLayout(card);
            sectionLayout->addWidget(clientHeading(QString::number(i+1)+". "+titles[i]));
            for(const QString &line:sectionsText[i]) {
                auto *text=clientHint(line);
                text->setContentsMargins(9,4,9,4);
                sectionLayout->addWidget(text);
            }
            bodyLayout->addWidget(card);
            cards_ << card;
        }
        bodyLayout->addStretch(); scroll->setWidget(body); layout->addWidget(scroll,1);
        connect(sections,&QListWidget::currentRowChanged,this,[this,scroll](int index) {
            if(index>=0 && index<cards_.size()) scroll->ensureWidgetVisible(cards_[index],10,10);
        });
    }
private:
    ClientWindow *window_; QList<QWidget*> cards_;
};
}

ClientPage *createProfileSupportPage(const QString &path,ApiClient *api,ClientWindow *window) {
    if(path=="/profile/info") return new BasicInfoPage(api,window);
    if(path=="/profile/password") return new PasswordPage(api,window);
    if(path=="/support/contact") return new ContactPage(window);
    if(path=="/support/manual") return new ManualPage(window);
    return nullptr;
}
