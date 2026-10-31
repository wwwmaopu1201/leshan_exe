#pragma once
#include "pages.h"
class ApiClient;
class ClientWindow;
ClientPage *createProfileSupportPage(const QString &path,ApiClient *api,ClientWindow *window);
