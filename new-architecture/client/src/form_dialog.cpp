#include "form_dialog.h"
#include "page_widgets.h"
#include "client_file_dialog.h"
#include "ui_controls.h"
#include "ui_dialogs.h"
#include "ui_i18n.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QTextEdit>
#include <QTreeWidget>
#include <QMouseEvent>
#include <QVBoxLayout>

namespace {
class CheckTree : public QTreeWidget {
protected:
    void mousePressEvent(QMouseEvent *event) override {
        pressed_=itemAt(event->pos());
        previous_=pressed_ ? pressed_->checkState(0) : Qt::Unchecked;
        QTreeWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        QTreeWidget::mouseReleaseEvent(event);
        QTreeWidgetItem *item=itemAt(event->pos());
        if(item && item==pressed_ && item->checkState(0)==previous_)
            item->setCheckState(0,previous_==Qt::Checked ? Qt::Unchecked : Qt::Checked);
        clearSelection(); clearFocus(); pressed_=nullptr;
    }
private:
    QTreeWidgetItem *pressed_=nullptr;
    Qt::CheckState previous_=Qt::Unchecked;
};
}

ClientFormDialog::ClientFormDialog(QWidget *parent,const QString &title,int width,int height)
    :QDialog(parent) {
    setObjectName("editorDialog"); setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
    UiModalDialog::attach(this,parent);
    setWindowTitle(title); resize(width,height);
    if(parent && parent->window()) {
        auto *overlay=new QWidget(parent->window()); overlay->setObjectName("dialogOverlay");
        overlay->setGeometry(parent->window()->rect());
        overlay->setStyleSheet("background: rgba(0,0,0,112);"); overlay->show(); overlay->raise();
        connect(this,&QDialog::finished,overlay,[overlay]{ overlay->hide(); overlay->deleteLater(); });
    }
    auto *outer=new QVBoxLayout(this); outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);
    auto *header=new QFrame; header->setObjectName("dialogHeader"); header->setFixedHeight(54);
    auto *head=new QHBoxLayout(header); head->setContentsMargins(20,0,16,0);
    auto *caption=new QLabel(title); caption->setObjectName("dialogTitle"); head->addWidget(caption); head->addStretch();
    auto *close=new QPushButton("×"); close->setObjectName("dialogClose"); head->addWidget(close);
    close->setAutoDefault(false);close->setFocusPolicy(Qt::NoFocus);
    connect(close,&QPushButton::clicked,this,&QDialog::reject); outer->addWidget(header);
    auto *scroll=new QScrollArea;scroll->setObjectName("dialogScroll"); scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content=new QWidget; content->setObjectName("dialogContent");
    auto *contentLayout=new QVBoxLayout(content);
    contentLayout->setContentsMargins(22,17,22,12);
    form_=new QFormLayout; form_->setContentsMargins(0,0,0,0); form_->setSpacing(12);
    form_->setLabelAlignment(Qt::AlignRight|Qt::AlignVCenter);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    contentLayout->addLayout(form_);
    error_=new QLabel; error_->setObjectName("promptError"); error_->setFixedHeight(20);
    error_->hide(); contentLayout->addWidget(error_); contentLayout->addStretch();
    scroll->setWidget(content); outer->addWidget(scroll,1);
    auto *footer=new QFrame; footer->setObjectName("dialogFooter");
    auto *buttons=new QHBoxLayout(footer); buttons->setContentsMargins(18,10,18,10); buttons->addStretch();
    auto *cancel=new QPushButton("取消"), *accept=new QPushButton("确定");
    accept->setObjectName("primaryButton"); buttons->addWidget(cancel); buttons->addWidget(accept);
    outer->addWidget(footer);
    accept->setDefault(true);
    connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
    connect(accept,&QPushButton::clicked,this,[this] {
        for(const auto &entry:fields_) {
            const FormField &field=entry.first; QWidget *widget=entry.second;
            QString value;
            if(auto *line=qobject_cast<QLineEdit*>(widget)) value=line->text().trimmed();
            else if(auto *multi=qobject_cast<QTextEdit*>(widget)) value=multi->toPlainText().trimmed();
            else if(auto *combo=qobject_cast<QComboBox*>(widget))
                value=field.editable ? editableChoiceText(combo) : combo->currentData().toString();
            else if(field.kind==FormField::File) {
                if(auto *line=widget->findChild<QLineEdit*>("filePathInput")) value=line->text().trimmed();
            } else if(field.kind==FormField::MultiChoice) {
                if(auto *tree=qobject_cast<QTreeWidget*>(widget))
                    for(QTreeWidgetItemIterator it(tree);*it;++it)
                        if((*it)->checkState(0)==Qt::Checked) { value="selected"; break; }
            }
            if(field.required && !widget->property("skipRequired").toBool() && value.isEmpty() && field.kind!=FormField::Toggle &&
               field.kind!=FormField::Integer && field.kind!=FormField::Decimal) {
                error_->setText("请填写"+field.label); error_->show();revealField(widget); widget->setFocus(); return;
            }
            if(field.maximum>0 && value.size()>field.maximum) {
                error_->setText(field.label+"不能超过"+QString::number(field.maximum)+"字");
                error_->show();revealField(widget); widget->setFocus(); return;
            }
        }
        if(validation_) {const QString error=validation_(values());customError_=!error.isEmpty();if(customError_){error_->setText(error);error_->show();return;}}
        this->accept();
    });
}

