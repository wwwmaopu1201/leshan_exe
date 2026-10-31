#pragma once
#include <QSettings>
#include <QDir>

// Explicitly honor IniFormat/setPath used by regression fixtures. The named
// two-argument QSettings constructor otherwise keeps using native preferences.
inline QSettings uiSettings(const QString &application) {
    if(QSettings::defaultFormat()==QSettings::IniFormat)
        return QSettings(QSettings::IniFormat,QSettings::UserScope,"Boer",application);
    const QString isolated=qEnvironmentVariable("BOER_TEST_SETTINGS_DIR");
    if(!isolated.isEmpty())return QSettings(QDir(isolated).filePath(application+".ini"),QSettings::IniFormat);
    return QSettings("Boer",application);
}
