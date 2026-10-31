#pragma once

#include "pages.h"

class ApiClient;
class ClientWindow;

ClientPage *createRemoteMonitorPage(const QString &path,ApiClient *api,ClientWindow *window);
