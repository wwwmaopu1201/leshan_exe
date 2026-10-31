#include "client_window.h"
#include "client_file_dialog.h"
#include "form_dialog.h"
#include "page_widgets.h"
#include "list_page.h"
#include "interaction_compatibility.h"
#include "ui_dialogs.h"
#include "ui_i18n.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <memory>

int runClientLargeDeviceCheck(ClientWindow &window,const QString &host,int port){
    int failures=0;auto check=[&](const QString &name,bool value){recordInteraction("client",name,value);failures+=!value;};
    auto wait=[&](std::function<bool()> condition){QElapsedTimer t;t.start();while(!condition() && t.elapsed()<6000)waitInteraction(10);return condition();};
    auto *api=window.api();api->setServer(host,port);auto response=std::make_shared<ApiResponse>();auto done=std::make_shared<bool>(false);
    api->post("/auth/login",QJsonObject{{"username","admin"},{"password","admin123"}},[=](const ApiResponse &value){*response=value;*done=true;});if(!wait([&]{return *done;}) || !response->ok){qWarning()<<"large device login failure"<<response->status<<response->error;return 2;}
    window.testSession(host,port,response->data.toObject().value("user").toObject(),response->data.toObject().value("token").toString());window.navigate("/device/list");
    ListPage *page=nullptr;QTreeWidget *tree=nullptr;auto find=[&]{for(auto *widget:window.findChildren<QWidget*>())if(widget->isVisible())if(auto *candidate=dynamic_cast<ListPage*>(widget))if(candidate->table()->columnForKey("code")>=0)page=candidate;for(auto *candidate:window.findChildren<QTreeWidget*>("groupTree"))if(candidate->isVisible())tree=candidate;return page && tree && tree->topLevelItemCount()>0 && !page->rows().isEmpty();};
    if(!wait(find)){qWarning()<<"large device visible page missing"<<window.api()->serverAddress()<<bool(page)<<bool(tree)<<window.findChildren<QWidget*>().size();window.grab().save(qEnvironmentVariable("BOER_INTERACTION_ARTIFACT_DIR")+"/client-large-failed.png");return 3;}
    wait([&]{return page->rows().size()==2250;});check("实际API2250设备在客户端本地分页模型中完整加载",page->rows().size()==2250);
    int count=0;QTreeWidgetItem *early=nullptr;for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+1)=="device"){++count;if((*it)->data(0,Qt::UserRole).toLongLong()==1)early=*it;}
    check("实际API客户端设备树完整2250条并包含早期设备",count==2250 && early);
    if(early){tree->setCurrentItem(early);tree->itemClicked(early,0);check("点击超过原2000条边界的早期设备仍显示其资料",wait([&]{return page->table()->rowCount()==1 && page->table()->recordAt(0).value("id").toInteger()==1;}));}
    window.grab().save(qEnvironmentVariable("BOER_INTERACTION_ARTIFACT_DIR")+"/client-api-2250-devices.png");
    QFile marker(qEnvironmentVariable("BOER_INTERACTION_ARTIFACT_DIR")+"/client-large-check-complete.json");if(!marker.open(QIODevice::WriteOnly))return 4;marker.write(QJsonDocument(QJsonObject{{"complete",true},{"failures",failures}}).toJson());marker.close();
    return failures ? 1 : 0;
}

