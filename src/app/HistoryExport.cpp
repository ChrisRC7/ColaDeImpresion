// Implementación de la exportación del historial a Excel (ver
// HistoryExport.h, D43): estadísticas (SummarizeHistory), días con registros
// y filtro por período, armado de las hojas con util::xlsx y el diálogo
// "Guardar como". Los textos de las celdas salen de MakeHistoryRow para que
// el libro diga exactamente lo mismo que la tabla del historial.
#include "app/HistoryExport.h"

#include <commdlg.h>

#include <algorithm>

#include "app/RowBuilder.h"
#include "i18n/Strings.h"
#include "util/Format.h"
#include "util/Strings.h"

namespace cola::app {

using i18n::S;
using i18n::Tr;
using util::xlsx::Cell;
using util::xlsx::CellStyle;
using util::xlsx::Row;
using util::xlsx::Sheet;

namespace {

// Posición de cada columna del historial en las celdas de MakeHistoryRow.
// Permite leer una celda por id ("history.paper") sin depender del índice,
// así agregar columnas no rompe la exportación.
class HistoryColumnIndex {
public:
    HistoryColumnIndex() {
        const auto columns = HistoryColumns();
        for (size_t i = 0; i < columns.size(); ++i) index_[columns[i].id] = i;
    }
    // Texto de la celda `id` de la fila; "" si el id no existe o falta la celda.
    const std::wstring& Cell(const ui::ListRow& row, const wchar_t* id) const {
        static const std::wstring empty;
        auto it = index_.find(id);
        return it != index_.end() && it->second < row.cells.size() ? row.cells[it->second] : empty;
    }

private:
    std::map<std::wstring, size_t> index_;  // id de columna -> índice de celda
};

// Texto de la tabla como clave de categoría ("—" = sin dato).
std::wstring CategoryKey(const std::wstring& text) { return text == L"—" ? std::wstring() : text; }

// Hora Unix (UTC) -> hora local (la que ve el operador).
SYSTEMTIME LocalTime(std::int64_t unix) { return util::ToLocalTime(util::FromUnixSeconds(unix)); }

// Hora Unix -> número de serie de fecha de Excel en hora local (Excel no
// guarda zona horaria: se escribe la hora tal como se lee en el LAIMI).
double LocalSerial(std::int64_t unix) { return util::xlsx::SerialDate(LocalTime(unix)); }

// Suma un trabajo con sus páginas a una categoría.
void Add(Tally& tally, std::uint32_t pages) {
    ++tally.jobs;
    tally.pages += pages;
}

}  // namespace

HistorySummary SummarizeHistory(const std::vector<config::HistoryEntry>& entries) {
    HistorySummary s;
    const HistoryColumnIndex columns;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        ++s.records;
        if (!s.firstUnix || e.timeUnix < s.firstUnix) s.firstUnix = e.timeUnix;
        if (e.timeUnix > s.lastUnix) s.lastUnix = e.timeUnix;
        // Páginas de la columna Páginas (páginas x copias, D42); 0 = "—".
        const std::uint32_t pages = e.PrintPagesCount();
        // clamp: un historial editado a mano con un resultado fuera de rango
        // no debe escribir fuera del arreglo.
        const int outcome = std::clamp(static_cast<int>(e.outcome), 0, 3);
        Add(s.byOutcome[outcome], pages);
        // El resto de las tablas solo cuenta lo que realmente se imprimió.
        if (e.outcome != config::JobOutcome::Printed) continue;

        if (!pages) ++s.printedUnknownPages;
        s.printedBytes += e.sizeBytes;
        const ui::ListRow row = MakeHistoryRow(e, i);  // los mismos textos que la tabla
        Add(s.byPaper[CategoryKey(columns.Cell(row, L"history.paper"))], pages);
        Add(s.byPrinter[CategoryKey(columns.Cell(row, L"history.printer"))], pages);
        Add(s.byDuplex[CategoryKey(columns.Cell(row, L"history.duplex"))], pages);
        Add(s.byBinding[CategoryKey(columns.Cell(row, L"history.binding"))], pages);
        Add(s.byOrientation[CategoryKey(columns.Cell(row, L"history.orientation"))], pages);
        Add(s.bySource[CategoryKey(columns.Cell(row, L"history.source"))], pages);
        Add(s.byUser[e.user], pages);
        Add(s.byMachine[MachineName(e.machine)], pages);
        // Día y hora según la hora local de finalización (cuando salió de la cola).
        Add(s.byDay[util::FormatDateKey(util::FromUnixSeconds(e.timeUnix))], pages);
        Add(s.byHour[LocalTime(e.timeUnix).wHour % 24], pages);
        // Trabajo más grande: el primero con el máximo (">" estricto).
        if (pages > s.largestPages) {
            s.largestPages = pages;
            s.largestDocument = e.document;
            s.largestUser = e.user;
        }
    }
    return s;
}

