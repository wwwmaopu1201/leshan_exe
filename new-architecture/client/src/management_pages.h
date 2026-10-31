#pragma once
#include "pages.h"
class ApiClient;
class ClientWindow;
ClientPage *createManagementPage(const QString &path,ApiClient *api,ClientWindow *window);
