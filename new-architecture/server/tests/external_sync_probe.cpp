// Test-only entry point for the same external sync worker used by the application.
#include "external_db.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <cstdio>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QFile input; if(!input.open(stdin,QIODevice::ReadOnly)) return 2;
    const QJsonDocument document=QJsonDocument::fromJson(input.readAll());
    if(!document.isObject()) return 2;
    const QJsonObject request=document.object(); QString error;
    const bool okay=request.value("mode").toString()=="test" ?
        ExternalDb::test(request.value("config").toObject(),&error) :
        ExternalDb::sync(request.value("source").toObject(),request.value("config").toObject(),request.value("full").toBool(),&error);
    const QByteArray output=QJsonDocument(QJsonObject{{"success",okay},{"error",error},
        {"drivers",QJsonArray::fromStringList(QSqlDatabase::drivers())}}).toJson(QJsonDocument::Compact);
    fwrite(output.constData(),1,size_t(output.size()),stdout); fputc('\n',stdout);
    return okay ? 0 : 1;
}
