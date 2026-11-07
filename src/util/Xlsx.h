#pragma once
// Escritor mínimo de libros de Excel (.xlsx, Office Open XML): hojas con texto,
// números y fechas, un juego fijo de estilos, anchos de columna, filas de
// títulos fijas y autofiltro. Sin dependencias (el ZIP lo arma ZipWriter).
// Lo usa app/HistoryExport para exportar el historial (decisión D43).
//
// El texto se escribe "en línea" (inlineStr) en cada celda en vez de usar la
// tabla de cadenas compartidas: el archivo queda algo más grande pero el
// escritor es mucho más simple, y Excel lo abre sin reparaciones.

#include <windows.h>

#include <string>
#include <vector>

namespace cola::util::xlsx {

// Estilos disponibles (índices de cellXfs en styles.xml).
// El valor numérico de cada uno ES su índice en kStyles (Xlsx.cpp): si se
// agrega uno, hay que agregar el <xf> correspondiente en la misma posición.
//   Normal   - sin formato.
//   Header   - encabezado de tabla: negrita, fondo azul claro y borde inferior.
//   DateTime - número de serie mostrado como "dd/mm/yyyy hh:mm".
//   Title    - título de hoja: negrita 14 pt.
//   Bold     - negrita.
//   Integer  - entero con separador de miles ("#,##0").
//   Percent  - porcentaje con un decimal (el valor va de 0 a 1).
//   Decimal  - número con separador de miles y un decimal.
//   Date     - número de serie mostrado como "dd/mm/yyyy".
//   Muted    - nota en cursiva gris, 10 pt (p. ej. "sin datos").
enum class CellStyle { Normal, Header, DateTime, Title, Bold, Integer, Percent, Decimal, Date, Muted };

// Una celda: vacía, texto o número, con un estilo. Las fechas son números
// (ver SerialDate) con estilo DateTime o Date.
struct Cell {
    enum class Kind { Empty, Text, Number };
    Kind kind = Kind::Empty;
    std::wstring text;    // solo si kind == Text
    double number = 0;    // solo si kind == Number
    CellStyle style = CellStyle::Normal;

    // Constructores con nombre, para armar filas de forma legible:
    // {Cell::Text(L"Total", CellStyle::Bold), Cell::Number(42, CellStyle::Integer)}.
    static Cell Text(std::wstring text, CellStyle style = CellStyle::Normal);
    static Cell Number(double value, CellStyle style = CellStyle::Normal);
    // Celda sin valor; con un estilo distinto de Normal se escribe igual (p.
    // ej. para que el fondo de un encabezado sea continuo).
    static Cell Blank(CellStyle style = CellStyle::Normal);
};

// Una fila: celdas desde la columna A, en orden.
using Row = std::vector<Cell>;

// Una hoja del libro.
struct Sheet {
    std::wstring name;                 // máx. 31 caracteres; se limpian los no permitidos
    std::vector<double> columnWidths;  // en caracteres; 0 = predeterminado
    std::vector<Row> rows;             // filas desde la 1; pueden tener distinta cantidad de celdas
    int frozenRows = 0;  // filas de arriba que quedan fijas al desplazarse
    bool autoFilter = false;  // sobre la primera fila y todas las columnas usadas
};

// "A", "B", ..., "Z", "AA"... (index desde 0).
// Numeración biyectiva en base 26 (no hay "cero"): 25 -> "Z", 26 -> "AA".
std::wstring ColumnName(int index);

// Número de serie de Excel (días desde 30/12/1899) de una fecha y hora local.
// La parte entera son los días y la fraccionaria la hora (0,5 = mediodía).
// El origen 30/12/1899 compensa el 29/02/1900 inexistente que Excel cuenta
// por compatibilidad con Lotus 1-2-3; para fechas actuales da el valor exacto.
// Excel no conoce zonas horarias: se pasa ya la hora local.
double SerialDate(const SYSTEMTIME& local);

// Nombre de hoja válido para Excel (sin []:*?/\ y con 31 caracteres como máximo).
// También quita caracteres de control y apóstrofos al principio y al final
// (Excel no los permite allí). Si no queda nada devuelve "Hoja". No garantiza
// nombres únicos: eso queda a cargo del llamador.
std::wstring SafeSheetName(const std::wstring& name);

// Bytes del archivo .xlsx.
// La primera hoja queda seleccionada al abrir el libro.
std::string BuildWorkbook(const std::vector<Sheet>& sheets);

// Escribe el libro en disco. false si falla (GetLastError tiene el motivo).
// Reemplaza el archivo si ya existe. Se abre sin compartir: si el archivo
// está abierto en Excel falla con ERROR_SHARING_VIOLATION.
bool SaveWorkbook(const std::wstring& path, const std::vector<Sheet>& sheets);

}  // namespace cola::util::xlsx
