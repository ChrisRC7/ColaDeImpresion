#include "util/Xlsx.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cwchar>

#include "util/Strings.h"
#include "util/ZipWriter.h"

namespace cola::util::xlsx {

Cell Cell::Text(std::wstring text, CellStyle style) {
    Cell c;
    c.kind = Kind::Text;
    c.text = std::move(text);
    c.style = style;
    return c;
}

Cell Cell::Number(double value, CellStyle style) {
    Cell c;
    c.kind = Kind::Number;
    c.number = value;
    c.style = style;
    return c;
}

Cell Cell::Blank(CellStyle style) {
    Cell c;
    c.style = style;
    return c;
}

std::wstring ColumnName(int index) {
    std::wstring name;
    for (int n = index + 1; n > 0; n = (n - 1) / 26) name.insert(name.begin(), static_cast<wchar_t>(L'A' + (n - 1) % 26));
    return name;
}

namespace {
// Días desde 1/1/1970 de una fecha civil (algoritmo de Howard Hinnant).
long long DaysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}
}  // namespace

double SerialDate(const SYSTEMTIME& t) {
    const long long days = DaysFromCivil(t.wYear, t.wMonth, t.wDay) - DaysFromCivil(1899, 12, 30);
    const double seconds = t.wHour * 3600.0 + t.wMinute * 60.0 + t.wSecond;
    return static_cast<double>(days) + seconds / 86400.0;
}

std::wstring SafeSheetName(const std::wstring& name) {
    std::wstring safe;
    for (wchar_t c : name)
        if (c >= 0x20 && !wcschr(L"[]:*?/\\", c)) safe.push_back(c);
    while (!safe.empty() && safe.front() == L'\'') safe.erase(safe.begin());
    while (!safe.empty() && safe.back() == L'\'') safe.pop_back();
    if (safe.size() > 31) safe.resize(31);
    return safe.empty() ? L"Hoja" : safe;
}

namespace {
constexpr char kXmlHeader[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
constexpr char kMainNs[] = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
constexpr char kRelNs[] = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

// Texto UTF-8 escapado para XML, sin los caracteres de control que XML no admite.
std::string Escape(const std::wstring& text) {
    std::wstring limited = text.size() > 32767 ? text.substr(0, 32767) : text;  // límite de Excel por celda
    std::string out;
    for (char c : ToUtf8(limited)) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r') break;
                out.push_back(c);
        }
    }
    return out;
}

std::string NumberText(double value) {
    if (!std::isfinite(value)) return "0";
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);  // independiente del locale
    return std::string(buffer, result.ptr);
}

std::string CellRef(int column, size_t row) { return ToUtf8(ColumnName(column)) + std::to_string(row + 1); }

size_t ColumnCount(const Sheet& sheet) {
    size_t count = sheet.columnWidths.size();
    for (const auto& row : sheet.rows) count = std::max(count, row.size());
    return count;
}

std::string FilterRange(const Sheet& sheet) {
    const int lastColumn = static_cast<int>(std::max<size_t>(1, ColumnCount(sheet))) - 1;
    const size_t lastRow = std::max<size_t>(1, sheet.rows.size()) - 1;
    return "A1:" + CellRef(lastColumn, lastRow);
}

std::string SheetXml(const Sheet& sheet, bool selected) {
    std::string x = kXmlHeader;
    x += "<worksheet xmlns=\"";
    x += kMainNs;
    x += "\" xmlns:r=\"";
    x += kRelNs;
    x += "\">";
    x += "<sheetViews><sheetView workbookViewId=\"0\"";
    if (selected) x += " tabSelected=\"1\"";
    if (sheet.freezeFirstRow)
        x += "><pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/>"
             "<selection pane=\"bottomLeft\"/></sheetView>";
    else
        x += "/>";
    x += "</sheetViews><sheetFormatPr defaultRowHeight=\"15\"/>";
    bool anyWidth = false;
    for (double w : sheet.columnWidths) anyWidth = anyWidth || w > 0;
    if (anyWidth) {
        x += "<cols>";
        for (size_t i = 0; i < sheet.columnWidths.size(); ++i) {
            if (sheet.columnWidths[i] <= 0) continue;
            const std::string n = std::to_string(i + 1);
            x += "<col min=\"" + n + "\" max=\"" + n + "\" width=\"" + NumberText(sheet.columnWidths[i]) +
                 "\" customWidth=\"1\"/>";
        }
        x += "</cols>";
    }
    x += "<sheetData>";
    for (size_t r = 0; r < sheet.rows.size(); ++r) {
        x += "<row r=\"" + std::to_string(r + 1) + "\">";
        const auto& row = sheet.rows[r];
        for (size_t c = 0; c < row.size(); ++c) {
            const Cell& cell = row[c];
            const int style = static_cast<int>(cell.style);
            if (cell.kind == Cell::Kind::Empty && style == 0) continue;
            x += "<c r=\"" + CellRef(static_cast<int>(c), r) + "\"";
            if (style) x += " s=\"" + std::to_string(style) + "\"";
            if (cell.kind == Cell::Kind::Text) {
                x += " t=\"inlineStr\"><is><t xml:space=\"preserve\">" + Escape(cell.text) + "</t></is></c>";
            } else if (cell.kind == Cell::Kind::Number) {
                x += "><v>" + NumberText(cell.number) + "</v></c>";
            } else {
                x += "/>";
            }
        }
        x += "</row>";
    }
    x += "</sheetData>";
    if (sheet.autoFilter && !sheet.rows.empty()) x += "<autoFilter ref=\"" + FilterRange(sheet) + "\"/>";
    x += "</worksheet>";
    return x;
}

