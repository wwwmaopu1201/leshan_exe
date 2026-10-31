#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QtMath>
inline QJsonObject uploadTaskSummary(const QJsonArray &tasks) {
    int completed=0,failed=0,canceled=0,totalProgress=0;
    for(const auto &value:tasks){const auto task=value.toObject();const auto status=task.value("status").toString();completed+=status=="completed";failed+=status=="failed";canceled+=status=="canceled";totalProgress+=QStringList{"completed","failed","canceled"}.contains(status) ? 100 : qBound(0,task.value("progress").toInt(),100);}
    return {{"total",tasks.size()},{"completed",completed},{"failed",failed},{"canceled",canceled},{"overallProgress",tasks.isEmpty() ? 0 : qRound(double(totalProgress)/tasks.size())}};
}