namespace {

// Hora Unix -> clave de día "AAAA-MM-DD" (FormatDateKey usa la hora local,
// igual que la tabla "Por día").
std::wstring DayKey(std::int64_t unix) { return util::FormatDateKey(util::FromUnixSeconds(unix)); }

// "2026-10-09" -> fecha (sin hora).
// Una clave mal formada (más corta) devuelve una fecha en ceros.
SYSTEMTIME DayFromKey(const std::wstring& key) {
    SYSTEMTIME t{};
    if (key.size() < 10) return t;
    t.wYear = static_cast<WORD>(_wtoi(key.substr(0, 4).c_str()));
    t.wMonth = static_cast<WORD>(_wtoi(key.substr(5, 2).c_str()));
    t.wDay = static_cast<WORD>(_wtoi(key.substr(8, 2).c_str()));
    return t;
}

}  // namespace

std::vector<HistoryDay> HistoryDays(const std::vector<config::HistoryEntry>& entries) {
    // std::map ordena las claves "AAAA-MM-DD" como texto = cronológicamente.
    std::map<std::wstring, std::uint32_t> days;
    for (const auto& e : entries) ++days[DayKey(e.timeUnix)];
    std::vector<HistoryDay> out;
    for (const auto& [key, records] : days) out.push_back({key, records});
    return out;
}

std::wstring DayDisplay(const std::wstring& key) {
    const SYSTEMTIME t = DayFromKey(key);
    return util::Format(L"%02u/%02u/%04u", t.wDay, t.wMonth, t.wYear);
}

std::vector<config::HistoryEntry> FilterHistory(const std::vector<config::HistoryEntry>& entries,
                                                const ExportPeriod& period) {
    if (period.All()) return entries;
    std::vector<config::HistoryEntry> out;
    for (const auto& e : entries) {
        const std::wstring day = DayKey(e.timeUnix);  // "AAAA-MM-DD": se compara como texto
        if (!period.fromKey.empty() && day < period.fromKey) continue;
        if (!period.toKey.empty() && day > period.toKey) continue;
        out.push_back(e);
    }
    return out;
}

// Texto del período para el resumen: todo, un solo día o un rango.
std::wstring PeriodText(const ExportPeriod& period) {
    if (period.All()) return Tr(S::ExportAll);
    if (period.fromKey == period.toKey) return DayDisplay(period.fromKey);
    return util::Format(Tr(S::ExportPeriodRange), DayDisplay(period.fromKey).c_str(),
                        DayDisplay(period.toKey).c_str());
}

// Fecha de hoy si se exporta todo; el día o el rango exportado si no. Las
// fechas van en formato AAAA-MM-DD (se ordenan bien en el explorador y no
// tienen "/", que no se permite en nombres de archivo).
std::wstring DefaultExportFileName(std::int64_t nowUnix, const ExportPeriod& period) {
    std::wstring when = DayKey(nowUnix);
    if (!period.All())
        when = period.fromKey == period.toKey
                   ? period.fromKey
                   : util::Format(Tr(S::ExportNameRange), period.fromKey.c_str(), period.toKey.c_str());
    return util::Format(Tr(S::ExportDefaultName), when.c_str()) + L".xlsx";
}

