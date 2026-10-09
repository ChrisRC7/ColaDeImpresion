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
class HistoryColumnIndex {
public:
    HistoryColumnIndex() {
        const auto columns = HistoryColumns();
        for (size_t i = 0; i < columns.size(); ++i) index_[columns[i].id] = i;
    }
    const std::wstring& Cell(const ui::ListRow& row, const wchar_t* id) const {
        static const std::wstring empty;
        auto it = index_.find(id);
        return it != index_.end() && it->second < row.cells.size() ? row.cells[it->second] : empty;
    }

private:
    std::map<std::wstring, size_t> index_;
};

// Texto de la tabla como clave de categoría ("—" = sin dato).
std::wstring CategoryKey(const std::wstring& text) { return text == L"—" ? std::wstring() : text; }

SYSTEMTIME LocalTime(std::int64_t unix) { return util::ToLocalTime(util::FromUnixSeconds(unix)); }

double LocalSerial(std::int64_t unix) { return util::xlsx::SerialDate(LocalTime(unix)); }

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
        const std::uint32_t pages = e.PrintPagesCount();
        const int outcome = std::clamp(static_cast<int>(e.outcome), 0, 3);
        Add(s.byOutcome[outcome], pages);
        if (e.outcome != config::JobOutcome::Printed) continue;

        if (!pages) ++s.printedUnknownPages;
        s.printedBytes += e.sizeBytes;
        const ui::ListRow row = MakeHistoryRow(e, i);  // los mismos textos que la tabla
        Add(s.byPaper[CategoryKey(columns.Cell(row, L"history.paper"))], pages);
        Add(s.byPrinter[CategoryKey(columns.Cell(row, L"history.printer"))], pages);
        Add(s.byDuplex[CategoryKey(columns.Cell(row, L"history.duplex"))], pages);
        Add(s.byOrientation[CategoryKey(columns.Cell(row, L"history.orientation"))], pages);
        Add(s.bySource[CategoryKey(columns.Cell(row, L"history.source"))], pages);
        Add(s.byUser[e.user], pages);
        Add(s.byMachine[MachineName(e.machine)], pages);
        Add(s.byDay[util::FormatDateKey(util::FromUnixSeconds(e.timeUnix))], pages);
        Add(s.byHour[LocalTime(e.timeUnix).wHour % 24], pages);
        if (pages > s.largestPages) {
            s.largestPages = pages;
            s.largestDocument = e.document;
            s.largestUser = e.user;
        }
    }
    return s;
}

std::wstring DefaultExportFileName(std::int64_t nowUnix) {
    return util::Format(Tr(S::ExportDefaultName), util::FormatDateKey(util::FromUnixSeconds(nowUnix)).c_str()) +
           L".xlsx";
}

