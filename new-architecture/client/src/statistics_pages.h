#pragma once
#include "pages.h"
class ApiClient;
class ClientWindow;
ClientPage *createStatisticsPage(const QString &path,ApiClient *api,ClientWindow *window);
