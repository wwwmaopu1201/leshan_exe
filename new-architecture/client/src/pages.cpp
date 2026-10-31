#include "pages.h"
#include "client_window.h"
#include "overview_pages.h"
#include "management_pages.h"
#include "remote_monitor_page.h"
#include "pattern_pages.h"
#include "statistics_pages.h"
#include "profile_support_pages.h"

ClientPage *createClientPage(const QString &path,ApiClient *api,ClientWindow *window) {
    if(auto *overview=createOverviewPage(path,api,window)) return overview;
    if(auto *management=createManagementPage(path,api,window)) return management;
    if(auto *remote=createRemoteMonitorPage(path,api,window)) return remote;
    if(auto *pattern=createPatternPage(path,api,window)) return pattern;
    if(auto *statistics=createStatisticsPage(path,api,window)) return statistics;
    if(auto *profile=createProfileSupportPage(path,api,window)) return profile;
    return nullptr;
}
