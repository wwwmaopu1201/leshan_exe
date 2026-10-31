#include "excel_export.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QVector>
#include <QtGlobal>
#include <cstdint>

namespace {
void number16(QByteArray &out,quint16 value) {
    out.append(char(value&0xff)); out.append(char((value>>8)&0xff));
}
void number32(QByteArray &out,quint32 value) {
    number16(out,quint16(value&0xffff)); number16(out,quint16(value>>16));
}
quint32 crc32(const QByteArray &bytes) {
    quint32 crc=0xffffffffu;
    for(unsigned char byte:bytes) {
        crc^=byte;
        for(int bit=0;bit<8;++bit) crc=(crc>>1) ^ (0xedb88320u & quint32(-(int(crc&1))));
    }
    return ~crc;
}
QString xmlEscape(const QString &source) {
    QString result; result.reserve(source.size());
    for(QChar character:source) {
        const ushort code=character.unicode();
        if(code<32 && code!='\t' && code!='\n' && code!='\r') continue;
        if(character=='&') result+="&amp;";
        else if(character=='<') result+="&lt;";
        else if(character=='>') result+="&gt;";
        else if(character=='"') result+="&quot;";
        else if(character=='\'') result+="&apos;";
        else result+=character;
    }
    return result;
}
QString columnName(int index) {
    QString result;
    for(++index;index>0;index=(index-1)/26) result.prepend(QChar('A'+(index-1)%26));
    return result;
}
bool parseCsv(const QByteArray &bytes,QVector<QStringList> *rows,QString *error) {
    QString text=QString::fromUtf8(bytes);
    if(text.startsWith(QChar(0xfeff))) text.remove(0,1);
    QStringList row; QString field;
    bool quoted=false;
    for(int i=0;i<text.size();++i) {
        const QChar ch=text[i];
        if(ch=='"') {
            if(quoted && i+1<text.size() && text[i+1]=='"') { field+='"'; ++i; }
            else quoted=!quoted;
        } else if(ch==',' && !quoted) {
            row << field; field.clear();
        } else if((ch=='\r' || ch=='\n') && !quoted) {
            if(ch=='\r' && i+1<text.size() && text[i+1]=='\n') ++i;
            row << field; field.clear();
            rows->append(row); row.clear();
        } else field+=ch;
    }
    if(quoted) { if(error) *error="导出数据的 CSV 格式不完整"; return false; }
    if(!row.isEmpty() || !field.isEmpty()) { row << field; rows->append(row); }
    if(rows->isEmpty()) { if(error) *error="导出数据为空"; return false; }
    return true;
}
bool numericColumn(const QString &header) {
    for(const QString &word:{"件数","针数","工价","工资","金额","合计","时长","速度","用量","效率","比例","次数","单价"})
        if(header.contains(word)) return true;
    return false;
}
QByteArray sheetXml(const QVector<QStringList> &rows) {
    const QStringList headers=rows.first();
    int count=headers.size(); for(const auto &row:rows) count=qMax(count,int(row.size()));
    QVector<int> widths(count,10);
    for(const auto &row:rows) for(int col=0;col<row.size();++col)
        widths[col]=qMax(widths[col],qMin(42,int(row[col].size()*1.5)+2));
    QByteArray xml=R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)";
    xml+="<dimension ref=\"A1:"+columnName(qMax(0,count-1)).toUtf8()+QByteArray::number(rows.size())+"\"/>";
    xml+="<cols>";
    for(int col=0;col<count;++col) xml+="<col min=\""+QByteArray::number(col+1)+"\" max=\""+
        QByteArray::number(col+1)+"\" width=\""+QByteArray::number(widths[col])+"\" customWidth=\"1\"/>";
    xml+="</cols><sheetData>";
    static const QRegularExpression numberPattern(R"(^-?(?:0|[1-9]\d*)(?:\.\d+)?$)");
    for(int row=0;row<rows.size();++row) {
        xml+="<row r=\""+QByteArray::number(row+1)+"\">";
        for(int col=0;col<rows[row].size();++col) {
            const QString value=rows[row][col];
            const QByteArray address=columnName(col).toUtf8()+QByteArray::number(row+1);
            const bool number=row>0 && col<headers.size() && numericColumn(headers[col]) &&
                numberPattern.match(value.trimmed()).hasMatch();
            if(number) xml+="<c r=\""+address+"\"><v>"+value.trimmed().toUtf8()+"</v></c>";
            else xml+="<c r=\""+address+"\""+(row==0 ? QByteArray(" s=\"1\"") : QByteArray())+
                " t=\"inlineStr\"><is><t xml:space=\"preserve\">"+
                xmlEscape(value).toUtf8()+"</t></is></c>";
        }
        xml+="</row>";
    }
    xml+="</sheetData></worksheet>";
    return xml;
}
struct ZipEntry { QByteArray name,data; quint32 crc=0,offset=0; };
bool writeZip(const QString &path,QVector<ZipEntry> entries,QString *error) {
    QByteArray archive,central;
    for(auto &entry:entries) {
        if(quint64(archive.size())+quint64(entry.data.size())>0xffffffffu) {
            if(error) *error="导出文件过大"; return false;
        }
        entry.offset=quint32(archive.size()); entry.crc=crc32(entry.data);
        number32(archive,0x04034b50); number16(archive,20); number16(archive,0x0800);
        number16(archive,0); number16(archive,0); number16(archive,0);
        number32(archive,entry.crc); number32(archive,quint32(entry.data.size()));
        number32(archive,quint32(entry.data.size())); number16(archive,quint16(entry.name.size()));
        number16(archive,0); archive+=entry.name; archive+=entry.data;
        number32(central,0x02014b50); number16(central,20); number16(central,20);
        number16(central,0x0800); number16(central,0); number16(central,0); number16(central,0);
        number32(central,entry.crc); number32(central,quint32(entry.data.size()));
        number32(central,quint32(entry.data.size())); number16(central,quint16(entry.name.size()));
        number16(central,0); number16(central,0); number16(central,0); number16(central,0);
        number32(central,0); number32(central,entry.offset); central+=entry.name;
    }
    const quint32 centralOffset=quint32(archive.size()); archive+=central;
    number32(archive,0x06054b50); number16(archive,0); number16(archive,0);
    number16(archive,quint16(entries.size())); number16(archive,quint16(entries.size()));
    number32(archive,quint32(central.size())); number32(archive,centralOffset); number16(archive,0);
    QSaveFile output(path);
    if(!output.open(QIODevice::WriteOnly) || output.write(archive)!=archive.size() || !output.commit()) {
        if(error) *error="无法保存 Excel 文件"; return false;
    }
    return true;
}
}