void ClientFormDialog::addField(const FormField &field,const QJsonValue &initial) {
    QWidget *control=nullptr;
    if(field.kind==FormField::Choice) {
        auto *combo=new UiComboBox;
        combo->setEditable(field.editable);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(8);
        combo->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
        for(const auto &choice:field.choices) {
            combo->addItem(choice.first,choice.second.toVariant());
            const bool systemGroup=(field.key=="groupId" || field.key=="parentId") && choice.second.toInteger()==1 && choice.first.trimmed()=="总分组";
            if(field.preserveChoices && !systemGroup && !choice.second.isNull() &&
               !choice.second.toVariant().toString().isEmpty())
                combo->setItemData(combo->count()-1,true,UiI18n::PreserveDataRole);
        }
        for(int i=0;i<combo->count();++i) if(combo->itemData(i)==initial.toVariant()) {
            combo->setCurrentIndex(i); break;
        }
        if(field.editable) combo->setCurrentText(initial.toString());
        control=combo;
    } else if(field.kind==FormField::MultiChoice) {
        auto *tree=new CheckTree; tree->setHeaderHidden(true); tree->setFixedHeight(190);
        tree->setFocusPolicy(Qt::NoFocus);
        for(const auto &choice:field.choices) {
            auto *item=new QTreeWidgetItem(tree,{choice.first});
            item->setData(0,Qt::UserRole,choice.second.toVariant());
            item->setData(0,UiI18n::PreserveDataRole,true);
            item->setFlags(item->flags()|Qt::ItemIsUserCheckable);
            item->setCheckState(0,initial.toArray().contains(choice.second) ? Qt::Checked : Qt::Unchecked);
        }
        control=tree;
    } else if(field.kind==FormField::File) {
        auto *holder=new QWidget; auto *layout=new QHBoxLayout(holder);
        layout->setContentsMargins(0,0,0,0);
        auto *line=new QLineEdit; line->setObjectName("filePathInput"); line->setReadOnly(true);
        line->setPlaceholderText("未选择文件");
        auto *browse=new QPushButton("选择文件"); browse->setObjectName("primaryButton");
        layout->addWidget(line,1); layout->addWidget(browse);
        connect(browse,&QPushButton::clicked,this,[this,line] {
            const QString path=ClientFileDialog::getOpenFileName(this,"选择文件");
            if(!path.isEmpty()) line->setText(path);
        });
        control=holder;
    } else if(field.kind==FormField::Multiline) {
        auto *edit=new QTextEdit; edit->setPlainText(initial.toString());
        edit->setFixedHeight(field.key=="importText" ? 215 : 82);
        edit->setReadOnly(field.readonly); control=edit;
    } else if(field.kind==FormField::Toggle) {
        auto *check=new QCheckBox; check->setChecked(initial.toBool()); control=check;
    } else if(field.kind==FormField::Integer || field.kind==FormField::Decimal) {
        auto *stepper=new NumericStepper(field.kind==FormField::Decimal ? 3 : 0);
        stepper->setRange(0,field.kind==FormField::Decimal ? 999999999.999 : 2147483647);
        stepper->setValue(initial.toDouble()); control=stepper;
    } else {
        auto *line=new QLineEdit(initial.toVariant().toString());
        line->setMaxLength(field.maximum>0 ? field.maximum : 32767);
        if(!field.inputPattern.isEmpty()) line->setValidator(new QRegularExpressionValidator(QRegularExpression(field.inputPattern),line));
        line->setReadOnly(field.readonly);
        if(field.required && !field.readonly) {
            line->setPlaceholderText("请输入"+field.label);
            connect(line,&QLineEdit::textChanged,this,[this,line,field] {
                if(line->property("skipRequired").toBool()) return;
                if(line->text().trimmed().isEmpty()) {
                    error_->setProperty("fieldKey",field.key);
                    error_->setText("请填写"+field.label); error_->show();
                } else if(error_->property("fieldKey").toString()==field.key) error_->hide();
            });
        }
        if(field.kind==FormField::Secret) { line->clear(); line->setEchoMode(QLineEdit::Password); }
        control=line;
    }
    control->setProperty("formFieldKey",field.key);
    if(auto *line=qobject_cast<QLineEdit*>(control))connect(line,&QLineEdit::textChanged,this,[this]{if(validation_ && customError_){const auto error=validation_(values());error_->setText(error);error_->setVisible(!error.isEmpty());customError_=!error.isEmpty();}});
    if(field.kind==FormField::Choice && field.readonly)control->setEnabled(false);
    control->setMinimumHeight(field.kind==FormField::Multiline ?
        (field.key=="importText" ? 215 : 82) : 33);
    QWidget *rowControl=control;
    if(field.kind==FormField::Choice && field.quickCreate) {
        auto *holder=new QWidget;
        auto *line=new QHBoxLayout(holder); line->setContentsMargins(0,0,0,0);
        line->setSpacing(7); line->addWidget(control,1);
        auto *button=new QPushButton("新增"); line->addWidget(button);
        button->setProperty("quickCreateField",field.key);
        connect(button,&QPushButton::clicked,this,[field,control] {
            field.quickCreate(qobject_cast<QComboBox*>(control));
        });
        rowControl=holder;
    }
    auto *label=new QLabel(field.label);
    if(field.preserveLabel) {label->setProperty("i18nSkip",true);label->setTextFormat(Qt::PlainText);label->setToolTip(field.label);label->setMaximumWidth(200);label->setText(label->fontMetrics().elidedText(field.label,Qt::ElideRight,200));}
    label->setWordWrap(!field.preserveLabel);
    form_->addRow(label,rowControl);
    fields_ << qMakePair(field,control);
}

