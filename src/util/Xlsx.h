#pragma once
// Escritor mínimo de libros de Excel (.xlsx, Office Open XML): hojas con texto,
// números y fechas, un juego fijo de estilos, anchos de columna, fila de
// títulos fija y autofiltro. Sin dependencias (el ZIP lo arma ZipWriter).

#include <windows.h>

#include <string>
#include <vector>

namespace cola::util::xlsx {

// Estilos disponibles (índices de cellXfs en styles.xml).
enum class CellStyle { Normal, Header, DateTime, Title, Bold, Integer, Percent, Decimal, Date, Muted };

struct Cell {
    enum class Kind { Empty, Text, Number };
    Kind kind = Kind::Empty;
    std::wstring text;
    double number = 0;
    CellStyle style = CellStyle::Normal;

    static Cell Text(std::wstring text, CellStyle style = CellStyle::Normal);
    static Cell Number(double value, CellStyle style = CellStyle::Normal);
    static Cell Blank(CellStyle style = CellStyle::Normal);
};

using Row = std::vector<Cell>;

struct Sheet {
    std::wstring name;                 // máx. 31 caracteres; se limpian los no permitidos
    std::vector<double> columnWidths;  // en caracteres; 0 = predeterminado
    std::vector<Row> rows;
    bool freezeFirstRow = false;
    bool autoFilter = false;  // sobre la primera fila y todas las columnas usadas
};

// "A", "B", ..., "Z", "AA"... (index desde 0).
std::wstring ColumnName(int index);

// Número de serie de Excel (días desde 30/12/1899) de una fecha y hora local.
double SerialDate(const SYSTEMTIME& local);

// Nombre de hoja válido para Excel (sin []:*?/\ y con 31 caracteres como máximo).
std::wstring SafeSheetName(const std::wstring& name);

// Bytes del archivo .xlsx.
std::string BuildWorkbook(const std::vector<Sheet>& sheets);

// Escribe el libro en disco. false si falla (GetLastError tiene el motivo).
bool SaveWorkbook(const std::wstring& path, const std::vector<Sheet>& sheets);

}  // namespace cola::util::xlsx
