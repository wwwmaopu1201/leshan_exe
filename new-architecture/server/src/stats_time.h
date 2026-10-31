#pragma once

#include <QDateTime>
#include <QString>
#include <QVariant>

inline QString sqlText(const QVariant &value) {
    if(value.metaType().id()==QMetaType::QDateTime)
        return value.toDateTime().toString("yyyy-MM-dd HH:mm:ss");
    if(value.metaType().id()==QMetaType::QDate)
        return value.toDate().toString("yyyy-MM-dd");
    return value.toString();
}

inline QPair<QString,QString> productionDisplayRange(const QString &recordDate,
    const QString &createdAt,const QString &startTime,const QString &endTime,double hours) {
    const QString format="yyyy-MM-dd HH:mm:ss";
    QDateTime base=QDateTime::fromString(createdAt,format);
    if(!base.isValid()) {
        QDate day=QDate::fromString(recordDate.left(10),"yyyy-MM-dd");
        if(!day.isValid()) day=QDate::currentDate();
        base=QDateTime(day,QTime(8,0));
    }
    QDateTime start=QDateTime::fromString(startTime,format);
    QDateTime end=QDateTime::fromString(endTime,format);
    if(!start.isValid()) start=base;
    if(!end.isValid()) end=base.addMSecs(qRound64(qMax(0.0,hours)*3600000.0));
    return {start.toString(format),end.toString(format)};
}
