#pragma once
#include <QDir>
#include <QRegularExpression>
#include <QString>

inline QString sharedFolderError(const QString &input) {
    const QString value=input.trimmed();
    if(value.isEmpty()) return {};
    if(value.contains(QRegularExpression("[<>\"|?*\\x00-\\x1f]"))) return "路径包含非法字符";
    if(!QDir::isAbsolutePath(value)) return "请输入绝对路径";
#ifdef Q_OS_WIN
    if(value.size()>259) return "路径过长";
    const QString tail=value.mid(value.size()>1 && value[1]==':' ? 2 : 0);
    if(tail.contains(':')) return "路径包含非法字符";
#else
    if(value.toUtf8().size()>1023) return "路径过长";
#endif
    for(const auto &part:value.split(QRegularExpression("[/\\\\]"))) if(part.toUtf8().size()>255) return "路径过长";
    return {};
}
