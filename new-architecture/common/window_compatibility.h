#pragma once
#include <QMainWindow>
#include <QStringList>
#include <functional>

// Used only by the explicit window compatibility regression entry points.
int runWindowCompatibility(QMainWindow &window,const QString &side,const QStringList &pages,
                           const std::function<void(const QString&)> &navigate,
                           const std::function<void()> &dialogs);
void recordWindowCompatibility(const QString &side,const QString &name,bool passed,const QString &detail={});
void scheduleWindowDialogCheck(QMainWindow &owner,const QString &side,const QString &label);
void waitWindowCompatibilityLayout(int milliseconds=80);
void checkWindowNotification(QMainWindow &owner,const QString &side,const QString &objectName);