namespace {

Sheet HistorySheet(const std::vector<config::HistoryEntry>& entries, const std::vector<std::wstring>& columnOrder) {
    const auto columns = HistoryColumns();
    // Columnas en el orden en que el operador las ve.
    std::vector<size_t> order;
    for (const auto& id : columnOrder) {
        for (size_t i = 0; i < columns.size(); ++i)
            if (columns[i].id == id) order.push_back(i);
    }
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
        header.push_back(Cell::Text(column.id == L"history.size" ? std::wstring(Tr(S::XlsSizeKb)) : column.label,
                                    CellStyle::Header));
        double width = column.id == L"history.document" ? 50 : std::max(10.0, column.width / 6.5);
        if (column.id == L"history.submitted" || column.id == L"history.time") width = 17;
        sheet.columnWidths.push_back(width);
    }
    header.push_back(Cell::Text(Tr(S::XlsServer), CellStyle::Header));
    sheet.columnWidths.push_back(20);
    sheet.rows.push_back(std::move(header));

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
std::vector<Sheet> SummarySheets(const std::vector<config::HistoryEntry>& entries, std::int64_t nowUnix) {
    const HistorySummary s = SummarizeHistory(entries);
    const Tally& printed = s.Printed();
    auto outcome = [&](config::JobOutcome o) -> const Tally& { return s.byOutcome[static_cast<int>(o)]; };

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
    line(S::XlsRecords, number(s.records));
    if (s.records) {
        line(S::XlsFrom, Cell::Number(LocalSerial(s.firstUnix), CellStyle::DateTime));
        line(S::XlsTo, Cell::Number(LocalSerial(s.lastUnix), CellStyle::DateTime));
    }
    line(S::XlsPrintedJobs, number(printed.jobs));
    line(S::XlsPrintedPages, number(printed.pages));
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
    std::wstring key;
    if (const Tally* day = MaxPages(s.byDay, key)) {
        SYSTEMTIME t{};
        t.wYear = static_cast<WORD>(_wtoi(key.substr(0, 4).c_str()));
        t.wMonth = static_cast<WORD>(_wtoi(key.substr(5, 2).c_str()));
        t.wDay = static_cast<WORD>(_wtoi(key.substr(8, 2).c_str()));
        const std::wstring date = util::Format(L"%02u/%02u/%04u", t.wDay, t.wMonth, t.wYear);
        line(S::XlsBusiestDay,
             Cell::Text(util::Format(Tr(S::XlsDayPages), date.c_str(), static_cast<unsigned long long>(day->pages))));
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
    table(S::XlsSheetOrientation, S::XlsByOrientation, S::ColOrientation, ByPages(s.byOrientation));
    table(S::XlsSheetSource, S::XlsBySource, S::ColSource, ByPages(s.bySource));
    table(S::XlsSheetUser, S::XlsByUser, S::ColUser, ByPages(s.byUser));
    table(S::XlsSheetMachine, S::XlsByMachine, S::ColMachine, ByPages(s.byMachine));

    // Por día (cronológico, con la fecha como fecha de Excel) y por hora.
    std::vector<std::pair<Cell, Tally>> days;
    for (const auto& [day, tally] : s.byDay) {
        SYSTEMTIME t{};
        t.wYear = static_cast<WORD>(_wtoi(day.substr(0, 4).c_str()));
        t.wMonth = static_cast<WORD>(_wtoi(day.substr(5, 2).c_str()));
        t.wDay = static_cast<WORD>(_wtoi(day.substr(8, 2).c_str()));
        days.push_back({Cell::Number(util::xlsx::SerialDate(t), CellStyle::Date), tally});
    }
    table(S::XlsSheetDay, S::XlsByDay, S::XlsDate, std::move(days));
    std::vector<std::pair<Cell, Tally>> hours;
    for (int h = 0; h < 24; ++h)
        if (s.byHour[h].jobs) hours.push_back({Cell::Text(util::Format(Tr(S::XlsHourRange), h, h)), s.byHour[h]});
    table(S::XlsSheetHour, S::XlsByHour, S::XlsHour, std::move(hours));
    return sheets;
}

}  // namespace

std::vector<Sheet> BuildHistoryWorkbook(const std::vector<config::HistoryEntry>& entries, std::int64_t nowUnix,
                                        const std::vector<std::wstring>& columnOrder) {
    std::vector<Sheet> sheets;
    sheets.push_back(HistorySheet(entries, columnOrder));
    for (auto& sheet : SummarySheets(entries, nowUnix)) sheets.push_back(std::move(sheet));
    return sheets;
}

std::wstring RunHistoryExport(HWND owner, const std::vector<config::HistoryEntry>& entries,
                              const std::vector<std::wstring>& columnOrder, std::wstring& error) {
    error.clear();
    const std::int64_t now = util::ToUnixSeconds(util::NowUtc());
    std::wstring file = DefaultExportFileName(now);
    file.resize(4096, L'\0');
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
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return L"";
    file.resize(wcslen(file.c_str()));

    if (!util::xlsx::SaveWorkbook(file, BuildHistoryWorkbook(entries, now, columnOrder))) {
        error = util::Win32ErrorMessage(GetLastError());
        return L"";
    }
    return file;
}

}  // namespace cola::app
