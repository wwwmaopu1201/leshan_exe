#pragma once

#include <QString>

// Converts the server's UTF-8 CSV export into a native, single-sheet XLSX file.
bool csvToXlsx(const QString &csvPath,const QString &xlsxPath,QString *error=nullptr);