namespace {

// Hoja "Historial": un registro por fila con las columnas de la tabla en el
// orden del operador y, al final, el servidor. Fila 1 fija y autofiltro.
Sheet HistorySheet(const std::vector<config::HistoryEntry>& entries, const std::vector<std::wstring>& columnOrder) {
    const auto columns = HistoryColumns();
    // Columnas en el orden en que el operador las ve.
    // columnOrder trae también los ids de la cola: solo coinciden los "history.*".
    std::vector<size_t> order;
    for (const auto& id : columnOrder) {
        for (size_t i = 0; i < columns.size(); ++i)
            if (columns[i].id == id) order.push_back(i);
    }
    // Si falta alguna columna (o se repite), se usa el orden predeterminado
    // para no exportar una hoja incompleta.
    if (order.size() != columns.size()) {
        order.clear();
        for (size_t i = 0; i < columns.size(); ++i) order.push_back(i);
    }

    Sheet sheet;
    sheet.name = Tr(S::XlsSheetHistory);
    sheet.frozenRows = 1;
    sheet.autoFilter = true;
    Row header;
    for (size_t i : order) {
        const auto& column = columns[i];
        // Tamaño se exporta como número en KB (para poder sumarlo), así que
        // su título lo dice.
        header.push_back(Cell::Text(column.id == L"history.size" ? std::wstring(Tr(S::XlsSizeKb)) : column.label,
                                    CellStyle::Header));
        // Ancho en caracteres de Excel: aprox. el ancho en DIP / 6,5 (mínimo
        // 10); Documento ancho fijo de 50 y las fechas 17 (fecha y hora).
        double width = column.id == L"history.document" ? 50 : std::max(10.0, column.width / 6.5);
        if (column.id == L"history.submitted" || column.id == L"history.time") width = 17;
        sheet.columnWidths.push_back(width);
    }
    header.push_back(Cell::Text(Tr(S::XlsServer), CellStyle::Header));
    sheet.columnWidths.push_back(20);
    sheet.rows.push_back(std::move(header));

    // Filas de datos. Por defecto se copia el texto de la tabla; algunas
    // columnas se escriben como número o fecha de Excel para poder filtrar,
    // sumar u ordenar bien (si no hay dato, queda el texto "—").
    for (size_t index = 0; index < entries.size(); ++index) {
        const auto& e = entries[index];
        const ui::ListRow row = MakeHistoryRow(e, index);
        Row out;
        for (size_t i : order) {
            const std::wstring& id = columns[i].id;
            const std::wstring text = i < row.cells.size() ? row.cells[i] : std::wstring();
            if (id == L"history.number") {
                out.push_back(e.position ? Cell::Number(e.position, CellStyle::Integer) : Cell::Blank());
            } else if (id == L"history.pages") {
                const std::uint32_t pages = e.PrintPagesCount();
                out.push_back(pages ? Cell::Number(pages, CellStyle::Integer) : Cell::Text(text));
            } else if (id == L"history.size") {
                out.push_back(Cell::Number(static_cast<double>(e.sizeBytes) / 1024.0, CellStyle::Decimal));
            } else if (id == L"history.submitted") {
                out.push_back(e.submittedUnix ? Cell::Number(LocalSerial(e.submittedUnix), CellStyle::DateTime)
                                              : Cell::Text(text));
            } else if (id == L"history.time") {
                out.push_back(Cell::Number(LocalSerial(e.timeUnix), CellStyle::DateTime));
            } else {
                out.push_back(Cell::Text(text));
            }
        }
        out.push_back(Cell::Text(e.server));
        sheet.rows.push_back(std::move(out));
    }
    return sheet;
}

// Hoja con una tabla "categoría | trabajos | páginas | %": el título y los
// encabezados quedan fijos para recorrer listas largas (usuarios, equipos, días).
Sheet TableSheet(S name, S title, const std::wstring& categoryTitle, std::vector<std::pair<Cell, Tally>> rows,
                 std::uint64_t totalPages, std::uint64_t totalJobs) {
    Sheet sheet;
    sheet.name = Tr(name);
    sheet.columnWidths = {46, 12, 12, 18};
    sheet.frozenRows = 2;
    sheet.rows.push_back({Cell::Text(Tr(title), CellStyle::Title)});
    sheet.rows.push_back({Cell::Text(categoryTitle, CellStyle::Header), Cell::Text(Tr(S::XlsJobs), CellStyle::Header),
                          Cell::Text(Tr(S::XlsPages), CellStyle::Header),
                          Cell::Text(Tr(totalPages ? S::XlsShare : S::XlsShareJobs), CellStyle::Header)});
    // Sin filas la hoja parecería mal exportada: se dice por qué está vacía.
    if (rows.empty()) sheet.rows.push_back({Cell::Text(Tr(S::XlsNoPrinted), CellStyle::Muted)});
    for (auto& [label, tally] : rows) {
        const double share = totalPages ? static_cast<double>(tally.pages) / static_cast<double>(totalPages)
                             : totalJobs ? static_cast<double>(tally.jobs) / static_cast<double>(totalJobs)
                                         : 0.0;
        sheet.rows.push_back({std::move(label), Cell::Number(static_cast<double>(tally.jobs), CellStyle::Integer),
                              Cell::Number(static_cast<double>(tally.pages), CellStyle::Integer),
                              Cell::Number(share, CellStyle::Percent)});
    }
    sheet.rows.push_back({Cell::Text(Tr(S::XlsTotal), CellStyle::Bold),
                          Cell::Number(static_cast<double>(totalJobs), CellStyle::Integer),
                          Cell::Number(static_cast<double>(totalPages), CellStyle::Integer)});
    return sheet;
}

// Categorías ordenadas de más a menos páginas (a igualdad, más trabajos y luego por nombre).
// (El "por nombre" lo da stable_sort sobre el std::map, que ya viene por nombre.)
// La categoría vacía se muestra como "Sin dato".
std::vector<std::pair<Cell, Tally>> ByPages(const std::map<std::wstring, Tally>& map) {
    std::vector<std::pair<std::wstring, Tally>> sorted(map.begin(), map.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        if (a.second.pages != b.second.pages) return a.second.pages > b.second.pages;
        return a.second.jobs > b.second.jobs;
    });
    std::vector<std::pair<Cell, Tally>> rows;
    for (auto& [key, tally] : sorted) rows.push_back({Cell::Text(key.empty() ? Tr(S::XlsNoData) : key), tally});
    return rows;
}

