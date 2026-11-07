// Implementación del escritor .xlsx (ver Xlsx.h).
//
// Un .xlsx es un ZIP con estas partes (todas generadas aquí):
//   [Content_Types].xml        - tipo MIME de cada parte.
//   _rels/.rels                - apunta al libro (xl/workbook.xml).
//   xl/workbook.xml            - lista de hojas y nombres definidos.
//   xl/_rels/workbook.xml.rels - relaciona cada hoja y los estilos con su archivo.
//   xl/styles.xml              - formatos numéricos, fuentes, rellenos y bordes.
//   xl/worksheets/sheetN.xml   - datos de cada hoja (N desde 1).
// El XML se arma concatenando cadenas: los únicos valores variables (texto de
// celdas y nombres de hojas) pasan por Escape().

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
    // Se trabaja con n = índice + 1 (base 1) y se resta 1 en cada dígito
    // porque las letras van de 1 (A) a 26 (Z), sin cero. Los dígitos salen
    // del menos significativo al más, por eso se insertan al principio.
    std::wstring name;
    for (int n = index + 1; n > 0; n = (n - 1) / 26) name.insert(name.begin(), static_cast<wchar_t>(L'A' + (n - 1) % 26));
    return name;
}

namespace {
// Días desde 1/1/1970 de una fecha civil (algoritmo de Howard Hinnant).
// Cuenta en "eras" de 400 años (146 097 días) con el año empezando en marzo,
// así el 29 de febrero queda al final y no hay casos especiales. Válido para
// cualquier fecha del calendario gregoriano proléptico.
long long DaysFromCivil(int y, unsigned m, unsigned d) {
    // Enero y febrero cuentan como meses 11 y 12 del año anterior.
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}
}  // namespace

double SerialDate(const SYSTEMTIME& t) {
    // Días enteros desde el origen de Excel + fracción del día por la hora.
    // Los milisegundos se ignoran (Excel muestra hasta minutos).
    const long long days = DaysFromCivil(t.wYear, t.wMonth, t.wDay) - DaysFromCivil(1899, 12, 30);
    const double seconds = t.wHour * 3600.0 + t.wMinute * 60.0 + t.wSecond;
    return static_cast<double>(days) + seconds / 86400.0;
}

std::wstring SafeSheetName(const std::wstring& name) {
    // Se descartan los caracteres de control y los que Excel prohíbe en
    // nombres de hoja.
    std::wstring safe;
    for (wchar_t c : name)
        if (c >= 0x20 && !wcschr(L"[]:*?/\\", c)) safe.push_back(c);
    // Excel rechaza nombres que empiezan o terminan con apóstrofo.
    while (!safe.empty() && safe.front() == L'\'') safe.erase(safe.begin());
    while (!safe.empty() && safe.back() == L'\'') safe.pop_back();
    if (safe.size() > 31) safe.resize(31);
    return safe.empty() ? L"Hoja" : safe;
}

namespace {
// Declaración XML con que empieza cada parte. standalone="yes" como Excel.
constexpr char kXmlHeader[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
// Espacio de nombres de SpreadsheetML (libro y hojas).
constexpr char kMainNs[] = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
// Espacio de nombres de las relaciones (atributo r:id del libro).
constexpr char kRelNs[] = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

// Texto UTF-8 escapado para XML, sin los caracteres de control que XML no admite.
// Se recorre byte a byte el UTF-8: los caracteres especiales de XML son ASCII,
// y los bytes de una secuencia multibyte son >= 0x80, así que nunca se
// confunden con ellos. El apóstrofo no se escapa porque los atributos se
// escriben con comillas dobles.
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
                // XML 1.0 solo admite tab, LF y CR entre los controles; los
                // demás harían que Excel diga que el archivo está dañado.
                if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r') break;
                out.push_back(c);
        }
    }
    return out;
}

// Número en texto para <v>: siempre con punto decimal (Excel lo exige así
// en el XML, sea cual sea el idioma) y la representación más corta que
// conserva el valor exacto. NaN/infinito no son válidos y se escriben como 0.
std::string NumberText(double value) {
    if (!std::isfinite(value)) return "0";
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);  // independiente del locale
    return std::string(buffer, result.ptr);
}

// Referencia de celda estilo A1 (columna y fila desde 0).
std::string CellRef(int column, size_t row) { return ToUtf8(ColumnName(column)) + std::to_string(row + 1); }

// Columnas usadas por la hoja: la fila más larga o la cantidad de anchos dados.
size_t ColumnCount(const Sheet& sheet) {
    size_t count = sheet.columnWidths.size();
    for (const auto& row : sheet.rows) count = std::max(count, row.size());
    return count;
}

// Rango del autofiltro: desde A1 hasta la última columna y fila usadas (al
// menos A1:A1 para una hoja vacía).
std::string FilterRange(const Sheet& sheet) {
    const int lastColumn = static_cast<int>(std::max<size_t>(1, ColumnCount(sheet))) - 1;
    const size_t lastRow = std::max<size_t>(1, sheet.rows.size()) - 1;
    return "A1:" + CellRef(lastColumn, lastRow);
}

