#pragma once

#include <QWidget>
#include <functional>

class Database;
class DeviceServer;
class QComboBox;
class QLineEdit;
class QTableWidget;
class QLabel;
class QPushButton;
class DateRangeWidget;

class PatternStatsPage : public QWidget {
public:
    explicit PatternStatsPage(Database *db,QWidget *parent=nullptr);
    void refresh();
private:
    Database *db_;
    DateRangeWidget *dates_;
    QComboBox *device_;
    QTableWidget *summary_,*details_;
};

class PatternFilesPage : public QWidget {
public:
    explicit PatternFilesPage(Database *db,DeviceServer *devices,
                              std::function<void(const QString&)> feedback,QWidget *parent=nullptr);
    void refresh();
private:
    void refreshServer();
    void refreshDeviceFiles();
    void uploadFile();
    void editPattern(qint64 id);
    void downloadToDevice(qint64 patternId);
    void openDownloadQueue();
    void openUploadQueue();
    Database *db_;
    DeviceServer *devices_;
    std::function<void(const QString&)> feedback_;
    QLineEdit *keyword_,*orderNo_,*deviceKeyword_;
    QComboBox *type_,*deviceSelect_,*deviceType_;
    DateRangeWidget *dates_;
    QTableWidget *serverTable_,*deviceTable_;
    QLabel *serverTotal_,*deviceTotal_,*serverPageLabel_,*devicePageLabel_;
    QPushButton *serverPrev_,*serverNext_,*devicePrev_,*deviceNext_;
    QComboBox *serverPageSize_,*devicePageSize_;
    QLineEdit *serverJump_,*deviceJump_;
    int serverPage_=1,devicePage_=1;
};
