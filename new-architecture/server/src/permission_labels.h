#pragma once
#include <QJsonObject>
#include <QStringList>
#include <QHash>

// Match the client's legacy parent-only permissions and explicit child permissions.
inline QStringList permissionLabels(QJsonObject permissions) {
    if(permissions.isEmpty())for(const QString &key:{"home","dashboard","deviceManagement","fileManagement","statistics","employeeManagement","remoteMonitoring"})permissions[key]=true;
    const QHash<QString,QStringList> children={{"deviceManagement",{"deviceInfo","remoteMonitoring"}},{"fileManagement",{"patternFiles","devicePatternFiles","downloadLog"}},{"statistics",{"salaryStatistics","statusStatistics"}}};
    for(auto it=children.cbegin();it!=children.cend();++it) {
        bool detailed=false;
        for(const QString &child:it.value())if(permissions.contains(child)) {
            if(it.key()=="deviceManagement" && child=="remoteMonitoring" && !permissions.contains("deviceInfo") && permissions.value(it.key()).toBool())continue;
            detailed=true;
        }
        bool any=false;
        for(const QString &child:it.value()){permissions[child]=detailed ? permissions.value(child).toBool() : permissions.value(it.key()).toBool();any|=permissions.value(child).toBool();}
        permissions[it.key()]=any;
    }
    const QList<QPair<QString,QString>> labels={{"home","首页"},{"dashboard","数据看板"},{"deviceManagement","设备管理"},{"deviceInfo","设备信息"},{"remoteMonitoring","远程监控"},{"fileManagement","文件管理"},{"patternFiles","花型文件"},{"devicePatternFiles","设备花型文件"},{"downloadLog","下发日志"},{"statistics","数据统计"},{"salaryStatistics","工资统计"},{"statusStatistics","状态统计"},{"employeeManagement","员工管理"}};
    QStringList result;for(const auto &entry:labels)if(permissions.value(entry.first).toBool())result<<entry.second;return result;
}
