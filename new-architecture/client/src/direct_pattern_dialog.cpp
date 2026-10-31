#include "direct_pattern_dialog.h"
#include "api_client.h"
#include "ui_dialogs.h"
#include "ui_i18n.h"
#include "page_widgets.h"
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QNetworkReply>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
namespace { QString trDirect(const char *zh,const char *en){return QString::fromUtf8(UiI18n::instance() && UiI18n::instance()->english() ? en : zh);} }
DirectPatternDialog::DirectPatternDialog(QWidget *parent,ApiClient *api,const QList<QPair<QString,QString>> &files,const QJsonArray &devices):QDialog(parent),api_(api) {
    setObjectName("editorDialog");setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);setProperty("fixedModalPosition",true);UiModalDialog::attach(this,parent);resize(760,480);
    auto *outer=new QVBoxLayout(this);outer->setContentsMargins(20,18,20,18);outer->setSpacing(12);
    title_=clientHeading("");outer->addWidget(title_);
    hint_=clientHint("");outer->addWidget(hint_);
    table_=new QTableWidget;table_->setObjectName("clientTable");table_->setColumnCount(3);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->verticalHeader()->hide();table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);outer->addWidget(table_,1);
    for(const auto &file:files)for(const auto &v:devices){const auto device=v.toObject();Item item;item.path=file.first;item.name=file.second;item.deviceId=device.value("id").toInteger();item.deviceName=device.value("displayName").toString(device.value("name").toString());items_.append(item);}
    table_->setRowCount(items_.size());for(int row=0;row<items_.size();++row)for(int col=0;col<3;++col)table_->setItem(row,col,new QTableWidgetItem);
    bar_=new QProgressBar;bar_->setRange(0,100);outer->addWidget(bar_);status_=clientHint("");outer->addWidget(status_);
    auto *actions=new QHBoxLayout;actions->addStretch();close_=new QPushButton;actions->addWidget(close_);outer->addLayout(actions);
    close_->setObjectName("directTransferClose");
    connect(close_,&QPushButton::clicked,this,&DirectPatternDialog::reject);
    setProperty("i18nSkip",true);for(auto *widget:findChildren<QWidget*>())widget->setProperty("i18nSkip",true);
    if(auto *i18n=UiI18n::instance())connect(i18n,&UiI18n::languageChanged,this,[this]{render();});
    timer_.setInterval(1000);connect(&timer_,&QTimer::timeout,this,&DirectPatternDialog::poll);render();QTimer::singleShot(0,this,[this]{next();});
}
DirectPatternDialog::~DirectPatternDialog(){cancel();}
void DirectPatternDialog::cancel(){
    if(canceled_)return;canceled_=true;timer_.stop();
    if(upload_ && !upload_->isFinished())upload_->abort();
    if(api_ && !jobId_.isEmpty())api_->remove("/pattern/direct-transfer/"+jobId_,{},{});
    jobId_.clear();
}
void DirectPatternDialog::reject(){cancel();QDialog::reject();}
void DirectPatternDialog::next(){
    if(canceled_)return;
    ++index_;progress_=0;pollErrors_=0;polling_=false;
    if(index_>=items_.size()){done_=true;render();return;}
    if(!api_){finish(trDirect("客户端连接已关闭","Client connection closed"));return;}
    auto &item=items_[index_];item.state="uploading";render();
    const QPointer<DirectPatternDialog> self(this);const QPointer<ApiClient> api=api_;
    upload_=api_->uploadFile("/pattern/direct-upload",item.path,{{"name",item.name},{"deviceId",item.deviceId}},
        [self,api](const ApiResponse &result){
            const QString id=result.data.toObject().value("id").toString();
            if(!self || self->canceled_){if(api && !id.isEmpty())api->remove("/pattern/direct-transfer/"+id,{},{});return;}
            self->upload_=nullptr;
            if(!result.ok){self->finish(result.status==404 || result.status==405 ? trDirect("当前服务端不支持此模式，请先升级Qt服务端","This server does not support direct transfer. Upgrade the Qt server first.") : result.error);return;}
            if(id.isEmpty()){self->finish(trDirect("服务器未返回临时传输编号","Server did not return a transfer identifier"));return;}
            self->jobId_=id;self->items_[self->index_].state="downloading";self->progress_=20;self->elapsed_.start();self->render();self->timer_.start();self->poll();
        },[self](qint64 sent,qint64 total){if(self && !self->canceled_ && total>0){self->progress_=qBound(0,int(20.0*sent/total),20);self->render();}});
}
void DirectPatternDialog::poll(){
    if(canceled_ || !api_ || jobId_.isEmpty())return;
    if(elapsed_.elapsed()>620000){finish(trDirect("设备传输超时","Device transfer timed out"));return;}
    if(polling_)return;polling_=true;const QPointer<DirectPatternDialog> self(this);
    api_->get("/pattern/direct-transfer/"+jobId_,[self](const ApiResponse &result){
        if(!self || self->canceled_)return;self->polling_=false;
        if(!result.ok){if(++self->pollErrors_>=3)self->finish(result.error);return;}
        self->pollErrors_=0;const auto state=result.data.toObject();
        self->progress_=20+qBound(0,state.value("progress").toInt(),100)*80/100;self->render();
        if(state.value("status").toString()=="completed")self->finish();
        else if(state.value("status").toString()=="failed")self->finish(state.value("message").toString());
    });
}
void DirectPatternDialog::finish(const QString &error){
    if(canceled_)return;timer_.stop();
    if(api_ && !jobId_.isEmpty())api_->remove("/pattern/direct-transfer/"+jobId_,{},{});jobId_.clear();
    if(index_>=0 && index_<items_.size()){items_[index_].state=error.isEmpty() ? "completed" : "failed";items_[index_].error=error;}
    progress_=100;render();QTimer::singleShot(0,this,[this]{next();});
}
void DirectPatternDialog::render(){
    setWindowTitle(trDirect("直接下发到机器","Send directly to machine"));title_->setText(windowTitle());
    hint_->setText(trDirect("服务端仅临时中转，不保存花型文件、花型库记录和下发日志。取消可停止后续传输，已写入机器的文件不会删除。","The server relays temporarily without saving pattern files, library records or delivery logs. Cancel stops further transfer; files already written to the machine remain."));
    table_->setHorizontalHeaderLabels({trDirect("花型文件","Pattern file"),trDirect("目标设备","Target device"),trDirect("状态","Status")});
    int completed=0,failed=0;
    for(int row=0;row<items_.size();++row){const auto &item=items_[row];
        table_->item(row,0)->setText(QFileInfo(item.path).fileName());table_->item(row,1)->setText(item.deviceName);
        QString text=item.state=="completed" ? trDirect("下发完成","Completed") : item.state=="failed" ? trDirect("下发失败","Failed") : item.state=="uploading" ? trDirect("正在上传","Uploading") : item.state=="downloading" ? trDirect("正在下发","Sending to machine") : trDirect("等待中","Waiting");
        if(!item.error.isEmpty())text+=" · "+(UiI18n::instance() ? UiI18n::instance()->text(item.error) : item.error);
        table_->item(row,2)->setText(text);table_->item(row,2)->setToolTip(text);
        completed+=item.state=="completed";failed+=item.state=="failed";
    }
    const bool active=index_>=0 && index_<items_.size() && (items_[index_].state=="uploading" || items_[index_].state=="downloading");
    bar_->setValue(items_.isEmpty() ? 100 : qBound(0,((completed+failed)*100+(active ? progress_ : 0))/int(items_.size()),100));
    status_->setText(trDirect("完成 %1 项，失败 %2 项，共 %3 项","Completed %1, failed %2, total %3").arg(completed).arg(failed).arg(items_.size()));
    close_->setText(done_ ? trDirect("关闭","Close") : trDirect("取消剩余传输","Cancel remaining transfers"));
}
