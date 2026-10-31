#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <optional>

class Database;
class QWidget;

std::optional<QJsonObject> showEditorDialog(Database *db, const QString &table,
                                             const QJsonObject &current, bool creating,
                                             QWidget *parent);
std::optional<QJsonValue> showMoveGroupDialog(Database *db, int selectedCount, QWidget *parent);
