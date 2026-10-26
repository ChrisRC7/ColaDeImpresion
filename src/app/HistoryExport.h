#pragma once
// Exportación del historial a Excel: una hoja con el historial completo (las
// mismas columnas y valores que la tabla, en el orden del operador), otra con
// el resumen general y una hoja por cada tabla del resumen (páginas impresas
// por papel, impresora, usuario, día...), para no tener que bajar entre tablas.
// El armado del libro es lógica pura (se prueba en tests/); RunHistoryExport
// pide el archivo con el diálogo de Windows y lo guarda.

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "config/History.h"
#include "util/Xlsx.h"

namespace cola::app {

struct Tally {
    std::uint64_t jobs = 0;
    std::uint64_t pages = 0;
};

// Estadísticas del historial. Las tablas por categoría cuentan solo los
// trabajos impresos; las páginas son las de la columna "Páginas" (PrintPagesCount).
struct HistorySummary {
    std::uint64_t records = 0;
    std::int64_t firstUnix = 0;  // registro más antiguo / más reciente (0 = vacío)
    std::int64_t lastUnix = 0;
    Tally byOutcome[4];  // índice = config::JobOutcome
    std::uint64_t printedUnknownPages = 0;  // impresos con páginas "—"
    std::uint64_t printedBytes = 0;
    // Claves con el texto que mostraba la tabla ("" = sin dato).
    std::map<std::wstring, Tally> byPaper, byPrinter, byDuplex, byBinding, byOrientation, bySource, byUser, byMachine;
    std::map<std::wstring, Tally> byDay;  // "2026-10-09" (hora local)
    Tally byHour[24];                     // hora local de finalización
    // Trabajo impreso con más páginas.
    std::wstring largestDocument;
    std::wstring largestUser;
    std::uint32_t largestPages = 0;

    const Tally& Printed() const { return byOutcome[static_cast<int>(config::JobOutcome::Printed)]; }
};

HistorySummary SummarizeHistory(const std::vector<config::HistoryEntry>& entries);

// Día con registros en el historial (por la hora local de finalización, como
// la tabla "Por día" del resumen).
struct HistoryDay {
    std::wstring key;  // "2026-10-09"
    std::uint32_t records = 0;
};

// Días con registros, del más antiguo al más reciente.
std::vector<HistoryDay> HistoryDays(const std::vector<config::HistoryEntry>& entries);

// "2026-10-09" -> "09/10/2026".
std::wstring DayDisplay(const std::wstring& key);

// Qué se exporta: todo el historial (claves vacías) o los días entre
// fromKey y toKey, ambos incluidos.
struct ExportPeriod {
    std::wstring fromKey;
    std::wstring toKey;
    bool All() const { return fromKey.empty() && toKey.empty(); }
};

// Registros del período, en el mismo orden.
std::vector<config::HistoryEntry> FilterHistory(const std::vector<config::HistoryEntry>& entries,
                                                const ExportPeriod& period);

// "Todo el historial", "09/10/2026" o "Del 01/10/2026 al 09/10/2026".
std::wstring PeriodText(const ExportPeriod& period);

// Hojas del libro. columnOrder = orden efectivo de las columnas (ids
// "history.*" de EffectiveColumnOrder); vacío = orden predeterminado. El
// período exportado se indica en el resumen.
std::vector<util::xlsx::Sheet> BuildHistoryWorkbook(const std::vector<config::HistoryEntry>& entries,
                                                    std::int64_t nowUnix,
                                                    const std::vector<std::wstring>& columnOrder = {},
                                                    const ExportPeriod& period = {});

// Nombre sugerido para el archivo ("Historial de impresión 2026-10-09.xlsx";
// con un rango, "Historial de impresión 2026-10-01 a 2026-10-09.xlsx").
std::wstring DefaultExportFileName(std::int64_t nowUnix, const ExportPeriod& period = {});

// Diálogo "Guardar como" + escritura de los registros ya filtrados. Devuelve
// la ruta guardada, o vacío si el operador canceló; si falla, error recibe el mensaje.
std::wstring RunHistoryExport(HWND owner, const std::vector<config::HistoryEntry>& entries,
                              const std::vector<std::wstring>& columnOrder, const ExportPeriod& period,
                              std::wstring& error);

}  // namespace cola::app
