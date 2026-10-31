#pragma once
#include <QMainWindow>
#include <QString>
#include <functional>

void recordInteraction(const QString &side,const QString &name,bool passed,const QString &detail={});
int runInteractionCompatibility(QMainWindow &window,const QString &side,
    const std::function<void(const QString&,bool)> &notify,const std::function<void()> &specific);
void sendInteractionKey(QWidget *widget,int key,Qt::KeyboardModifiers modifiers=Qt::NoModifier);
void waitInteraction(int milliseconds=60);
