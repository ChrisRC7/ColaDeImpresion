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
    std::map<std::wstring, Tally> byPaper, byPrinter, byDuplex, byOrientation, bySource, byUser, byMachine;
    std::map<std::wstring, Tally> byDay;  // "2026-10-09" (hora local)
    Tally byHour[24];                     // hora local de finalización
    // Trabajo impreso con más páginas.
    std::wstring largestDocument;
    std::wstring largestUser;
    std::uint32_t largestPages = 0;

    const Tally& Printed() const { return byOutcome[static_cast<int>(config::JobOutcome::Printed)]; }
};

HistorySummary SummarizeHistory(const std::vector<config::HistoryEntry>& entries);

// Hojas del libro. columnOrder = orden efectivo de las columnas (ids
// "history.*" de EffectiveColumnOrder); vacío = orden predeterminado.
std::vector<util::xlsx::Sheet> BuildHistoryWorkbook(const std::vector<config::HistoryEntry>& entries,
                                                    std::int64_t nowUnix,
                                                    const std::vector<std::wstring>& columnOrder = {});

// Nombre sugerido para el archivo ("Historial de impresión 2026-10-09.xlsx").
std::wstring DefaultExportFileName(std::int64_t nowUnix);

// Diálogo "Guardar como" + escritura. Devuelve la ruta guardada, o vacío si
// el operador canceló; si falla, error recibe el mensaje.
std::wstring RunHistoryExport(HWND owner, const std::vector<config::HistoryEntry>& entries,
                              const std::vector<std::wstring>& columnOrder, std::wstring& error);

}  // namespace cola::app