int runClientInteractionRegression(ClientWindow &window){
    window.navigate("/employee/list");waitInteraction();
    return runInteractionCompatibility(window,"client",[&](const QString &message,bool error){window.notify(message,error);},[&]{
        for(bool english:{false,true}){
            UiI18n::instance()->setEnglish(english);
            ClientFormDialog dialog(&window,"键盘表单",560,400);
            FormField name;name.key="name";name.label="员工姓名";name.required=true;name.maximum=100;dialog.addField(name);
            FormField phone;phone.key="phone";phone.label="手机号";phone.required=true;phone.maximum=11;phone.inputPattern="^[0-9]{0,11}$";dialog.addField(phone);
            QTimer::singleShot(80,&dialog,[&]{
                QLineEdit *name=nullptr,*phone=nullptr;for(auto *line:dialog.findChildren<QLineEdit*>()){if(line->property("formFieldKey")=="name")name=line;if(line->property("formFieldKey")=="phone")phone=line;}
                if(!name || !phone){recordInteraction("client","真实表单输入可找到",false);dialog.done(QDialog::Rejected);return;}
                name->setFocus();QInputMethodEvent commit;commit.setCommitString("中文姓名😀");QApplication::sendEvent(name,&commit);sendInteractionKey(name,Qt::Key_Tab);waitInteraction();
                recordInteraction("client",QString(english ? "en" : "zh")+" 真实表单中文提交及Tab顺序",name->text()=="中文姓名😀" && QApplication::focusWidget()==phone);
                QInputMethodEvent invalid;invalid.setCommitString("中文");QApplication::sendEvent(phone,&invalid);recordInteraction("client",QString(english ? "en" : "zh")+" 手机号校验拒绝IME非数字提交",phone->text().isEmpty());
                QInputMethodEvent number;number.setCommitString("13800138001");QApplication::sendEvent(phone,&number);recordInteraction("client",QString(english ? "en" : "zh")+" 数字IME提交后通过手机号输入校验",phone->text()=="13800138001" && phone->hasAcceptableInput());
                UiI18n::instance()->setEnglish(!english);waitInteraction();UiI18n::instance()->setEnglish(english);waitInteraction();recordInteraction("client",QString(english ? "en" : "zh")+" 编辑中来回切语言不丢中文和号码",name->text()=="中文姓名😀" && phone->text()=="13800138001");
                sendInteractionKey(phone,Qt::Key_Return);waitInteraction();
                if(dialog.isVisible())dialog.done(QDialog::Rejected);
            });
            const auto saved=dialog.execute();recordInteraction("client",QString(english ? "en" : "zh")+" 真实表单Enter执行确认而非取消",saved && saved->value("name")=="中文姓名😀" && saved->value("phone")=="13800138001",QString("dialog_result=%1").arg(dialog.result()));
            ClientFormDialog dirty(&window,"放弃编辑键盘检查",560,370);dirty.setConfirmDiscard(true);dirty.addField(name,"原名称");
            bool kept=false,prompted=false;QTimer::singleShot(80,&dirty,[&]{
                QLineEdit *line=nullptr;for(auto *candidate:dirty.findChildren<QLineEdit*>())if(candidate->property("formFieldKey")=="name")line=candidate;
                if(!line){dirty.done(QDialog::Rejected);return;}line->setText("修改后名称");line->setFocus();
                QTimer::singleShot(50,&dirty,[&]{auto *inner=qobject_cast<QDialog*>(QApplication::activeModalWidget());prompted=inner && inner!=&dirty;if(prompted)sendInteractionKey(inner,Qt::Key_Escape);});
                sendInteractionKey(&dirty,Qt::Key_Escape);waitInteraction();kept=dirty.isVisible() && line->text()=="修改后名称";
                recordInteraction("client",QString(english ? "en" : "zh")+" Esc取消放弃确认仍保留编辑",prompted && kept);
                if(!dirty.isVisible())return;
                QTimer::singleShot(50,&dirty,[&]{auto *inner=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(inner && inner!=&dirty){auto *yes=inner->findChild<QPushButton*>("primaryButton");if(yes)yes->click();}});
                sendInteractionKey(&dirty,Qt::Key_Escape);
            });
            const auto discarded=dirty.execute();recordInteraction("client",QString(english ? "en" : "zh")+" 再次Esc并确认放弃后正常关闭",kept && !discarded);
            ClientFormDialog cancelForm(&window,"取消按钮键盘检查",560,360);cancelForm.addField(name,"已有名称");
            QTimer::singleShot(80,&cancelForm,[&]{QPushButton *cancel=nullptr;for(auto *button:cancelForm.findChildren<QPushButton*>())if(button->text()==UiI18n::instance()->text("取消"))cancel=button;if(cancel){cancel->setFocus();sendInteractionKey(cancel,Qt::Key_Return);}if(cancelForm.isVisible())cancelForm.done(QDialog::Accepted);});
            const auto canceledForm=cancelForm.execute();recordInteraction("client",QString(english ? "en" : "zh")+" 焦点在取消按钮时Enter仍取消而不误保存",!canceledForm);
        }
        QTemporaryDir directory;QLineEdit *restore=nullptr;for(auto *line:window.findChildren<QLineEdit*>())if(line->isVisible() && line->isEnabled()){restore=line;break;}if(restore)restore->setFocus();
        QTimer::singleShot(100,&window,[&]{auto *chooser=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());recordInteraction("client","实际文件保存对话框可访问且位于屏幕内",chooser && chooser->screen()->availableGeometry().contains(chooser->frameGeometry()));if(chooser)sendInteractionKey(chooser,Qt::Key_Escape);});
        const QString canceled=ClientFileDialog::getSaveFileName(&window,"取消保存测试",directory.filePath("never-written.xlsx"),"Excel (*.xlsx)");waitInteraction(120);
        recordInteraction("client","Esc取消文件保存不写文件并恢复父窗焦点",canceled.isEmpty() && !QFileInfo::exists(directory.filePath("never-written.xlsx")) && (!restore || QApplication::focusWidget()==restore));
        QDialog data(&window);data.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);UiModalDialog::attach(&data,&window);data.resize(760,530);auto *box=new QVBoxLayout(&data);auto *title=new QLabel("2000 rows / long Unicode metadata");title->setObjectName("dialogTitle");box->addWidget(title);auto *table=new ClientTable;box->addWidget(table);
        table->setColumns({{"序号","#",60},{"设备名称","name",210},{"备注","remark",240},{"速度","speed",100}},true,{"编辑","删除"},[](const QJsonObject &,const QString &){});
        QJsonArray rows;for(int i=1;i<=2000;++i)rows.append(QJsonObject{{"id",i},{"name",QString("设备%1").arg(i)+QString(90,QChar(0x8bbe))},{"remark",QString(500,QChar(0x6ce8))},{"speed",2000+i}});
        QElapsedTimer timing;timing.start();table->setRecords(rows);const qint64 populationMs=timing.elapsed();
        QTimer::singleShot(120,&data,[&]{
            table->cellWidget(0,0)->findChild<QCheckBox*>()->setChecked(true);table->verticalScrollBar()->setValue(table->verticalScrollBar()->maximum());waitInteraction();
            const QRect last=table->visualItemRect(table->item(1999,table->columnForKey("name")));
            table->cellWidget(1999,0)->findChild<QCheckBox*>()->setChecked(true);
            recordInteraction("client","2000条长中文数据末行可达且首尾逻辑选择正确",table->rowCount()==2000 && last.top()>=0 && last.bottom()<=table->viewport()->height() && table->checkedIds().contains(1) && table->checkedIds().contains(2000),QString("populate_ms=%1").arg(populationMs));
            const QString remark=table->item(1999,table->columnForKey("remark"))->toolTip();const auto selected=table->checkedIds();
            data.resize(620,390);UiI18n::instance()->setEnglish(false);waitInteraction();UiI18n::instance()->setEnglish(true);waitInteraction();
            recordInteraction("client","2000条数据叠加缩放语言切换保留选择和500字全文提示",table->checkedIds()==selected && remark==QString(500,QChar(0x6ce8)) && !table->wordWrap());
            const QPalette original=QApplication::palette();QPalette dark=original;dark.setColor(QPalette::Window,QColor("#202020"));dark.setColor(QPalette::Base,QColor("#181818"));dark.setColor(QPalette::Text,Qt::white);dark.setColor(QPalette::WindowText,Qt::white);QApplication::setPalette(dark);waitInteraction();
            data.grab().save(qEnvironmentVariable("BOER_INTERACTION_ARTIFACT_DIR")+"/client-large-data-dark-palette.png");recordInteraction("client","局部暗色palette下表格文字与背景可区分",table->palette().color(QPalette::Text)!=table->palette().color(QPalette::Base));QApplication::setPalette(original);data.done(QDialog::Rejected);
        });data.exec();
        QTcpServer delayed;const bool listening=delayed.listen(QHostAddress::LocalHost,0);QList<QPointer<QTcpSocket>> peers;QObject::connect(&delayed,&QTcpServer::newConnection,&delayed,[&]{while(auto *socket=delayed.nextPendingConnection())peers<<socket;});
        bool safe=listening;for(int cycle=0;cycle<3 && safe;++cycle){auto *victim=new ClientWindow;victim->show();QPointer<ClientWindow> guard(victim);auto afterDestroy=std::make_shared<bool>(false);victim->api()->setServer("127.0.0.1",delayed.serverPort());victim->api()->get("/healthz",[guard,afterDestroy](const ApiResponse &){if(!guard)*afterDestroy=true;});waitInteraction(100);delete victim;waitInteraction(120);for(auto peer:peers)if(peer)peer->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}");waitInteraction(80);safe&=guard.isNull() && !*afterDestroy;}
        recordInteraction("client","三次待网络响应时销毁窗口，迟到响应不访问已销毁窗口",safe);window.activateWindow();
    });
}
