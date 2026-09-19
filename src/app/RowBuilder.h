#pragma once
// Traduce datos del dominio (trabajos, historial) a filas de la lista y textos
// de la interfaz. Lógica pura (sin ventanas) para poder probarla.

#include <string>
#include <vector>

#include "config/History.h"
#include "core/PrintTypes.h"
#include "ui/JobListView.h"

namespace cola::app {

// Valor especial de "impresora activa" que significa todas las del servidor.
inline constexpr wchar_t kAllPrinters[] = L"*";

std::wstring StateText(core::JobState state);
std::wstring OutcomeText(config::JobOutcome outcome);
std::wstring ProblemText(core::PrinterProblem problem);
std::wstring PagesText(const core::JobInfo& job);

// true si el trabajo coincide con el texto de búsqueda (documento, usuario,
// equipo, impresora, estado). Varias palabras = todas deben coincidir.
bool JobMatchesFilter(const core::JobInfo& job, const std::wstring& filter);
bool HistoryMatchesFilter(const config::HistoryEntry& entry, const std::wstring& filter);

// Trabajos visibles para la impresora elegida ("*" = todas) y el filtro,
// ordenados por impresora y posición en la cola.
std::vector<const core::JobInfo*> VisibleJobs(const core::QueueSnapshot& snapshot, const std::wstring& printer,
                                              const std::wstring& filter);

ui::ListRow MakeJobRow(const core::JobInfo& job, bool showPrinter);
ui::ListRow MakeHistoryRow(const config::HistoryEntry& entry, size_t index);
std::wstring HistoryKey(const config::HistoryEntry& entry, size_t index);

// Resumen para la barra de estado.
struct QueueSummary {
    unsigned total = 0;
    unsigned printing = 0;
    unsigned paused = 0;
    unsigned errors = 0;
};
QueueSummary Summarize(const std::vector<const core::JobInfo*>& jobs);
std::wstring SummaryText(const QueueSummary& summary);

}  // namespace cola::app
