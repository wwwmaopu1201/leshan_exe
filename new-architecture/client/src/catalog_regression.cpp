#include "login_regression.h"
#include "client_window.h"
#include "page_widgets.h"
#include "ui_i18n.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QToolTip>
#include <QHelpEvent>
#include <QScreen>
#include <QFileInfo>
#include <QDir>
#include <QCheckBox>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QAction>
#include <QDesktopServices>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <QMenu>
#include <QKeyEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <memory>

class RegressionUrlCapture : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void capture(const QUrl &url) { urls.append(url); }
};

int runCatalogRegression(QApplication &app,const QString &host,int port) {
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsDir.path());
    UiI18n::instance()->setEnglish(false);
    QFile log(qEnvironmentVariable("BOER_CUSTOMER_ROW_LOG"));
    if(!log.open(QIODevice::WriteOnly|QIODevice::Append)) return 2;
    int failures=0;
    auto record=[&](int row,const QString &name,bool passed) {
        log.write(QJsonDocument(QJsonObject{{"row",row},{"name",name},{"result",passed ? "PASS" : "FAIL"},
            {"scope","Qt客户端真实页面按钮/输入/分页，连接全新临时Qt服务端"}}).toJson(QJsonDocument::Compact)+'\n'); log.flush();
        qInfo("ROW %d %s %s",row,passed ? "PASS" : "FAIL",qPrintable(name));
        failures+=!passed;
    };
    auto wait=[&](std::function<bool()> test,int timeout=4000) {
        QElapsedTimer timer; timer.start();
        while(!test() && timer.elapsed()<timeout) { app.processEvents(); QThread::msleep(2); }
        return test();
    };
    ClientWindow window; window.show(); auto *api=window.api(); api->setServer(host,port);
    auto request=[&](const QByteArray &method,const QString &path,const QJsonObject &body=QJsonObject{}) {
        auto response=std::make_shared<ApiResponse>(); auto done=std::make_shared<bool>(false);
        api->request(method,path,body,[response,done](const ApiResponse &value){ *response=value; *done=true; });
        if(!wait([&]{return *done;})) qFatal("Catalog request timeout");
        return *response;
    };
    const auto login=request("POST","/auth/login",{{"username","admin"},{"password","admin123"}});
    if(!login.ok) return 3;
    window.testSession(host,port,login.data.toObject().value("user").toObject(),login.data.toObject().value("token").toString());
    struct Catalog { QString page,api; int pagination,empty,length,number,search; };
    const QList<Catalog> cases={
        {"/device/types","/device/type-summary",132,128,130,131,0},
        {"/device/electric-control-types","/device/electric-control-type-summary",142,142,142,142,142},
        {"/file/pattern-types","/pattern/type-summary",237,233,235,236,232},
        {"/file/order-nos","/pattern/order-summary",250,247,249,0,246}};
    int fixtureIndex=0;
    QList<qint64> linkedPatterns;
    for(const Catalog &item:cases) {
        for(int n=0;n<25;++n) if(!request("POST",item.api,{{"value",QString("Catalog%1").arg(n,2,10,QChar('0'))}}).ok) return 4;
        window.navigate(item.page); app.processEvents();
        ClientTable *table=nullptr; ClientPagination *pagination=nullptr; QLineEdit *search=nullptr;
        for(auto *widget:window.findChildren<QTableWidget*>()) if(widget->isVisible()) {
            auto *candidate=dynamic_cast<ClientTable*>(widget);
            if(candidate && candidate->columnForKey("value")>=0) table=candidate;
        }
        for(auto *candidate:window.findChildren<ClientPagination*>()) if(candidate->isVisible()) pagination=candidate;
        for(auto *input:window.findChildren<QLineEdit*>()) if(input->isVisible() &&
            (input->placeholderText()=="搜索类型名称" || input->placeholderText()=="搜索订单编号")) search=input;
        const bool loaded=wait([&]{return table && table->rowCount()==20;});
        if(item.page=="/file/order-nos")record(252,"订单管理字段统一显示订单编号",table && table->horizontalHeaderItem(table->columnForKey("value"))->text()=="订单编号");
        record(item.pagination,item.page+" 25条目录初始页20条",loaded && pagination);
        if(!table || !pagination || !search) return 5;
        QPushButton *next=nullptr;
        if(pagination) for(auto *button:pagination->findChildren<QPushButton*>()) if(button->text()=="›") next=button;
        if(next) next->click();
        record(item.pagination,item.page+" 下一页显示剩余5条",next && wait([&]{return table->rowCount()==5;}) && pagination->page()==2);
        auto *size=pagination ? pagination->findChild<QComboBox*>() : nullptr;
        if(size) size->setCurrentIndex(size->findData(100));
        record(item.pagination,item.page+" 从第二页改100条回第一页并显示全部25条",size && wait([&]{return table->rowCount()==25;}) && pagination->page()==1);
        if(search) { search->setText("Catalog07"); QMetaObject::invokeMethod(search,"returnPressed",Qt::DirectConnection); }
        record(item.search,item.page+" 搜索准确返回07",search && wait([&]{return table->rowCount()==1;}) && table->recordAt(0).value("value").toString()=="Catalog07");
        if(search) { search->clear(); QMetaObject::invokeMethod(search,"returnPressed",Qt::DirectConnection); }
        wait([&]{return table && table->rowCount()==25;});
        QPushButton *create=nullptr;
        for(auto *button:window.findChildren<QPushButton*>()) if(button->isVisible() && (button->text()=="新增类型" || button->text()=="新增编号")) { create=button; break; }
        if(!create) { record(item.number,item.page+" 新增按钮存在",false); continue; }
        QTimer::singleShot(40,&window,[&] {
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            auto *input=dialog ? dialog->findChild<QLineEdit*>("promptInput") : nullptr;
            QPushButton *save=nullptr;
            if(dialog) for(auto *button:dialog->findChildren<QPushButton*>()) if(button->text()=="确定") save=button;
            if(!input || !save) qFatal("Catalog create dialog missing");
            save->click(); app.processEvents();
            auto *error=dialog->findChild<QLabel*>("promptError");
            record(item.empty,item.page+" 空名称不提交且显示内联错误",dialog->isVisible() && error && error->isVisible());
            auto *marker=dialog->findChild<QLabel*>("requiredMarker");
            record(item.empty==142 ? 142 : item.empty+1,item.page+" 必填项显示星号",marker && marker->text()=="*");
            input->setText("临时名称"); input->clear(); app.processEvents();
            record(item.empty,item.page+" 输入后清空立即提示必填",error && error->isVisible());
            input->setText(QString(101,QChar(0x957f)));
            record(item.length,item.page+" 新增输入框限制100字",input->text().size()==100);
            if(item.page=="/file/order-nos") {
                for(const QString &value:QStringList{"含汉字","A-B"," A1","A1 "}) {input->setText(value);save->click();record(163,"订单真实新增弹窗拒绝非法输入："+value,dialog->isVisible() && error && error->isVisible() && error->text().contains("字母"));}
            }
            input->setText("1"); save->click();
        });
        create->click();
        record(item.number,item.page+" 输入数字1从界面成功新增并刷新",wait([&]{return table->rowCount()==26;}));
        auto findRow=[&](const QString &value) {
            for(int row=0;row<table->rowCount();++row) if(table->recordAt(row).value("value").toString()==value) return row;
            return -1;
        };
        QString current="1";
        const int renameRow=item.page=="/device/types" ? 136 : item.page.contains("electric") ? 143 : item.page.contains("pattern-types") ? 240 : 257;
        for(int n=0;n<5;++n) {
            const int row=findRow(current); const QString replacement="Rename"+QString::number(n);
            QPushButton *rename=nullptr;auto *frozen=table->findChild<QTableWidget*>("clientFrozenActions");
            if(row>=0 && frozen)if(auto *actions=frozen->cellWidget(row,0))for(auto *button:actions->findChildren<QPushButton*>())if(button->text()=="修改名称")rename=button;
            record(renameRow,item.page+" 重命名控件/选中项存在",row>=0 && rename);
            if(row<0 || !rename) break;
            table->cellClicked(row,table->columnForKey("value"));
            QTimer::singleShot(40,&window,[&] {
                auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                auto *input=dialog ? dialog->findChild<QLineEdit*>("promptInput") : nullptr;
                if(!input) qFatal("Catalog rename prompt missing"); input->setText(replacement);
                QMetaObject::invokeMethod(input,"returnPressed",Qt::DirectConnection);
            });
            rename->click();
            record(renameRow,item.page+" 连续第"+QString::number(n+1)+"次重命名并刷新",wait([&]{return findRow(replacement)>=0 && findRow(current)<0;}));
            current=replacement;
        }
        const int operationAlias=item.page=="/device/types" ? 140 : item.page.contains("electric") ? 147 : item.page.contains("pattern-types") ? 244 : 257;
        record(operationAlias,item.page+" 原表同类操作条目：连续重命名后新名存在",findRow(current)>=0 && current.endsWith("4"));
        const int batchRow=item.page=="/device/types" ? 135 : item.page.contains("electric") ? 142 : item.page.contains("pattern-types") ? 239 : 253;
        for(const QString &name:QStringList{"Catalog03","Catalog04"}){const int row=findRow(name);if(row<0)return 20;table->cellClicked(row,table->columnForKey("value"));}
        QPushButton *batchRename=nullptr;for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()=="批量修改")batchRename=button;
        if(!batchRename)return 21;
        QTimer::singleShot(40,&window,[&] {
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("catalog batch editor missing");QLineEdit *first=nullptr,*second=nullptr;
            for(auto *line:dialog->findChildren<QLineEdit*>()){if(line->property("formFieldKey")=="0")first=line;if(line->property("formFieldKey")=="1")second=line;}
            if(!first || !second)qFatal("catalog batch fields missing");first->setText("BatchDuplicate");second->setText("BatchDuplicate");auto *save=dialog->findChild<QPushButton*>("primaryButton");save->click();app.processEvents();
            auto *error=dialog->findChild<QLabel*>("promptError");record(batchRow,item.page+" 批量输入重复新名时原地红字禁止提交",dialog->isVisible() && error && error->isVisible());
            first->setText("BatchType03");second->setText("BatchType04");app.processEvents();record(batchRow,item.page+" 修正批量重名后红字自动清除",error && !error->isVisible());save->click();
        });
        batchRename->click();record(batchRow,item.page+" 实际批量修改两个名称并刷新列表",wait([&]{return findRow("BatchType03")>=0 && findRow("BatchType04")>=0 && findRow("Catalog03")<0 && findRow("Catalog04")<0;}));
        for(const QString &value:{QString("Catalog01"),QString("Catalog02")}) {
            const int row=findRow(value); if(row>=0) table->cellClicked(row,table->columnForKey("value"));
        }
        const QString removeText=item.page.contains("order-nos") ? "清空编号" : item.page.startsWith("/device") ? "删除类型" : "清空类型";
        QPushButton *remove=nullptr;
        for(auto *button:window.findChildren<QPushButton*>()) if(button->isVisible() && button->text()==removeText && button->objectName().isEmpty()) { remove=button; break; }
        const int removeRow=item.page=="/device/types" ? 137 : item.page.contains("electric") ? 144 : item.page.contains("pattern-types") ? 241 : 254;
        if(remove) {
            QTimer::singleShot(40,&window,[&] {
                auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if(!dialog) qFatal("Catalog delete confirmation missing");
                if(item.page.contains("pattern-types")){UiI18n::instance()->setEnglish(true);app.processEvents();bool translated=true;for(auto *label:dialog->findChildren<QLabel*>())translated&=!label->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));for(auto *button:dialog->findChildren<QPushButton*>())translated&=!button->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));record(245,"花型类型实际批量清理确认提示与按钮切英文无中文残留",translated);UiI18n::instance()->setEnglish(false);app.processEvents();}

                for(auto *button:dialog->findChildren<QPushButton*>()) if(button->text()=="确定") { button->click(); return; }
                qFatal("Catalog delete confirmation button missing");
            });
            remove->click();
        }
        record(removeRow,item.page+" 批量清理两个目录项并刷新24条",remove && wait([&]{return table->rowCount()==24;}) && findRow("Catalog01")<0 && findRow("Catalog02")<0);
        record(operationAlias,item.page+" 原表同类操作条目：实际清理两项后记录不再显示",remove && table->rowCount()==24 && findRow("Catalog01")<0 && findRow("Catalog02")<0);
        if(item.page.contains("electric")) {const auto entries=apiRows(request("GET",item.api).data);bool stamps=!entries.isEmpty();for(const auto &value:entries)stamps&=value.toObject().value("updateTime").toString().size()>=10;record(146,"电控类型最近更新时间有真实时间值",stamps);}
        const bool device=item.page.startsWith("/device");
        const QString relatedKey=item.page.contains("electric") ? "electricControlType" : device ? "type" : item.page.contains("pattern-types") ? "patternType" : "orderNo";
        qint64 linkedId=0;
        if(device) {
            auto linked=request("POST","/device",{{"code","linked-"+QString::number(fixtureIndex)},
                {"name","关联设备"+QString::number(fixtureIndex)},{relatedKey,"Catalog00"}});
            if(!linked.ok) return 6; linkedId=linked.data.toObject().value("id").toInteger();
        } else {
            const QString filePath=settingsDir.filePath("linked-"+QString::number(fixtureIndex)+".dst");
            QFile source(filePath); if(!source.open(QIODevice::WriteOnly)) return 7; source.write("LINKED-PATTERN"); source.close();
            auto response=std::make_shared<ApiResponse>(); auto done=std::make_shared<bool>(false);
            api->uploadFile("/pattern/upload",filePath,{{"name","关联花型"+QString::number(fixtureIndex)},{relatedKey,"Catalog00"},
                {"unitPrice",2.5},{"stitches",123}},
                [response,done](const ApiResponse &value){ *response=value; *done=true; });
            if(!wait([&]{return *done;}) || !response->ok) return 8;
            linkedId=response->data.toObject().value("id").toInteger();
            linkedPatterns.append(linkedId);
        }
        window.navigate(item.page);
        const bool countLoaded=wait([&]{const int row=findRow("Catalog00"); return row>=0 && table->recordAt(row).value(device ? "deviceCount" : "patternCount").toInt()==1;});
        const int linkedCase=item.page.contains("electric") ? 145 : device ? 138 : item.page.contains("pattern-types") ? 242 : 255;
        record(linkedCase,item.page+" 关联数量准确为1",countLoaded);
        const int linkedRow=findRow("Catalog00");
        if(linkedRow>=0) table->cellClicked(linkedRow,table->columnForKey(device ? "deviceCount" : "patternCount"));
        ClientTable *target=nullptr;
        const bool linked=wait([&] {
            if(device) {
                for(auto *widget:window.findChildren<QTableWidget*>()) if(widget->isVisible()) {
                    auto *candidate=dynamic_cast<ClientTable*>(widget);
                    if(candidate && candidate->columnForKey("code")>=0) target=candidate;
                }
            } else if(auto *holder=window.findChild<QWidget*>("clientServerPatterns"))
                target=dynamic_cast<ClientTable*>(holder->findChild<QTableWidget*>("clientTable"));
            return target && target->rowCount()==1 && target->recordAt(0).value("id").toInteger()==linkedId;
        });
        record(linkedCase,item.page+" 点击关联数字跳转且只显示对应记录",linked && window.currentPath()==(device ? "/device/list" : "/file/pattern"));
        ++fixtureIndex;
    }
    // A user changes only the type: unrelated per-file metadata must survive.
    window.navigate("/file/pattern"); app.processEvents();
    auto *holder=window.findChild<QWidget*>("clientServerPatterns");
    auto *patterns=holder ? dynamic_cast<ClientTable*>(holder->findChild<QTableWidget*>("clientTable")) : nullptr;
    if(!holder || !patterns) return 9;
    for(auto *button:holder->findChildren<QPushButton*>()) if(button->text()=="重置") { button->click(); break; }
    record(255,"重置关联筛选后恢复两个花型",wait([&]{return patterns->rowCount()==2;}));
    for(int row=0;row<patterns->rowCount();++row) patterns->cellClicked(row,patterns->columnForKey("name"));
    QPushButton *batch=nullptr;
    for(auto *button:holder->findChildren<QPushButton*>()) if(button->text()=="批量修改") batch=button;
    if(batch) {
        QTimer::singleShot(40,&window,[&] {
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog) qFatal("Pattern batch editor missing");
            QComboBox *type=nullptr;
            for(auto *combo:dialog->findChildren<QComboBox*>()) if(combo->property("formFieldKey").toString()=="patternType") type=combo;
            if(!type) qFatal("Pattern batch type field missing"); type->setCurrentText("Catalog24");
            for(auto *button:dialog->findChildren<QPushButton*>()) if(button->text()=="确定") { button->click(); return; }
            qFatal("Pattern batch submit missing");
        });
        batch->click();
    }
    const bool updated=wait([&] {
        if(patterns->rowCount()!=2) return false;
        for(int row=0;row<2;++row) if(patterns->recordAt(row).value("patternType").toString()!="Catalog24") return false;
        return true;
    });
    bool preserved=updated;
    for(int i=0;i<linkedPatterns.size();++i) {
        const auto value=request("GET","/pattern/"+QString::number(linkedPatterns[i])).data.toObject();
        preserved=preserved && value.value("unitPrice").toDouble()==2.5 &&
            value.value("orderNo").toString()==(i==0 ? QString() : QString("Catalog00"));
    }
    record(168,"界面批量只改类型，两个花型的原工价与订单均保留",batch && preserved);
    const QString longGroupName=QString(50,QChar(0x7ec4));const auto longEmployeeGroup=request("POST","/group",{{"name",longGroupName},{"parentId",QJsonValue(QJsonValue::Null)}}).data.toObject().value("id").toInteger();
    const auto added=request("POST","/employee",{{"code","0001001"},{"name","导出测试"},{"phone","13800138000"},{"groupId",1}});
    window.navigate("/employee/list");
    ClientTable *employees=nullptr;
    wait([&] { for(auto *widget:window.findChildren<QTableWidget*>()) if(auto *table=dynamic_cast<ClientTable*>(widget)) if(table->isVisible() && table->columnForKey("code")>=0) employees=table; return employees && employees->rowCount()==1; });
    QPushButton *newEmployee=nullptr;for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()=="新增员工")newEmployee=button;
    if(newEmployee) {
        QTimer::singleShot(40,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("employee add missing");
            for(auto *line:dialog->findChildren<QLineEdit*>()){const QString key=line->property("formFieldKey").toString();if(key=="code")line->setText("1009");if(key=="name")line->setText("重复手机");if(key=="phone")line->setText("138 0013 8000");}
            for(auto *combo:dialog->findChildren<QComboBox*>())if(combo->property("formFieldKey")=="groupId") {
                combo->setCurrentIndex(combo->findData(longEmployeeGroup));app.processEvents();record(314,"员工新增50字分组不撑宽表单，当前选择Hover保留全文",combo->currentText()==longGroupName && combo->toolTip()==longGroupName && dialog->rect().contains(QRect(combo->mapTo(dialog,QPoint()),combo->size())));
                combo->showPopup();app.processEvents();auto *popup=combo->view()->window();record(314,"超长分组下拉窗口限制在屏幕内并可Hover核对全名",popup && combo->screen()->availableGeometry().contains(popup->geometry()) && combo->itemData(combo->currentIndex(),Qt::ToolTipRole).toString()==longGroupName);combo->hidePopup();combo->setCurrentIndex(combo->findData(1));
            }
            dialog->findChild<QPushButton*>("primaryButton")->click();
        });newEmployee->click();
    }
    const bool duplicateNotice=wait([&]{for(auto *label:window.findChildren<QLabel*>())if(label->isVisible() && label->text().contains("手机号已被"))return true;return false;});
    record(313,"客户端真实新增员工拒绝重复手机并显示服务端错误",newEmployee && duplicateNotice && request("GET","/employee/list?code=1009").data.toObject().value("list").toArray().isEmpty());
    const QString longRemark=QString(500,QChar(0x5907))+"\n多行备注结尾";const auto originalEmployeeId=added.data.toObject().value("id").toInteger();const auto remarkSaved=request("PUT","/employee/"+QString::number(originalEmployeeId),{{"remark",longRemark}});record(315,"员工备注修改接口可真实保存，而非拒绝表单始终携带的备注字段",remarkSaved.ok && remarkSaved.data.toObject().value("remark")==longRemark);window.navigate("/employee/list");wait([&]{return employees->rowCount()==1 && employees->recordAt(0).value("remark")==longRemark;});
    auto *remarkCell=employees->item(0,employees->columnForKey("remark"));employees->scrollToItem(remarkCell);app.processEvents();const QPoint remarkPoint=employees->visualItemRect(remarkCell).center();QHelpEvent hoverRemark(QEvent::ToolTip,remarkPoint,employees->viewport()->mapToGlobal(remarkPoint));QApplication::sendEvent(employees->viewport(),&hoverRemark);app.processEvents();
    QLabel *tooltipLabel=nullptr;for(auto *top:QApplication::topLevelWidgets())if(top->isVisible() && top->objectName()=="qtooltip_label")tooltipLabel=qobject_cast<QLabel*>(top);
    record(315,"员工500字多行备注在单元格保留完整Hover原文",remarkCell && remarkCell->toolTip()==longRemark && QToolTip::text()==longRemark);
    record(315,"长备注实际Tooltip显示在屏幕内，不横向溢出",tooltipLabel && tooltipLabel->screen()->availableGeometry().contains(tooltipLabel->geometry()));if(tooltipLabel)tooltipLabel->grab().save(QFileInfo(log.fileName()).absoluteDir().filePath("employee-long-remark-tooltip.png"));QToolTip::hideText();
    QPushButton *editEmployee=nullptr;auto *employeeActions=employees->findChild<QTableWidget*>("clientFrozenActions");if(employeeActions && employeeActions->cellWidget(0,0))for(auto *button:employeeActions->cellWidget(0,0)->findChildren<QPushButton*>())if(button->text()=="编辑")editEmployee=button;
    const QString editedRemark=QString(200,QChar(0x5907))+"界面修改";
    if(editEmployee){QTimer::singleShot(40,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("employee edit missing");for(auto *line:dialog->findChildren<QLineEdit*>())if(line->property("formFieldKey")=="name")line->setText("实际编辑");for(auto *text:dialog->findChildren<QTextEdit*>())if(text->property("formFieldKey")=="remark")text->setPlainText(editedRemark);dialog->findChild<QPushButton*>("primaryButton")->click();});editEmployee->click();}
    record(315,"实际员工编辑携带备注能够保存姓名和备注，不再被服务端拒绝",editEmployee && wait([&]{const auto current=request("GET","/employee/"+QString::number(originalEmployeeId)).data.toObject();return current.value("name")=="实际编辑" && current.value("remark")==editedRemark;}));
    UiI18n::instance()->setEnglish(true);app.processEvents();QTimer::singleShot(40,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());bool valid=dialog;const QRegularExpression chinese("[\\x{4e00}-\\x{9fff}]");if(dialog)for(auto *label:dialog->findChildren<QLabel*>())if(label->isVisible())valid&=!label->text().contains(chinese) && (label->wordWrap() || label->width()>=label->fontMetrics().horizontalAdvance(label->text()));record(328,"英文新增员工实际弹窗字段完整翻译，标签不裁切",valid);if(dialog)dialog->reject();});if(newEmployee)newEmployee->click();UiI18n::instance()->setEnglish(false);app.processEvents();
    QPushButton *exportButton=nullptr;
    for(auto *button:window.findChildren<QPushButton*>()) if(button->isVisible() && button->text()=="导出") exportButton=button;
    const QString target=QFileInfo(log.fileName()).absoluteDir().filePath("员工实际界面导出.xlsx"); QFile::remove(target);
    if(exportButton) {
        QTimer::singleShot(40,&window,[&] {
            auto *dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if(!dialog) qFatal("employee export dialog missing"); dialog->selectFile(target); QMetaObject::invokeMethod(dialog,"accept");
        });
        exportButton->click();
    }
    const bool saved=wait([&] {return QFileInfo::exists(target);});
    QFile exported(target); const auto archive=exported.open(QIODevice::ReadOnly) ? exported.readAll() : QByteArray();
    record(325,"员工实际导出XLSX，前导零工号和手机号按文本保存",added.ok && saved && archive.contains("0001001</t>") && archive.contains("13800138000</t>") && archive.contains("t=\"inlineStr\""));
    record(325,"员工实际导出含创建时间且设置列宽，不生成井号占位",saved && archive.contains(QString("创建时间").toUtf8()) && archive.contains("customWidth=\"1\"") && !archive.contains("####") && archive.contains(added.data.toObject().value("createdAt").toString().toUtf8()));
    QPushButton *importButton=nullptr,*refreshEmployees=nullptr,*deleteEmployees=nullptr;
    ClientPagination *employeePages=nullptr;
    for(auto *button:window.findChildren<QPushButton*>()) if(button->isVisible()) {
        if(button->text()=="批量导入") importButton=button;
        if(button->text()=="刷新") refreshEmployees=button;
        if(button->text()=="批量删除") deleteEmployees=button;
    }
    for(auto *candidate:window.findChildren<ClientPagination*>()) if(candidate->isVisible()) employeePages=candidate;
    const QString csvPath=settingsDir.filePath("员工导入.csv"); QFile csv(csvPath);
    if(!csv.open(QIODevice::WriteOnly)) return 9;
    csv.write(QString("\ufeff员工工号,员工姓名,手机号,备注\r\n0002001,文件员工,139 0013 8000,\"含逗号,备注\"\r\n0002002,第二员工,13700138000,\"两行\n备注\"\r\n").toUtf8()); csv.close();
    auto importRows=[&](bool fromFile,bool overwrite) {
        if(!importButton) qFatal("employee import button missing");
        QTimer::singleShot(40,&window,[&] {
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); if(!dialog) qFatal("employee import dialog missing");
            for(auto *box:dialog->findChildren<QComboBox*>()) if(box->property("formFieldKey")=="groupId") box->setCurrentIndex(box->findData(1));
            if(fromFile) dialog->findChild<QLineEdit*>("filePathInput")->setText(csvPath);
            else for(auto *text:dialog->findChildren<QTextEdit*>()) if(text->property("formFieldKey")=="importText") text->setPlainText("员工工号\t员工姓名\t手机号\n0001001\t粘贴覆盖\t138 0013 8000");
            for(auto *box:dialog->findChildren<QCheckBox*>()) if(box->property("formFieldKey")=="overwriteExisting") box->setChecked(overwrite);
            for(auto *button:dialog->findChildren<QPushButton*>()) if(button->text()=="确定") {button->click();return;}
            qFatal("employee import submit missing");
        });
        importButton->click();
    };
    importRows(true,false);
    record(317,"实际CSV文件识别BOM、空格手机号、引号逗号和多行备注",wait([&]{return employees->rowCount()==3;}) && request("GET","/employee").ok);
    auto imported=request("GET","/employee").data.toObject().value("list").toArray();
    bool quoted=false; for(const auto &item:imported) if(item.toObject().value("code")=="0002001") quoted=item.toObject().value("remark")=="含逗号,备注";
    record(317,"CSV含逗号的备注未错列",quoted);
    importRows(false,true);
    const auto originalId=added.data.toObject().value("id").toInteger();
    const bool overwritten=wait([&] {return request("GET","/employee/"+QString::number(originalId)).data.toObject().value("name")=="粘贴覆盖";});
    record(321,"粘贴制表符表格正常解析并识别空格手机号",overwritten);
    record(318,"界面选择覆盖已有工号后更新原员工且不增加条数",overwritten && employees->rowCount()==3);
    for(int i=0;i<22;++i) if(!request("POST","/employee",{{"code",QString::number(3000+i)},{"name",QString("员工%1").arg(i)},{"phone",QString::number(13500138000LL+i)},{"groupId",1}}).ok) return 10;
    if(!refreshEmployees || !employeePages) return 11; refreshEmployees->click();
    auto *employeeSize=employeePages->findChild<QComboBox*>();record(316,"员工表格默认20条并提供10/20/50/100条选择",wait([&]{return employees->rowCount()==20;}) && employeeSize->currentData().toInt()==20 && employeeSize->findData(10)>=0 && employeeSize->findData(50)>=0 && employeeSize->findData(100)>=0);employeeSize->setCurrentIndex(employeeSize->findData(10));
    record(324,"员工列表25条切10条每页显示10条",wait([&]{return employees->rowCount()==10;}) && employeePages->page()==1);
    QPushButton *employeeNext=nullptr; for(auto *button:employeePages->findChildren<QPushButton*>()) if(button->text()=="›") employeeNext=button;
    if(!employeeNext || !deleteEmployees) return 12; employeeNext->click();
    const bool second=wait([&]{return employees->rowCount()==10 && employeePages->page()==2 && employees->recordAt(0).value("code")=="3011";});
    for(int r=0;r<employees->rowCount();++r) employees->cellClicked(r,employees->columnForKey("code"));
    int refreshed=0;const auto refreshConnection=QObject::connect(api,&ApiClient::requestObserved,&window,[&](const QString &path,int,bool){if(path=="/api/employee/list")++refreshed;});refreshEmployees->click();record(327,"同一页刷新后保留10个已勾选员工",wait([&]{return refreshed>0 && employees->checkedIds().size()==10;}));QObject::disconnect(refreshConnection);
    UiI18n::instance()->setEnglish(true);app.processEvents();QTimer::singleShot(40,&window,[&] {
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); if(!dialog) qFatal("employee delete confirmation missing");bool translated=true;for(auto *label:dialog->findChildren<QLabel*>())translated&=!label->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));record(327,"员工批量删除英文二次确认不残留中文",translated);dialog->findChild<QPushButton*>("primaryButton")->click();
    });
    record(327,"切换英文后保留当前10个已勾选员工，不因刷新丢失选择",employees->checkedIds().size()==10);deleteEmployees->click();UiI18n::instance()->setEnglish(false);app.processEvents();
    record(323,"员工第二页批量删除10条后自动刷新显示剩余5条",second && wait([&]{return employees->rowCount()==5;}) && employeePages->page()==2);
    employeeSize->setCurrentIndex(employeeSize->findData(100));
    record(324,"员工从第二页切100条回第一页显示剩余15条",wait([&]{return employees->rowCount()==15;}) && employeePages->page()==1);
    int permissionIndex=0;
    for(const QString &permission:QStringList{"deviceInfo","remoteMonitoring","patternFiles","salaryStatistics","none"}) {
        QJsonObject map; for(const QString &key:QStringList{"home","dashboard","deviceManagement","deviceInfo","remoteMonitoring","fileManagement","patternFiles","devicePatternFiles","downloadLog","statistics","salaryStatistics","statusStatistics","employeeManagement"}) map[key]=false;
        if(permission!="none") map[permission]=true;
        if(permission=="deviceInfo" || permission=="remoteMonitoring") map["deviceManagement"]=true;
        if(permission=="patternFiles") map["fileManagement"]=true;
        if(permission=="salaryStatistics") map["statistics"]=true;
        const QString roleName="limit"+QString::number(permissionIndex++);
        if(!request("POST","/role",{{"name",roleName},{"permissions",QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact))}}).ok) return 13;
        if(!request("POST","/user",{{"username",roleName},{"password","123456"},{"nickname","权限测试"},{"phone","13800138000"},{"role",roleName},{"groupId",1}}).ok) return 14;
        const auto session=request("POST","/auth/login",{{"username",roleName},{"password","123456"}});
        if(!session.ok) return 15;
        const auto data=session.data.toObject();
        ClientWindow limited; limited.show(); int startupFailures=0,requests=0;
        QObject::connect(limited.api(),&ApiClient::requestObserved,&limited,[&](const QString &,int,bool ok) {++requests;if(!ok)++startupFailures;});
        limited.testSession(host,port,data.value("user").toObject(),data.value("token").toString());
        wait([&] {return requests>0;},1000); wait([] {return false;},150);
        const QString expected=permission=="deviceInfo" ? "/device/list" : permission=="remoteMonitoring" ? "/device/monitor" : permission=="patternFiles" ? "/file/pattern" : permission=="salaryStatistics" ? "/statistics/salary" : "/profile/info";
        bool onlySelected=limited.currentPath()==expected;
        for(const QString &key:QStringList{"deviceInfo","remoteMonitoring","patternFiles","devicePatternFiles","downloadLog","salaryStatistics","statusStatistics","employeeManagement","home","dashboard"}) onlySelected=onlySelected && limited.hasPermission(key)==(key==permission);
        auto *navigation=limited.findChild<QTreeWidget*>("clientNavigation");
        bool routes=true; if(navigation) for(QTreeWidgetItemIterator it(navigation);*it;++it) {
            const auto path=(*it)->data(0,Qt::UserRole).toString();
            const bool ownSubpage=(permission=="deviceInfo" && path.startsWith("/device/")) ||
                (permission=="patternFiles" && QStringList{"/file/pattern-types","/file/order-nos"}.contains(path)) ||
                (permission=="salaryStatistics" && path.startsWith("/statistics/salary"));
            if(!path.isEmpty() && !path.startsWith("/profile/") && !path.startsWith("/support/") && path!=expected && !ownSubpage) routes=false;
        }
        record(permission=="none" ? 431 : 430,"真实账号仅授予"+permission+"后客户端菜单和默认页面受限",onlySelected && routes);
        record(permission=="none" ? 431 : 430,"真实账号"+permission+"登录后没有越权请求错误或多条报错",requests>0 && startupFailures==0);
        if(permission=="none") {
            ClientTable *logs=nullptr;
            wait([&] {for(auto *widget:limited.findChildren<QTableWidget*>()) if(auto *table=dynamic_cast<ClientTable*>(widget)) if(table->isVisible() && table->columnForKey("loginTime")>=0) logs=table;return logs && logs->rowCount()>0;});
            record(331,"无业务菜单权限的普通账号仍可查看自身实际登录记录",logs && logs->rowCount()>0);
            auto profile=[&](const QString &key) {for(auto *line:limited.findChildren<QLineEdit*>()) if(line->property("profileField")==key)return line;return static_cast<QLineEdit*>(nullptr);};
            auto *name=profile("nickname"),*phone=profile("phone");
            QPushButton *save=nullptr;for(auto *button:limited.findChildren<QPushButton*>()) if(button->isVisible() && button->text()=="更新资料")save=button;
            if(!name || !phone || !save)return 16;
            name->clear();phone->clear();save->click();app.processEvents();
            bool errorsFit=true;int errors=0;
            for(auto *label:limited.findChildren<QLabel*>("promptError")) if(label->isVisible()) {++errors;for(auto *scroll:limited.findChildren<QScrollArea*>())if(scroll->isVisible() && scroll->isAncestorOf(label))scroll->ensureWidgetVisible(label,8,8);app.processEvents();errorsFit=errorsFit && label->visibleRegion().boundingRect().contains(label->rect());}
            record(329,"个人资料两项为空时红字完整显示且不被表单裁切",errors==2 && errorsFit);
            UiI18n::instance()->setEnglish(true);app.processEvents();bool translatedErrors=true;for(auto *label:limited.findChildren<QLabel*>("promptError"))if(label->isVisible())translatedErrors&=!label->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));record(332,"个人资料实际空值红字在英文切换后完整翻译",translatedErrors);UiI18n::instance()->setEnglish(false);app.processEvents();
            name->setText(QString(70,'x'));record(330,"个人资料账号姓名限制50字符",name->text().size()==50);
            name->setText("资料测试");phone->setText("138 0013 8000");save->click();
            record(329,"修正个人资料后正常保存并规范化空格手机号",wait([&] {return limited.user().value("nickname")=="资料测试" && limited.user().value("phone")=="13800138000";}));
            limited.navigate("/profile/password");app.processEvents();int revealed=0;bool icons=true;
            for(auto *line:limited.findChildren<QLineEdit*>()) if(line->isVisible() && line->echoMode()==QLineEdit::Password) {
                if(line->actions().isEmpty()) {icons=false;continue;}auto *action=line->actions().last();const auto icon=action->icon().cacheKey();action->trigger();
                icons=icons && line->echoMode()==QLineEdit::Normal && action->icon().cacheKey()!=icon;action->trigger();icons=icons && line->echoMode()==QLineEdit::Password;++revealed;
            }
            record(333,"修改密码三项均可切换明暗文且图标同步变化",revealed==3 && icons);
            UiI18n::instance()->setEnglish(true);limited.resize(1120,700);app.processEvents();bool passwordFits=true;for(auto *label:limited.findChildren<QLabel*>())if(label->isVisible() && !label->property("i18nSkip").toBool())passwordFits&=label->wordWrap() || label->width()>=label->fontMetrics().horizontalAdvance(label->text());record(334,"1120英文修改密码标签完整容纳，输入框不被标签遮挡",passwordFits);QPushButton *passwordSave=nullptr;for(auto *button:limited.findChildren<QPushButton*>())if(button->isVisible() && button->text()==UiI18n::instance()->text("保存"))passwordSave=button;if(passwordSave)passwordSave->click();auto *passwordToast=limited.findChild<QLabel*>("clientToast");record(335,"修改密码实际非法输入提示完成英文翻译",passwordSave && passwordToast && !passwordToast->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")) && passwordToast->text()==UiI18n::instance()->text("请检查旧密码、新密码和确认密码"));UiI18n::instance()->setEnglish(false);app.processEvents();
        }
        limited.close();
    }
    RegressionUrlCapture urls;
    QDesktopServices::setUrlHandler("https",&urls,"capture"); QDesktopServices::setUrlHandler("mailto",&urls,"capture");
    window.navigate("/support/contact");app.processEvents();
    QPushButton *mapButton=nullptr,*submitMessage=nullptr;
    for(auto *button:window.findChildren<QPushButton*>()) if(button->isVisible()) {
        if(button->text()=="查看地图")mapButton=button;if(button->text()=="提交留言")submitMessage=button;
    }
    if(mapButton)mapButton->click();
    record(336,"地图按钮发出有效高德地址搜索URL（拦截系统跳转）",mapButton && !urls.urls.isEmpty() && urls.urls.last().host()=="uri.amap.com" && QUrlQuery(urls.urls.last()).queryItemValue("keyword").contains("东南大道788号"));
    for(auto *line:window.findChildren<QLineEdit*>()) {
        if(line->property("contactField")=="name")line->setText("本机测试");
        if(line->property("contactField")=="contact")line->setText("test@example.invalid");
    }
    for(auto *box:window.findChildren<QComboBox*>()) if(box->property("contactField")=="type")box->setCurrentIndex(1);
    for(auto *text:window.findChildren<QTextEdit*>())if(text->property("contactField")=="description")text->setPlainText("只验证草稿参数，不发送。中文 & 符号");
    window.resize(1120,700);app.processEvents();bool contactFits=true;int contactFields=0;for(auto *field:window.findChildren<QWidget*>())if(field->property("contactField").isValid()){++contactFields;for(auto *scroll:window.findChildren<QScrollArea*>())if(scroll->isVisible() && scroll->isAncestorOf(field))scroll->ensureWidgetVisible(field);app.processEvents();contactFits&=window.rect().contains(QRect(field->mapTo(&window,QPoint()),field->size()));}record(337,"1120小窗在线留言各输入控件均能完整访问，不撑出页面",contactFields>=4 && contactFits);window.grab().save(QFileInfo(log.fileName()).absoluteDir().filePath("contact-form-small.png"));window.resize(1440,900);app.processEvents();
    if(submitMessage)submitMessage->click();
    const auto mail=urls.urls.isEmpty() ? QUrl() : urls.urls.last();const QUrlQuery draft(mail);
    record(338,"提交留言发出mailto草稿而非空HTTP地址（未实际发送）",submitMessage && mail.scheme()=="mailto" && draft.queryItemValue("body",QUrl::FullyDecoded).contains("中文 & 符号"));
    auto *messageToast=window.findChild<QLabel*>("clientToast");
    record(339,"留言明确提示尚未提交并交由邮件程序完成发送",mail.path()=="chengjing@bohr123.com" && messageToast && messageToast->text().contains("当前尚未提交留言"));
    QDesktopServices::unsetUrlHandler("https");QDesktopServices::unsetUrlHandler("mailto");
    window.navigate("/support/manual");app.processEvents();QPushButton *downloadManual=nullptr;
    bool outdated=false,editInstruction=false,qtProgram=false;for(auto *label:window.findChildren<QLabel*>())if(label->isVisible()){outdated|=label->text().contains("bohr-lan-client-setup.exe") || label->text().contains("双击设备行可快速编辑");editInstruction|=label->text().contains("点击设备行的“编辑”按钮");qtProgram|=label->text().contains("bohr-qt-client.exe");}record(341,"当前操作说明改为Qt程序名和编辑按钮，移除旧安装包及双击编辑错误文案",!outdated && editInstruction && qtProgram);
    for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()=="下载说明")downloadManual=button;
    const auto manualPath=QFileInfo(log.fileName()).absoluteDir().filePath("客户端实际下载说明.pdf");QFile::remove(manualPath);
    if(downloadManual) {
        QTimer::singleShot(40,&window,[&] {auto *dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("manual save dialog missing");dialog->selectFile(manualPath);QMetaObject::invokeMethod(dialog,"accept");});
        downloadManual->click();
    }
    QFile manual(manualPath),resource(":/client-manual.pdf");
    const auto actualManual=manual.open(QIODevice::ReadOnly) ? manual.readAll() : QByteArray();
    const auto expectedManual=resource.open(QIODevice::ReadOnly) ? resource.readAll() : QByteArray();
    record(340,"实际点击下载说明，得到新Qt博尔说明书且与内置PDF逐字节一致",downloadManual && actualManual.startsWith("%PDF") && actualManual==expectedManual);
    window.navigate("/device/list");app.processEvents();QTreeWidget *groups=nullptr;
    for(auto *candidate:window.findChildren<QTreeWidget*>("groupTree"))if(candidate->isVisible())groups=candidate;
    if(!groups || !wait([&]{return groups->topLevelItemCount()>0;}))return 17;
    auto nodeFor=[&](const QString &type,qint64 id=0) {for(QTreeWidgetItemIterator it(groups);*it;++it)if((*it)->data(0,Qt::UserRole+1)==type && (id==0 || (*it)->data(0,Qt::UserRole).toLongLong()==id))return *it;return static_cast<QTreeWidgetItem*>(nullptr);};
    auto groupByName=[&](const QString &name) {for(const auto &value:apiRows(request("GET","/group/all").data))if(value.toObject().value("name")==name)return value.toObject();return QJsonObject{};};
    auto contextGroup=[&](QTreeWidgetItem *node,const QString &action,const QString &name,int expectedParent,int row) {
        if(!node)qFatal("group context target missing");const auto nodeType=node->data(0,Qt::UserRole+1).toString();const auto nodeId=node->data(0,Qt::UserRole).toLongLong();groups->scrollToItem(node);app.processEvents();
        wait([&]{node=nodeFor(nodeType,nodeId);return node!=nullptr;});if(!node)qFatal("group context target disappeared");
        QTimer::singleShot(30,&window,[&] {
            auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)qFatal("group context menu missing");QAction *chosen=nullptr;bool createsRoot=false;for(auto *item:menu->actions()){if(item->text()==action)chosen=item;createsRoot|=item->text()=="新增分组";}if(nodeType=="group")record(89,"实体分组菜单仅新增子级或同级，不再创建上级根分组",!createsRoot);if(!chosen){qInfo("Requested group action: %s",qPrintable(action));for(auto *item:menu->actions())qInfo("Available group action: %s",qPrintable(item->text()));qFatal("group context action missing");}
            if(action=="重命名") {
                bool rebuilt=false;const auto connection=QObject::connect(groups->model(),&QAbstractItemModel::modelReset,&window,[&]{rebuilt=true;});
                for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()=="刷新" && button->objectName()=="clientLink"){button->click();break;}
                wait([&]{return rebuilt && nodeFor(nodeType,nodeId);});QObject::disconnect(connection);
            }
            QTimer::singleShot(30,&window,[&] {
                auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("group editor missing");QComboBox *parent=nullptr;
                for(auto *box:dialog->findChildren<QComboBox*>())if(box->property("formFieldKey")=="parentId")parent=box;
                record(row,"分组弹窗默认上级符合实际操作："+action,parent && parent->currentData().toLongLong()==expectedParent);
                if(action=="新增分组")record(73,"未分组新增只提供顶级父级，避免误建到实体总分组",parent && parent->count()==1 && parent->currentData().toLongLong()==0);
                if(action=="新增子分组")record(84,"客户端新增子组弹窗紧凑且上级选择控件在窗口内",dialog->width()<=500 && dialog->height()<=360 && parent && dialog->rect().contains(QRect(parent->mapTo(dialog,QPoint()),parent->size())));
                if(action=="新增子分组")record(83,"实体组新增父级列表限本组和后代，不出现其他顶级或祖先",parent && parent->count()==1 && parent->itemData(0).toLongLong()==expectedParent);
                QEvent leave(QEvent::Leave);QApplication::sendEvent(dialog,&leave);record(action=="新增子分组" ? 86 : 75,"分组编辑鼠标离开仍保持窗口和输入",dialog->isVisible());
                for(auto *line:dialog->findChildren<QLineEdit*>())if(line->property("formFieldKey")=="name")line->setText(name);
                for(auto *button:dialog->findChildren<QPushButton*>())if(button->text()=="确定"){button->click();return;}
            });
            menu->setActiveAction(chosen);QKeyEvent key(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&key);
        });
        groups->customContextMenuRequested(groups->visualItemRect(node).center());
    };
    contextGroup(nodeFor("ungrouped"),"新增分组","UI顶级分组",0,72);
    wait([&]{return !groupByName("UI顶级分组").isEmpty();});const auto topGroup=groupByName("UI顶级分组");
    record(82,"客户端选择顶级分组创建后与总分组同级",!topGroup.isEmpty() && topGroup.value("parentId").isNull());
    const qint64 topId=topGroup.value("id").toInteger();wait([&]{return nodeFor("group",topId)!=nullptr;});
    contextGroup(nodeFor("group",topId),"新增子分组","UI子分组",int(topId),88);
    record(88,"在实体分组新增子分组保留指定父级",wait([&]{return groupByName("UI子分组").value("parentId").toInteger()==topId;}));
    contextGroup(nodeFor("group",1),"新增子分组","总组下新子组",1,83);
    record(83,"总分组右键新增子组实际保存到总组，不混入其他顶级",wait([&]{return groupByName("总组下新子组").value("parentId").toInteger()==1;}));
    const auto childToDelete=groupByName("总组下新子组").value("id").toInteger();const auto keptDevice=request("POST","/device",{{"code","client-group-delete-kept"},{"name","分组删除保留"},{"groupId",childToDelete}}).data.toObject().value("id").toInteger();window.resize(1120,700);window.navigate("/device/list");app.processEvents();QTreeWidgetItem *childNode=nullptr;wait([&]{childNode=nodeFor("group",childToDelete);return childNode!=nullptr;});if(!childNode)return 22;groups->scrollToItem(childNode);app.processEvents();childNode=nodeFor("group",childToDelete);if(!childNode)return 22;
    QTimer::singleShot(30,&window,[&]{auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());QAction *remove=nullptr;if(menu)for(auto *action:menu->actions())if(action->text()=="删除分组")remove=action;if(menu){qInfo("Client group menu local=(%d,%d) size=%dx%d window=%dx%d",(menu->mapToGlobal(QPoint())-window.mapToGlobal(QPoint())).x(),(menu->mapToGlobal(QPoint())-window.mapToGlobal(QPoint())).y(),menu->width(),menu->height(),window.width(),window.height());menu->grab().save(QFileInfo(log.fileName()).absoluteDir().filePath("client-group-menu-small.png"));}record(85,"客户端1120小窗分组菜单支持删除且不越出窗口",menu && remove && window.rect().contains(QRect((menu->mapToGlobal(QPoint())-window.mapToGlobal(QPoint())),menu->size())));if(!remove)qFatal("client group delete missing");QTimer::singleShot(30,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("client group delete confirmation missing");dialog->findChild<QPushButton*>("primaryButton")->click();});menu->setActiveAction(remove);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);});groups->customContextMenuRequested(groups->visualItemRect(childNode).center());record(85,"客户端删除非空分组后设备保留并移至未分组",wait([&]{return groupByName("总组下新子组").isEmpty() && request("GET","/device/"+QString::number(keptDevice)).data.toObject().value("groupId").isNull();}));
    const auto moveOne=request("POST","/device",{{"code","batch-move-en-one"},{"name","移出测试一"},{"groupId",1}}).data.toObject().value("id").toInteger(),moveTwo=request("POST","/device",{{"code","batch-move-en-two"},{"name","移出测试二"},{"groupId",1}}).data.toObject().value("id").toInteger();window.navigate("/device/list");app.processEvents();ClientTable *deviceRows=nullptr;for(auto *widget:window.findChildren<QTableWidget*>())if(auto *candidate=dynamic_cast<ClientTable*>(widget))if(candidate->isVisible() && candidate->columnForKey("code")>=0)deviceRows=candidate;if(!deviceRows)return 23;QTreeWidgetItem *rootNode=nullptr;wait([&]{rootNode=nodeFor("group",1);if(!rootNode)return false;QSet<qint64> ids;for(int n=0;n<rootNode->childCount();++n)if(rootNode->child(n)->data(0,Qt::UserRole+1)=="device")ids.insert(rootNode->child(n)->data(0,Qt::UserRole).toLongLong());return ids.contains(moveOne) && ids.contains(moveTwo);});if(!rootNode)return 24;groups->setCurrentItem(rootNode);groups->itemClicked(rootNode,0);if(!wait([&]{return deviceRows->isEnabled() && deviceRows->rowCount()==2;})){qInfo("Move fixture rows=%d enabled=%d",deviceRows->rowCount(),deviceRows->isEnabled());return 25;}for(int row=0;row<2;++row)deviceRows->cellWidget(row,0)->findChild<QCheckBox*>()->setChecked(true);UiI18n::instance()->setEnglish(true);app.processEvents();QPushButton *batchRemove=nullptr;for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->text()==UiI18n::instance()->text("批量移出分组"))batchRemove=button;if(!batchRemove)return 26;
    QTimer::singleShot(30,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("batch move out missing");bool english=true;for(auto *label:dialog->findChildren<QLabel*>())english&=!label->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));record(123,"设备批量移出英文二次确认没有残留中文",english);dialog->findChild<QPushButton*>("primaryButton")->click();});batchRemove->click();record(101,"批量移出后当前实体分组自动重新筛选，不显示已移出设备",wait([&]{return deviceRows->isEnabled() && deviceRows->rowCount()==0;}));auto *moveToast=window.findChild<QLabel*>("clientToast");record(123,"批量移出实际成功提示英文，两个设备分组置空",moveToast && moveToast->text()==UiI18n::instance()->text("设备已移出分组") && request("GET","/device/"+QString::number(moveOne)).data.toObject().value("groupId").isNull() && request("GET","/device/"+QString::number(moveTwo)).data.toObject().value("groupId").isNull());UiI18n::instance()->setEnglish(false);app.processEvents();window.resize(1440,900);
    contextGroup(nodeFor("group",topId),"重命名","UI顶级改名",topGroup.value("parentId").toInt(),80);
    record(80,"分组合法重命名不会被不变的父级字段误拒绝",wait([&]{return groupByName("UI顶级改名").value("id").toInteger()==topId;}));
    record(81,"右键菜单打开期间刷新分组后仍可安全编辑原节点",groupByName("UI顶级改名").value("id").toInteger()==topId);

    const qint64 deleteId=request("POST","/device",{{"code","context-delete"},{"name","待删除测试设备"}}).data.toObject().value("id").toInteger();window.navigate("/device/list");QTreeWidgetItem *deleteNode=nullptr;wait([&]{deleteNode=nodeFor("device",deleteId);return deleteNode!=nullptr;});
    for(bool accept:{false,true}) {
        if(accept)UiI18n::instance()->setEnglish(true);app.processEvents();deleteNode=nodeFor("device",deleteId);if(!deleteNode)qFatal("context delete fixture missing");groups->scrollToItem(deleteNode);app.processEvents();
        QTimer::singleShot(30,&window,[&]{auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());QAction *chosen=nullptr;bool misleadingMove=false;if(menu)for(auto *action:menu->actions()){if(action->text()==UiI18n::instance()->text("删除设备"))chosen=action;misleadingMove|=action->text()==UiI18n::instance()->text("移出分组");}
            record(77,"未分组设备右键提供真正删除且不显示无意义移出",chosen && !misleadingMove);if(!chosen)qFatal("delete context action missing");
            QTimer::singleShot(30,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());record(78,"设备右键删除前必须二次确认",dialog!=nullptr);if(!dialog)qFatal("delete confirmation missing");
                if(accept){bool translated=true;for(auto *label:dialog->findChildren<QLabel*>())translated&=!label->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"));record(125,"英文删除设备确认弹窗不残留中文提示",translated);dialog->findChild<QPushButton*>("primaryButton")->click();}else dialog->reject();});
            menu->setActiveAction(chosen);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);
        });groups->customContextMenuRequested(groups->visualItemRect(deleteNode).center());
        if(!accept)record(78,"取消设备删除后记录保留",request("GET","/device/"+QString::number(deleteId)).ok);
        else record(77,"确认后设备实际删除，不是移到未分组",wait([&]{return !request("GET","/device/"+QString::number(deleteId)).ok;}));
    }
    UiI18n::instance()->setEnglish(false);app.processEvents();
    qint64 deepParent=topId;
    for(int level=0;level<10;++level){const auto value=request("POST","/group",{{"name",QString("界面第%1层分组").arg(level+1)},{"parentId",deepParent}});if(!value.ok)return 18;deepParent=value.data.toObject().value("id").toInteger();}
    window.navigate("/device/list");QTreeWidgetItem *deep=nullptr;
    wait([&]{deep=nodeFor("group",deepParent);return deep!=nullptr;});
    if(deep){groups->scrollToItem(deep);groups->horizontalScrollBar()->setValue(groups->horizontalScrollBar()->maximum());app.processEvents();}
    record(90,"客户端十级分组可横向滚动完整显示末级名称",deep && groups->horizontalScrollBar()->maximum()>0 && groups->visualItemRect(deep).width()>=groups->fontMetrics().horizontalAdvance(deep->text(0))+24);
    const auto collision=request("POST","/group",{{"name","空闲"},{"parentId",QJsonValue(QJsonValue::Null)}}).data.toObject().value("id").toInteger();window.navigate("/device/list");
    QTreeWidgetItem *businessGroup=nullptr;wait([&]{businessGroup=nodeFor("group",collision);return businessGroup!=nullptr;});if(!businessGroup)return 19;
    groups->setCurrentItem(businessGroup);groups->itemClicked(businessGroup,0);app.processEvents();QLabel *selection=nullptr;
    for(auto *label:window.findChildren<QLabel*>())if(label->isVisible() && label->property("selectionSource").isValid())selection=label;
    record(124,"选中其他实体分组时当前选择立即回显其名称",selection && selection->text()=="空闲");
    UiI18n::instance()->setEnglish(true);app.processEvents();record(124,"与状态文案同名的业务分组切英文仍保留原名",selection && selection->text()=="空闲");
    for(auto *button:window.findChildren<QPushButton*>())if(button->isVisible() && button->property("clearGroupSelection").toBool())button->click();app.processEvents();
    record(124,"英文清空当前选择正确显示All Groups",selection && selection->text()=="All Groups");
    if(selection && selection->text()!="All Groups")qInfo("SELECTION text=%s tooltip=%s width=%d",qPrintable(selection->text()),qPrintable(selection->toolTip()),selection->width());
    UiI18n::instance()->setEnglish(false);app.processEvents();record(124,"切回中文清空范围名称恢复全部分组",selection && selection->text()=="全部分组");
    ClientTable *deviceList=nullptr;for(auto *widget:window.findChildren<QTableWidget*>())if(auto *table=dynamic_cast<ClientTable*>(widget))if(table->isVisible() && table->columnForKey("code")>=0)deviceList=table;
    if(!deviceList || !wait([&]{return deviceList->rowCount()>0;}))return 22;
    const auto editedId=deviceList->recordAt(0).value("id").toInteger();auto *deviceActions=deviceList->findChild<QTableWidget*>("clientFrozenActions");QPushButton *editDevice=nullptr;
    if(deviceActions)if(auto *actions=deviceActions->cellWidget(0,0))for(auto *button:actions->findChildren<QPushButton*>())if(button->text()=="编辑")editDevice=button;
    if(!editDevice)return 23;
    QTimer::singleShot(40,&window,[&] {
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("optional type client editor missing");
        for(auto *box:dialog->findChildren<QComboBox*>())if(box->property("formFieldKey")=="type")box->setCurrentIndex(0);
        for(auto *button:dialog->findChildren<QPushButton*>())if(button->text()=="确定"){button->click();return;}
    });
    editDevice->click();record(469,"客户端设备类型未选择时实际保存成功",wait([&]{return request("GET","/device/"+QString::number(editedId)).data.toObject().value("type").toString().isEmpty();}));
    return failures ? 1 : 0;
}

#include "catalog_regression.moc"