// Categoría con más páginas de un mapa (la primera en orden alfabético si
// empatan). Devuelve nullptr si el mapa está vacío; `key` recibe su nombre.
template <typename T>
const T* MaxPages(const std::map<std::wstring, T>& map, std::wstring& key) {
    const T* best = nullptr;
    for (const auto& [k, tally] : map) {
        if (!best || tally.pages > best->pages) {
            best = &tally;
            key = k;
        }
    }
    return best;
}

// Hoja "Resumen" con los datos generales y una hoja por cada tabla.
std::vector<Sheet> SummarySheets(const std::vector<config::HistoryEntry>& entries, std::int64_t nowUnix,
                                 const ExportPeriod& period) {
    const HistorySummary s = SummarizeHistory(entries);
    const Tally& printed = s.Printed();
    // Totales de un resultado concreto.
    auto outcome = [&](config::JobOutcome o) -> const Tally& { return s.byOutcome[static_cast<int>(o)]; };

    // Hoja "Resumen": título, fecha de generación, nota explicativa y tabla
    // "Dato | Valor" con los datos generales.
    Sheet sheet;
    sheet.name = Tr(S::XlsSheetSummary);
    sheet.columnWidths = {46, 40};
    sheet.rows.push_back({Cell::Text(Tr(S::XlsTitle), CellStyle::Title)});
    sheet.rows.push_back({Cell::Text(
        util::Format(Tr(S::XlsGenerated), util::FormatDateTime(util::FromUnixSeconds(nowUnix)).c_str()),
        CellStyle::Muted)});
    sheet.rows.push_back({Cell::Text(Tr(S::XlsNote), CellStyle::Muted)});

    // General.
    auto number = [](std::uint64_t v) { return Cell::Number(static_cast<double>(v), CellStyle::Integer); };
    auto line = [&](S label, Cell value) { sheet.rows.push_back({Cell::Text(Tr(label)), std::move(value)}); };
    sheet.rows.push_back({});
    sheet.rows.push_back({Cell::Text(Tr(S::XlsGeneral), CellStyle::Bold)});
    sheet.rows.push_back({Cell::Text(Tr(S::XlsItem), CellStyle::Header), Cell::Text(Tr(S::XlsValue), CellStyle::Header)});
    line(S::XlsPeriod, Cell::Text(PeriodText(period)));
    line(S::XlsRecords, number(s.records));
    if (s.records) {
        line(S::XlsFrom, Cell::Number(LocalSerial(s.firstUnix), CellStyle::DateTime));
        line(S::XlsTo, Cell::Number(LocalSerial(s.lastUnix), CellStyle::DateTime));
    }
    line(S::XlsPrintedJobs, number(printed.jobs));
    line(S::XlsPrintedPages, number(printed.pages));
    // Promedio de páginas por trabajo impreso, solo entre los que tienen
    // páginas conocidas (los "—" bajarían el promedio sin razón).
    const std::uint64_t knownJobs = printed.jobs - s.printedUnknownPages;
    line(S::XlsAvgPages, Cell::Number(knownJobs ? static_cast<double>(printed.pages) / static_cast<double>(knownJobs) : 0,
                                      CellStyle::Decimal));
    line(S::XlsUnknownPages, number(s.printedUnknownPages));
    line(S::XlsCancelledJobs, number(outcome(config::JobOutcome::Cancelled).jobs));
    line(S::XlsCancelledPages, number(outcome(config::JobOutcome::Cancelled).pages));
    line(S::XlsFailedJobs, number(outcome(config::JobOutcome::Failed).jobs));
    line(S::XlsRemovedJobs, number(outcome(config::JobOutcome::Removed).jobs));
    line(S::XlsUsers, number(s.byUser.size()));
    line(S::XlsMachines, number(s.byMachine.size()));
    line(S::XlsPrinters, number(s.byPrinter.size()));
    line(S::XlsDays, number(s.byDay.size()));
    line(S::XlsAvgPerDay, Cell::Number(s.byDay.empty() ? 0
                                                        : static_cast<double>(printed.pages) /
                                                              static_cast<double>(s.byDay.size()),
                                       CellStyle::Decimal));
    // Destacados: día, hora y usuario con más páginas, y el trabajo más grande.
    std::wstring key;
    if (const Tally* day = MaxPages(s.byDay, key)) {
        line(S::XlsBusiestDay, Cell::Text(util::Format(Tr(S::XlsDayPages), DayDisplay(key).c_str(),
                                                       static_cast<unsigned long long>(day->pages))));
    }
    int busiestHour = -1;
    for (int h = 0; h < 24; ++h)
        if (s.byHour[h].jobs && (busiestHour < 0 || s.byHour[h].pages > s.byHour[busiestHour].pages)) busiestHour = h;
    if (busiestHour >= 0) {
        const std::wstring hour = util::Format(Tr(S::XlsHourRange), busiestHour, busiestHour);
        line(S::XlsBusiestHour, Cell::Text(util::Format(Tr(S::XlsDayPages), hour.c_str(),
                                                        static_cast<unsigned long long>(s.byHour[busiestHour].pages))));
    }
    if (const Tally* user = MaxPages(s.byUser, key))
        line(S::XlsTopUser, Cell::Text(util::Format(Tr(S::XlsDayPages), key.c_str(),
                                                    static_cast<unsigned long long>(user->pages))));
    if (s.largestPages)
        line(S::XlsLargestJob, Cell::Text(util::Format(Tr(S::XlsJobPages), s.largestDocument.c_str(),
                                                       s.largestUser.c_str(), s.largestPages)));
    // Volumen impreso en MB.
    line(S::XlsPrintedSize, Cell::Number(static_cast<double>(s.printedBytes) / (1024.0 * 1024.0), CellStyle::Decimal));

    std::vector<Sheet> sheets;
    sheets.push_back(std::move(sheet));

    // Por resultado (todos los trabajos).
    std::vector<std::pair<Cell, Tally>> outcomes;
    std::uint64_t allJobs = 0, allPages = 0;
    for (auto o : {config::JobOutcome::Printed, config::JobOutcome::Cancelled, config::JobOutcome::Failed,
                   config::JobOutcome::Removed}) {
        outcomes.push_back({Cell::Text(OutcomeText(o)), outcome(o)});
        allJobs += outcome(o).jobs;
        allPages += outcome(o).pages;
    }
    // El porcentaje de esta tabla es sobre los trabajos (incluye los no impresos).
    // Se pasa totalPages = 0 para que TableSheet calcule el % sobre trabajos;
    // después se corrige la celda de páginas de la fila Total (la última
    // fila, columna 2) con el total real.
    sheets.push_back(TableSheet(S::XlsSheetOutcome, S::XlsByOutcome, Tr(S::ColResult), std::move(outcomes), 0,
                                allJobs));
    sheets.back().rows.back()[2] = Cell::Number(static_cast<double>(allPages), CellStyle::Integer);

    // Las demás tablas: porcentaje sobre las páginas impresas.
    auto table = [&](S name, S title, S category, std::vector<std::pair<Cell, Tally>> rows) {
        sheets.push_back(TableSheet(name, title, Tr(category), std::move(rows), printed.pages, printed.jobs));
    };
    table(S::XlsSheetPaper, S::XlsByPaper, S::ColPaper, ByPages(s.byPaper));
    table(S::XlsSheetPrinter, S::XlsByPrinter, S::ColPrinter, ByPages(s.byPrinter));
    table(S::XlsSheetDuplex, S::XlsByDuplex, S::ColDuplex, ByPages(s.byDuplex));
    table(S::XlsSheetBinding, S::XlsByBinding, S::ColBinding, ByPages(s.byBinding));
    table(S::XlsSheetOrientation, S::XlsByOrientation, S::ColOrientation, ByPages(s.byOrientation));
    table(S::XlsSheetSource, S::XlsBySource, S::ColSource, ByPages(s.bySource));
    table(S::XlsSheetUser, S::XlsByUser, S::ColUser, ByPages(s.byUser));
    table(S::XlsSheetMachine, S::XlsByMachine, S::ColMachine, ByPages(s.byMachine));

    // Por día (cronológico, con la fecha como fecha de Excel) y por hora.
    std::vector<std::pair<Cell, Tally>> days;
    for (const auto& [day, tally] : s.byDay)
        days.push_back({Cell::Number(util::xlsx::SerialDate(DayFromKey(day)), CellStyle::Date), tally});
    table(S::XlsSheetDay, S::XlsByDay, S::XlsDate, std::move(days));
    // Por hora: solo las franjas con trabajos ("08:00–08:59").
    std::vector<std::pair<Cell, Tally>> hours;
    for (int h = 0; h < 24; ++h)
        if (s.byHour[h].jobs) hours.push_back({Cell::Text(util::Format(Tr(S::XlsHourRange), h, h)), s.byHour[h]});
    table(S::XlsSheetHour, S::XlsByHour, S::XlsHour, std::move(hours));
    return sheets;
}

}  // namespace