// XML de una hoja (xl/worksheets/sheetN.xml). `selected` marca la pestaña
// activa al abrir. El orden de los elementos lo impone el esquema de
// SpreadsheetML (sheetViews, sheetFormatPr, cols, sheetData, autoFilter): si
// se altera, Excel ofrece "reparar" el libro.
std::string SheetXml(const Sheet& sheet, bool selected) {
    std::string x = kXmlHeader;
    x += "<worksheet xmlns=\"";
    x += kMainNs;
    x += "\" xmlns:r=\"";
    x += kRelNs;
    x += "\">";
    x += "<sheetViews><sheetView workbookViewId=\"0\"";
    if (selected) x += " tabSelected=\"1\"";
    // Filas fijas: un panel dividido en ySplit filas, en estado "frozen"; la
    // parte desplazable (bottomLeft) empieza en la fila siguiente.
    if (sheet.frozenRows > 0)
        x += "><pane ySplit=\"" + std::to_string(sheet.frozenRows) + "\" topLeftCell=\"A" +
             std::to_string(sheet.frozenRows + 1) +
             "\" activePane=\"bottomLeft\" state=\"frozen\"/><selection pane=\"bottomLeft\"/></sheetView>";
    else
        x += "/>";
    x += "</sheetViews><sheetFormatPr defaultRowHeight=\"15\"/>";
    // <cols> solo si alguna columna tiene ancho propio (un <cols> vacío no es
    // válido). Una entrada <col> por columna con ancho.
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
            // Celda vacía y sin estilo: no se escribe (Excel no la necesita).
            if (cell.kind == Cell::Kind::Empty && style == 0) continue;
            x += "<c r=\"" + CellRef(static_cast<int>(c), r) + "\"";
            // s = índice en cellXfs; 0 (Normal) es el predeterminado y se omite.
            if (style) x += " s=\"" + std::to_string(style) + "\"";
            if (cell.kind == Cell::Kind::Text) {
                // Texto en línea; xml:space="preserve" conserva espacios
                // iniciales/finales y saltos de línea.
                x += " t=\"inlineStr\"><is><t xml:space=\"preserve\">" + Escape(cell.text) + "</t></is></c>";
            } else if (cell.kind == Cell::Kind::Number) {
                x += "><v>" + NumberText(cell.number) + "</v></c>";
            } else {
                // Vacía con estilo (p. ej. relleno del encabezado).
                x += "/>";
            }
        }
        x += "</row>";
    }
    x += "</sheetData>";
    // El autofiltro va sobre todo el rango usado; su primera fila hace de
    // encabezado. Además requiere el nombre oculto _FilterDatabase en
    // workbook.xml (ver BuildWorkbook).
    if (sheet.autoFilter && !sheet.rows.empty()) x += "<autoFilter ref=\"" + FilterRange(sheet) + "\"/>";
    x += "</worksheet>";
    return x;
}

// El orden de cellXfs coincide con el enum CellStyle.
// Contenido de xl/styles.xml (fijo):
//   numFmts: formatos propios desde el id 164 (los 0-163 son de Excel):
//            164 fecha y hora, 165 porcentaje con 1 decimal, 166 número con
//            1 decimal y miles, 167 fecha. El 3 ("#,##0") es predefinido.
//   fonts:   0 normal, 1 negrita, 2 título 14 pt, 3 cursiva gris 10 pt.
//   fills:   0 y 1 son obligatorios (none y gray125, Excel los espera así);
//            2 es el azul claro de los encabezados.
//   borders: 0 sin borde, 1 línea fina inferior (encabezados).
//   cellXfs: una combinación por valor de CellStyle, en el mismo orden.
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

    // [Content_Types].xml: tipos por extensión (rels, xml) y uno explícito
    // para el libro, los estilos y cada hoja. Va primero en el ZIP.
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

    // Relación raíz del paquete: el documento principal es el libro.
    zip.Add("_rels/.rels", std::string(kXmlHeader) +
                               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                               "<Relationship Id=\"rId1\" "
                               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
                               "officeDocument\" Target=\"xl/workbook.xml\"/>"
                               "</Relationships>");

    // xl/workbook.xml: una <sheet> por hoja (sheetId y r:id = posición + 1,
    // que coinciden con las relaciones de abajo) y, aparte, los nombres
    // definidos que hacen falta para los autofiltros.
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
            // Forma: 'Nombre de hoja'!$A$1:$K$120, con localSheetId = índice
            // de la hoja desde 0. Dentro de las comillas simples un apóstrofo
            // se escribe doble.
            std::wstring quoted;
            for (wchar_t c : name) {
                quoted.push_back(c);
                if (c == L'\'') quoted.push_back(c);
            }
            std::string range = FilterRange(sheets[i]);
            const auto colon = range.find(':');
            std::string from = range.substr(0, colon), to = range.substr(colon + 1);
            // "K120" -> "$K$120": se inserta "$" antes de la columna y antes
            // del primer dígito (referencia absoluta).
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

    // xl/_rels/workbook.xml.rels: rId1..rIdN son las hojas y rId(N+1) los
    // estilos (el id solo tiene que ser único dentro de este archivo).
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
    // Las hojas al final; la primera queda como pestaña seleccionada.
    for (size_t i = 0; i < count; ++i)
        zip.Add("xl/worksheets/sheet" + std::to_string(i + 1) + ".xml", SheetXml(sheets[i], i == 0));
    return zip.Finish();
}

bool SaveWorkbook(const std::wstring& path, const std::vector<Sheet>& sheets) {
    // Primero se arma todo en memoria, así un error al generar no deja un
    // archivo a medias en disco.
    const std::string bytes = BuildWorkbook(sheets);
    // Sin compartir (0) y CREATE_ALWAYS (reemplaza). Falla si Excel lo tiene
    // abierto; GetLastError queda para que el llamador lo explique.
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    // Se guarda el error antes de CloseHandle, que podría sobrescribirlo.
    const DWORD error = GetLastError();
    CloseHandle(file);
    // Una escritura incompleta sin error (disco lleno en red, etc.) se
    // informa como ERROR_WRITE_FAULT.
    if (!ok || written != bytes.size()) {
        SetLastError(ok ? ERROR_WRITE_FAULT : error);
        return false;
    }
    return true;
}

}  // namespace cola::util::xlsx
