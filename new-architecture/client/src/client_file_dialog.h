#pragma once

#include <QString>
#include <QStringList>

class QWidget;

namespace ClientFileDialog {
QString getOpenFileName(QWidget *parent,const QString &title,const QString &initial={},
                        const QString &filter={});
QStringList getOpenFileNames(QWidget *parent,const QString &title,const QString &initial={},
                             const QString &filter={});
QString getSaveFileName(QWidget *parent,const QString &title,const QString &suggested={},
                        const QString &filter={});
}
