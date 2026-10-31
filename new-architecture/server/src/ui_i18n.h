#pragma once

#include <QObject>
#include <QString>
#include <QHash>

class QApplication;
class QComboBox;
class QWidget;

class UiI18n : public QObject {
    Q_OBJECT
public:
    static constexpr int PreserveDataRole=Qt::UserRole+122;
    static constexpr int OriginalRole=Qt::UserRole+120;
    static UiI18n *instance();
    explicit UiI18n(QApplication *app);
    bool english() const { return english_; }
    void setEnglish(bool english);
    QString text(const QString &source) const;
    QString sourceText(const QComboBox *combo) const;
void refresh();
signals:
    void languageChanged();
protected:
    bool eventFilter(QObject *watched,QEvent *event) override;
private:
    void refreshWidget(QWidget *widget);
    QHash<QString,QString> words_;
    bool english_=false;
    bool refreshing_=false;
    bool scheduled_=false;
};
