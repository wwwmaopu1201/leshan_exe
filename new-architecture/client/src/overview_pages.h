#pragma once
#include "pages.h"
class ApiClient;
class ClientWindow;
ClientPage *createOverviewPage(const QString &path,ApiClient *api,ClientWindow *window);
