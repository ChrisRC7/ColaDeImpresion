#pragma once
// Exportación del historial a Excel: una hoja con el historial completo (las
// mismas columnas y valores que la tabla, en el orden del operador), otra con
// el resumen general y una hoja por cada tabla del resumen (páginas impresas
// por papel, impresora, usuario, día...), para no tener que bajar entre tablas.
// El armado del libro es lógica pura (se prueba en tests/); RunHistoryExport
// pide el archivo con el diálogo de Windows y lo guarda.
//
// Decisión D43: escritor .xlsx propio (util/ZipWriter + util/Xlsx), sin
// dependencias. El período (todo o un rango de días) se elige antes en
// ui/ExportDialog; los días se cuentan por la hora LOCAL de finalización.
// Todo corre en el hilo de la UI (el libro es pequeño: el historial guarda
// por defecto hasta 2000 registros).

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "config/History.h"
#include "util/Xlsx.h"

namespace cola::app {

// Acumulador de una categoría: cuántos trabajos y cuántas páginas suman.
struct Tally {
    std::uint64_t jobs = 0;   // trabajos de la categoría
    std::uint64_t pages = 0;  // páginas (columna Páginas); los "—" suman 0
};

// Estadísticas del historial. Las tablas por categoría cuentan solo los
// trabajos impresos; las páginas son las de la columna "Páginas" (PrintPagesCount).
struct HistorySummary {
    std::uint64_t records = 0;   // registros totales (todos los resultados)
    std::int64_t firstUnix = 0;  // registro más antiguo / más reciente (0 = vacío)
    std::int64_t lastUnix = 0;
    Tally byOutcome[4];  // índice = config::JobOutcome
    std::uint64_t printedUnknownPages = 0;  // impresos con páginas "—"
    std::uint64_t printedBytes = 0;         // bytes de los trabajos impresos
    // Claves con el texto que mostraba la tabla ("" = sin dato).
    std::map<std::wstring, Tally> byPaper, byPrinter, byDuplex, byBinding, byOrientation, bySource, byUser, byMachine;
    std::map<std::wstring, Tally> byDay;  // "2026-10-09" (hora local)
    Tally byHour[24];                     // hora local de finalización
    // Trabajo impreso con más páginas.
    std::wstring largestDocument;
    std::wstring largestUser;
    std::uint32_t largestPages = 0;

    // Totales de los trabajos impresos (atajo a byOutcome[Printed]).
    const Tally& Printed() const { return byOutcome[static_cast<int>(config::JobOutcome::Printed)]; }
};

// Recorre los registros y llena HistorySummary. Las categorías usan los
// mismos textos que la tabla (MakeHistoryRow), así Excel coincide con lo que
// ve el operador; "—" se agrupa como "sin dato".
HistorySummary SummarizeHistory(const std::vector<config::HistoryEntry>& entries);

// Día con registros en el historial (por la hora local de finalización, como
// la tabla "Por día" del resumen).
struct HistoryDay {
    std::wstring key;  // "2026-10-09"
    std::uint32_t records = 0;  // registros de ese día (lo muestra el diálogo)
};

// Días con registros, del más antiguo al más reciente.
std::vector<HistoryDay> HistoryDays(const std::vector<config::HistoryEntry>& entries);

// "2026-10-09" -> "09/10/2026".
std::wstring DayDisplay(const std::wstring& key);

// Qué se exporta: todo el historial (claves vacías) o los días entre
// fromKey y toKey, ambos incluidos.
// Las claves tienen la forma "AAAA-MM-DD", así se pueden comparar como texto.
struct ExportPeriod {
    std::wstring fromKey;  // primer día incluido ("" = sin límite inferior)
    std::wstring toKey;    // último día incluido ("" = sin límite superior)
    // true = exportar todo el historial.
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
// entries: registros YA filtrados por período (FilterHistory); nowUnix: hora
// de generación (se muestra en el resumen). Orden de las hojas: Historial,
// Resumen y una por tabla (Por resultado, Por papel, ..., Por día, Por hora).
std::vector<util::xlsx::Sheet> BuildHistoryWorkbook(const std::vector<config::HistoryEntry>& entries,
                                                    std::int64_t nowUnix,
                                                    const std::vector<std::wstring>& columnOrder = {},
                                                    const ExportPeriod& period = {});

// Nombre sugerido para el archivo ("Historial de impresión 2026-10-09.xlsx";
// con un rango, "Historial de impresión 2026-10-01 a 2026-10-09.xlsx").
std::wstring DefaultExportFileName(std::int64_t nowUnix, const ExportPeriod& period = {});

// Diálogo "Guardar como" + escritura de los registros ya filtrados. Devuelve
// la ruta guardada, o vacío si el operador canceló; si falla, error recibe el mensaje.
// Se llama en el hilo de la UI (el diálogo es modal sobre owner).
std::wstring RunHistoryExport(HWND owner, const std::vector<config::HistoryEntry>& entries,
                              const std::vector<std::wstring>& columnOrder, const ExportPeriod& period,
                              std::wstring& error);

}  // namespace cola::app
