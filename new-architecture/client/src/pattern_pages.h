#pragma once
#include "pages.h"
class ApiClient;
class ClientWindow;
ClientPage *createPatternPage(const QString &path,ApiClient *api,ClientWindow *window);
