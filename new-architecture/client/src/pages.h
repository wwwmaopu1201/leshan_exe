#pragma once

#include <QWidget>

class ApiClient;
class ClientWindow;

class ClientPage : public QWidget {
public:
    explicit ClientPage(QWidget *parent=nullptr):QWidget(parent) {}
    virtual void refresh() {}
};

ClientPage *createClientPage(const QString &path,ApiClient *api,ClientWindow *window);
