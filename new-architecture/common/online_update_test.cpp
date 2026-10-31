#include "online_update.h"
#include <QApplication>
#include <QJsonObject>
#include <cstdio>
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    int failures=0;
    auto check=[&](bool ok,const char *name) { if(!ok) { std::fprintf(stderr,"FAIL: %s\n",name); ++failures; } };
    check(OnlineUpdate::isNewer("1.10.0","1.9.9"),"numeric version comparison");
    check(!OnlineUpdate::isNewer("1.0.0","1.0.0"),"equal version");
    check(!OnlineUpdate::isNewer("0.9.0","1.0.0"),"no downgrade");
    check(!OnlineUpdate::isNewer("1.2bad","1.0"),"reject malformed version");
    QJsonObject release{{"version","1.1.0"},{"notes","修复问题\n增加功能"},{"url","https://updates.example.com/client.exe"},{"sha256",QString(64,'a')}};
    check(OnlineUpdate::validRelease(release),"valid release");
    check(OnlineUpdate::defaultManifestUrl("client").toString()=="http://47.92.226.92:99/update/client.json","client default address");
    check(OnlineUpdate::defaultManifestUrl("server").toString()=="http://47.92.226.92:99/update/server.json","server default address");
    release["url"]="http://47.92.226.92:99/update/client-0.2.0-setup.exe";
    check(OnlineUpdate::validRelease(release),"allow configured HTTP update directory");
    release["url"]="http://47.92.226.92:99/update/../other/client.exe";
    check(!OnlineUpdate::validRelease(release),"reject paths outside HTTP update directory");
    release["url"]="http://47.92.226.92:98/update/client.exe";
    check(!OnlineUpdate::validRelease(release),"reject different HTTP port");
    release["url"]="http://updates.example.com/client.exe";
    check(!OnlineUpdate::validRelease(release),"reject insecure executable download");
    release["url"]="https://updates.example.com/client.exe"; release["sha256"]="abc";
    check(!OnlineUpdate::validRelease(release),"require checksum");
    release["sha256"]=QString(64,'a'); release["notes"]=" ";
    check(!OnlineUpdate::validRelease(release),"require release notes");
    return failures ? 1 : 0;
}