bool csvToXlsx(const QString &csvPath,const QString &xlsxPath,QString *error) {
    QFile source(csvPath);
    if(!source.open(QIODevice::ReadOnly)) { if(error) *error="无法读取导出数据"; return false; }
    QVector<QStringList> rows;
    if(!parseCsv(source.readAll(),&rows,error)) return false;
    const QByteArray contentTypes=R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/></Types>)";
    const QByteArray rootRels=R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>)";
    const QByteArray workbook=R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Statistics" sheetId="1" r:id="rId1"/></sheets></workbook>)";
    const QByteArray workbookRels=R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>)";
    const QByteArray styles=R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><fonts count="2"><font><sz val="11"/><name val="Arial"/></font><font><b/><sz val="11"/><name val="Arial"/></font></fonts><fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills><borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders><cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs><cellXfs count="2"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/><xf numFmtId="0" fontId="1" fillId="0" borderId="0" applyFont="1"/></cellXfs><cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles></styleSheet>)";
    return writeZip(xlsxPath,{{"[Content_Types].xml",contentTypes},{"_rels/.rels",rootRels},
        {"xl/workbook.xml",workbook},{"xl/_rels/workbook.xml.rels",workbookRels},
        {"xl/styles.xml",styles},{"xl/worksheets/sheet1.xml",sheetXml(rows)}},error);
}