// El orden de cellXfs coincide con el enum CellStyle.
constexpr char kStyles[] =
    "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
    "<numFmts count=\"4\">"
    "<numFmt numFmtId=\"164\" formatCode=\"dd/mm/yyyy hh:mm\"/>"
    "<numFmt numFmtId=\"165\" formatCode=\"0.0%\"/>"
    "<numFmt numFmtId=\"166\" formatCode=\"#,##0.0\"/>"
    "<numFmt numFmtId=\"167\" formatCode=\"dd/mm/yyyy\"/>"
    "</numFmts>"
    "<fonts count=\"4\">"
    "<font><sz val=\"11\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
    "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
    "<font><b/><sz val=\"14\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
    "<font><i/><sz val=\"10\"/><color rgb=\"FF6B7280\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
    "</fonts>"
    "<fills count=\"3\">"
    "<fill><patternFill patternType=\"none\"/></fill>"
    "<fill><patternFill patternType=\"gray125\"/></fill>"
    "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFDDE5F3\"/><bgColor indexed=\"64\"/></patternFill></fill>"
    "</fills>"
    "<borders count=\"2\">"
    "<border><left/><right/><top/><bottom/><diagonal/></border>"
    "<border><left/><right/><top/><bottom style=\"thin\"><color rgb=\"FF8EA2C6\"/></bottom><diagonal/></border>"
    "</borders>"
    "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
    "<cellXfs count=\"10\">"
    "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
    "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" "
    "applyBorder=\"1\"/>"
    "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
    "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
    "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
    "<xf numFmtId=\"3\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
    "<xf numFmtId=\"165\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
    "<xf numFmtId=\"166\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
    "<xf numFmtId=\"167\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
    "<xf numFmtId=\"0\" fontId=\"3\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
    "</cellXfs>"
    "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
    "</styleSheet>";
}  // namespace

std::string BuildWorkbook(const std::vector<Sheet>& sheets) {
    ZipWriter zip;
    const size_t count = sheets.size();

    std::string types = kXmlHeader;
    types +=
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
    for (size_t i = 0; i < count; ++i)
        types += "<Override PartName=\"/xl/worksheets/sheet" + std::to_string(i + 1) +
                 ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
    types += "</Types>";
    zip.Add("[Content_Types].xml", std::move(types));

    zip.Add("_rels/.rels", std::string(kXmlHeader) +
                               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                               "<Relationship Id=\"rId1\" "
                               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
                               "officeDocument\" Target=\"xl/workbook.xml\"/>"
                               "</Relationships>");

    std::string workbook = kXmlHeader;
    workbook += "<workbook xmlns=\"";
    workbook += kMainNs;
    workbook += "\" xmlns:r=\"";
    workbook += kRelNs;
    workbook += "\"><bookViews><workbookView/></bookViews><sheets>";
    std::string names;
    for (size_t i = 0; i < count; ++i) {
        const std::wstring name = SafeSheetName(sheets[i].name);
        workbook += "<sheet name=\"" + Escape(name) + "\" sheetId=\"" + std::to_string(i + 1) + "\" r:id=\"rId" +
                    std::to_string(i + 1) + "\"/>";
        if (sheets[i].autoFilter && !sheets[i].rows.empty()) {
            // Excel espera el nombre oculto del rango filtrado; apóstrofos duplicados.
            std::wstring quoted;
            for (wchar_t c : name) {
                quoted.push_back(c);
                if (c == L'\'') quoted.push_back(c);
            }
            std::string range = FilterRange(sheets[i]);
            const auto colon = range.find(':');
            std::string from = range.substr(0, colon), to = range.substr(colon + 1);
            auto absolute = [](const std::string& ref) {
                size_t digit = ref.find_first_of("0123456789");
                return "$" + ref.substr(0, digit) + "$" + ref.substr(digit);
            };
            names += "<definedName name=\"_xlnm._FilterDatabase\" localSheetId=\"" + std::to_string(i) +
                     "\" hidden=\"1\">'" + Escape(quoted) + "'!" + absolute(from) + ":" + absolute(to) + "</definedName>";
        }
    }
    workbook += "</sheets>";
    if (!names.empty()) workbook += "<definedNames>" + names + "</definedNames>";
    workbook += "</workbook>";
    zip.Add("xl/workbook.xml", std::move(workbook));

    std::string rels = kXmlHeader;
    rels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    for (size_t i = 0; i < count; ++i)
        rels += "<Relationship Id=\"rId" + std::to_string(i + 1) +
                "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
                "Target=\"worksheets/sheet" +
                std::to_string(i + 1) + ".xml\"/>";
    rels += "<Relationship Id=\"rId" + std::to_string(count + 1) +
            "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
            "Target=\"styles.xml\"/></Relationships>";
    zip.Add("xl/_rels/workbook.xml.rels", std::move(rels));

    zip.Add("xl/styles.xml", std::string(kXmlHeader) + kStyles);
    for (size_t i = 0; i < count; ++i)
        zip.Add("xl/worksheets/sheet" + std::to_string(i + 1) + ".xml", SheetXml(sheets[i], i == 0));
    return zip.Finish();
}

bool SaveWorkbook(const std::wstring& path, const std::vector<Sheet>& sheets) {
    const std::string bytes = BuildWorkbook(sheets);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    const DWORD error = GetLastError();
    CloseHandle(file);
    if (!ok || written != bytes.size()) {
        SetLastError(ok ? ERROR_WRITE_FAULT : error);
        return false;
    }
    return true;
}

}  // namespace cola::util::xlsx
