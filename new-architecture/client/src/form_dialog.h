#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QList>
#include <functional>
#include <optional>

class QFormLayout;
class QLabel;
class QComboBox;
class ClientPagination;

struct FormField {
    enum Kind { Text, Multiline, Secret, Integer, Decimal, Choice, MultiChoice, Toggle, File };
    QString label,key,inputPattern;
    Kind kind=Text;
    bool required=false,readonly=false,editable=false;
    bool preserveChoices=true;
    bool preserveLabel=false;
    int maximum=255;
    QList<QPair<QString,QJsonValue>> choices;
    std::function<void(QComboBox*)> quickCreate;
};

class ClientFormDialog : public QDialog {
public:
    ClientFormDialog(QWidget *parent,const QString &title,int width=560,int height=480);
    void addField(const FormField &field,const QJsonValue &initial={});
    void setFieldPagination(int pageSize,int fixedFields=0);
    std::optional<QJsonObject> execute();
    void setConfirmDiscard(bool enabled) { confirmDiscard_=enabled; }
    void setValidation(std::function<QString(const QJsonObject&)> validation) {validation_=std::move(validation);}
    void reject() override;
private:
    QJsonObject values() const;
    void revealField(QWidget *field);
    ClientPagination *fieldPagination_=nullptr;
    int fixedFields_=0;
    QJsonObject initialValues_;
    bool confirmDiscard_=false;
    bool customError_=false;
    std::function<QString(const QJsonObject&)> validation_;
    QFormLayout *form_;
    QLabel *error_;
    QList<QPair<FormField,QWidget*>> fields_;
};
