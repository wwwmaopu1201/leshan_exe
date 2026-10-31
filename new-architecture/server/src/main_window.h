#pragma once

#include <QMainWindow>
#include <QHash>
#include <QSet>

class ApiServer;
class Database;
class DeviceServer;
class QLabel;
class QLineEdit;
class QComboBox;
class DateRangeWidget;
class QPushButton;
class PortStepper;
class PatternFilesPage;
class PatternStatsPage;
class QListWidget;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;
class QTextEdit;
class QComboBox;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Database *db, ApiServer *server, DeviceServer *deviceServer, QWidget *parent = nullptr);
signals:
    void logoutRequested();
private:
    friend int runCustomerUiRegression();
    void showPage(int index);
    void refreshTable(int index);
    void editRecord(int index, bool creating);
    void deleteRecord(int index);
    void moveSelected(int index);
    QList<qint64> selectedIds(int index) const;
    QSet<qint64> selectedDevices_;
    void refreshHome();
    void refreshLogs();
    void refreshGroupTree(int index);
    void showFeedback(const QString &message,bool error=false);
    void applyLanguageButtons();
    Database *db_;
    ApiServer *server_;
    DeviceServer *deviceServer_;
    QListWidget *navigation_;
    QStackedWidget *pages_;
    QList<QTableWidget*> tables_;
    QList<QLineEdit*> filters_;
    QList<QLabel*> footers_;
    QHash<int,QLabel*> contextNotes_;
    QList<QLabel*> statValues_;
    QHash<int,QTreeWidget*> trees_;
    QHash<int,QLabel*> scopeLabels_;
    QHash<int,bool> ungroupedModes_;
    QHash<int,int> currentPages_;
    QHash<int,QComboBox*> pageSizes_;
    QHash<int,QPushButton*> previousButtons_;
    QHash<int,QPushButton*> nextButtons_;
    QHash<int,QPushButton*> pageButtons_;
    QHash<int,PortStepper*> jumpers_;
    QHash<int,QPushButton*> moveButtons_;
    QHash<int,QPushButton*> batchDeleteButtons_;
    QHash<int,QComboBox*> optionFilters_;
    QHash<int,DateRangeWidget*> dateRanges_;
    QLabel *summary_;
    QListWidget *console_;
    QComboBox *logLevel_;
    QLabel *feedbackLabel_;
    QTimer *feedbackTimer_;
    QPushButton *languageZhButton_;
    QPushButton *languageEnButton_;
    PatternFilesPage *patternFilesPage_;
    PatternStatsPage *patternStatsPage_;
};