std::vector<Sheet> BuildHistoryWorkbook(const std::vector<config::HistoryEntry>& entries, std::int64_t nowUnix,
                                        const std::vector<std::wstring>& columnOrder, const ExportPeriod& period) {
    // Primero el historial (la hoja que se abre al abrir el libro) y luego
    // el resumen y sus tablas.
    std::vector<Sheet> sheets;
    sheets.push_back(HistorySheet(entries, columnOrder));
    for (auto& sheet : SummarySheets(entries, nowUnix, period)) sheets.push_back(std::move(sheet));
    return sheets;
}

std::wstring RunHistoryExport(HWND owner, const std::vector<config::HistoryEntry>& entries,
                              const std::vector<std::wstring>& columnOrder, const ExportPeriod& period,
                              std::wstring& error) {
    error.clear();
    const std::int64_t now = util::ToUnixSeconds(util::NowUtc());
    // Búfer del nombre de archivo para GetSaveFileNameW: arranca con el
    // nombre sugerido y se amplía con ceros (el diálogo escribe ahí la ruta
    // elegida, hasta nMaxFile caracteres).
    std::wstring file = DefaultExportFileName(now, period);
    file.resize(4096, L'\0');
    // Filtro en el formato de OPENFILENAME: pares "descripción\0patrón\0"
    // terminados con un \0 adicional.
    std::wstring filter = Tr(S::ExportFilter);
    filter += L'\0';
    filter += L"*.xlsx";
    filter += L'\0';
    filter += L'\0';

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.lpstrDefExt = L"xlsx";
    // OVERWRITEPROMPT: pregunta antes de reemplazar; NOCHANGEDIR: no cambia
    // el directorio actual del proceso (las rutas relativas siguen valiendo).
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    // FALSE = canceló (o error del diálogo): no es un fallo de exportación.
    if (!GetSaveFileNameW(&ofn)) return L"";
    // Recorta el búfer hasta el primer \0 (la ruta elegida).
    file.resize(wcslen(file.c_str()));

    // Si falla, el motivo se toma de GetLastError (lo último que dejó la
    // escritura del archivo: acceso denegado, archivo abierto en Excel...).
    if (!util::xlsx::SaveWorkbook(file, BuildHistoryWorkbook(entries, now, columnOrder, period))) {
        error = util::Win32ErrorMessage(GetLastError());
        return L"";
    }
    return file;
}

}  // namespace cola::app
