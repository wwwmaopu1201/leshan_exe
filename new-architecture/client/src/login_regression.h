#pragma once
class QApplication;
#include <QString>
int runLoginRegression(QApplication &app);
int runChartRegression(QApplication &app);
int runCatalogRegression(QApplication &app,const QString &host,int port);
int runPatternRegression(QApplication &app,const QString &host,int port);
int runStatisticsRegression(QApplication &app,const QString &host,int port);