void ClientFormDialog::setFieldPagination(int pageSize,int fixedFields) {
    if(fieldPagination_ || fields_.size()-fixedFields<=pageSize)return;
    fixedFields_=qMax(0,fixedFields);fieldPagination_=new ClientPagination(this);fieldPagination_->setObjectName("formPagination");
    fieldPagination_->setTotal(fields_.size()-fixedFields_);
    auto update=[this]{const int first=(fieldPagination_->page()-1)*fieldPagination_->pageSize()+fixedFields_,last=first+fieldPagination_->pageSize();for(int row=0;row<fields_.size();++row)form_->setRowVisible(row,row<fixedFields_ || (row>=first && row<last));};
    connect(fieldPagination_,&ClientPagination::changed,this,update);
    static_cast<QVBoxLayout*>(layout())->insertWidget(layout()->count()-1,fieldPagination_);
    if(auto *sizes=fieldPagination_->findChild<QComboBox*>()) {const int at=sizes->findData(pageSize);if(at>=0)sizes->setCurrentIndex(at);}
    update();
}
void ClientFormDialog::revealField(QWidget *field) {
    if(fieldPagination_)for(int row=fixedFields_;row<fields_.size();++row)if(fields_[row].second==field){fieldPagination_->setPage((row-fixedFields_)/fieldPagination_->pageSize()+1);break;}
    if(auto *scroll=findChild<QScrollArea*>("dialogScroll"))scroll->ensureWidgetVisible(field);
}

std::optional<QJsonObject> ClientFormDialog::execute() {
    initialValues_=values();
    if(exec()!=QDialog::Accepted) return std::nullopt;
    return values();
}

void ClientFormDialog::reject() {
    if(confirmDiscard_ && values()!=initialValues_ &&
       UiMessageBox::question(this,"放弃编辑","是否放弃编辑？")!=UiMessageBox::Yes) return;
    QDialog::reject();
}

QJsonObject ClientFormDialog::values() const {
    QJsonObject result;
    for(const auto &entry:fields_) {
        const FormField &field=entry.first; QWidget *widget=entry.second;
        if(auto *line=qobject_cast<QLineEdit*>(widget)) {
            if(field.kind==FormField::Secret && line->text().isEmpty()) continue;
            result[field.key]=line->text().trimmed();
        } else if(auto *multi=qobject_cast<QTextEdit*>(widget)) result[field.key]=multi->toPlainText().trimmed();
        else if(auto *check=qobject_cast<QCheckBox*>(widget)) result[field.key]=check->isChecked();
        else if(auto *stepper=qobject_cast<NumericStepper*>(widget)) {
            if(field.kind==FormField::Integer) result[field.key]=int(stepper->value());
            else result[field.key]=stepper->value();
        } else if(auto *combo=qobject_cast<QComboBox*>(widget)) {
            if(field.editable) result[field.key]=editableChoiceText(combo);
            else {
                const QVariant value=combo->currentData();
                result[field.key]=value.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue::fromVariant(value);
            }
        } else if(auto *tree=qobject_cast<QTreeWidget*>(widget)) {
            QJsonArray selected;
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->checkState(0)==Qt::Checked)
                    selected.append(QJsonValue::fromVariant((*it)->data(0,Qt::UserRole)));
            result[field.key]=selected;
        } else if(field.kind==FormField::File) {
            if(auto *line=widget->findChild<QLineEdit*>("filePathInput")) result[field.key]=line->text().trimmed();
        }
    }
    return result;
}
