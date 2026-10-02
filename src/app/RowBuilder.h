#pragma once
// Traduce datos del dominio (trabajos, historial) a filas de la lista y textos
// de la interfaz. Lógica pura (sin ventanas) para poder probarla.

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "config/History.h"
#include "core/PageAccounting.h"
#include "core/PrintTypes.h"
#include "ui/JobListView.h"
#include "ui/PrinterBar.h"

namespace cola::app {

// Valor especial de "impresora activa" que significa todas las del servidor.
inline constexpr wchar_t kAllPrinters[] = L"*";

// Criterio para ordenar la cola. "Queue" es el orden real de impresión
// (impresora + posición); los demás son vistas para buscar trabajos.
// Pages = páginas que se van a imprimir en total (páginas x copias).
enum class SortBy { Queue, Machine, Time, User, Document, Status, Size, Pages, Printer, Paper, Orientation, Duplex,
                    Source, Count_ };

inline constexpr int kSortCount = static_cast<int>(SortBy::Count_);

// Identificador estable para config.json ("queue", "machine", "time"...).
const wchar_t* SortById(SortBy sort);
SortBy SortByFromId(const std::wstring& id);
// Texto traducido para el menú y la barra de estado.
std::wstring SortName(SortBy sort);

std::wstring StateText(core::JobState state);
// Estado de un trabajo concreto ("Retenido" si lo pausó la retención automática).
std::wstring JobStatusText(const core::JobInfo& job);
std::wstring OutcomeText(config::JobOutcome outcome);
std::wstring ProblemText(core::PrinterProblem problem);
// Color con el que se muestra un problema de impresora.
ui::Tone ToneForProblem(core::PrinterProblem problem);
std::wstring PagesText(const core::JobInfo& job);

// Páginas que se van a imprimir en total: páginas x copias
// (PageAccounting::printPages). Con 3 páginas y 3 copias, 9. Si no hay datos
// del trabajo se estima con TotalPages x dmCopies y se marca como estimado.
struct PrintTotal {
    std::uint32_t value = 0;
    core::Reliability reliability = core::Reliability::Unknown;
};
PrintTotal TotalPrintPages(const core::JobInfo& job, const core::ContentFacts* facts);
// "9" o "—" (no determinable). La fiabilidad se ve en la pestaña Configuración.
std::wstring TotalPagesText(const PrintTotal& total);

// Datos analizados de un trabajo (puede devolver nullptr).
using FactsLookup = std::function<const core::ContentFacts*(const core::JobInfo&)>;

// Columnas de la tabla. En la cola, sortId es el SortBy de la columna.
std::vector<ui::ListColumn> JobColumns(bool showPrinter);
std::vector<ui::ListColumn> HistoryColumns();

// Clic en el encabezado de una columna, como en la cola de Windows: la misma
// columna invierte el orden; otra columna ordena de forma ascendente.
std::pair<SortBy, bool> NextSort(SortBy current, bool descending, SortBy clicked);

// true si el trabajo coincide con el texto de búsqueda (documento, usuario,
// equipo, impresora, estado). Varias palabras = todas deben coincidir.
bool JobMatchesFilter(const core::JobInfo& job, const std::wstring& filter);
bool HistoryMatchesFilter(const config::HistoryEntry& entry, const std::wstring& filter);

// Trabajos visibles para la impresora elegida ("*" = todas) y el filtro,
// ordenados según el criterio pedido (por defecto, el orden de la cola).
std::vector<const core::JobInfo*> VisibleJobs(const core::QueueSnapshot& snapshot, const std::wstring& printer,
                                              const std::wstring& filter, SortBy sort = SortBy::Queue,
                                              bool descending = false, const FactsLookup& facts = {});

// Nombre del equipo sin las barras iniciales ("\\\\LAB-PC07" -> "LAB-PC07").
std::wstring MachineName(const std::wstring& machine);

// Celdas en el orden de JobColumns(showPrinter).
// printer (puede ser nullptr) aporta los nombres de bandejas y papeles del controlador.
ui::ListRow MakeJobRow(const core::JobInfo& job, bool showPrinter, const core::ContentFacts* facts = nullptr,
                       const core::PrinterInfo* printer = nullptr);
ui::ListRow MakeHistoryRow(const config::HistoryEntry& entry, size_t index);
std::wstring HistoryKey(const config::HistoryEntry& entry, size_t index);

// Estado al que debe llevarse un conjunto de trabajos al alternar la pausa:
// true = pausar (hay alguno corriendo), false = reanudar (todos están en pausa).
bool ShouldPauseJobs(const std::vector<core::JobInfo>& jobs);

// Impresoras distintas a las que pertenecen esos trabajos.
std::vector<std::wstring> PrintersOf(const std::vector<core::JobInfo>& jobs);

// Construye los botones del acceso rápido a impresoras a partir de las
// impresoras fijadas por el operador. Devuelve solo las que existen en el
// servidor actual, en el orden en que están fijadas, con su cuenta de trabajos
// y su problema más grave. El primer botón siempre es "Todas".
std::vector<ui::PrinterChip> BuildQuickAccess(const core::QueueSnapshot& snapshot,
                                              const std::vector<std::wstring>& favorites);

// Alterna una impresora en la lista de acceso rápido y devuelve la lista nueva.
std::vector<std::wstring> TogglePinned(std::vector<std::wstring> favorites, const std::wstring& printer);
bool IsPinned(const std::vector<std::wstring>& favorites, const std::wstring& printer);

// Resumen para la barra de estado.
struct QueueSummary {
    unsigned total = 0;
    unsigned printing = 0;
    unsigned paused = 0;
    unsigned held = 0;  // retenidos automáticamente (no cuentan como "en pausa")
    unsigned errors = 0;
};
QueueSummary Summarize(const std::vector<const core::JobInfo*>& jobs);
std::wstring SummaryText(const QueueSummary& summary);

}  // namespace cola::app
