#pragma once
// Traduce datos del dominio (trabajos, historial) a filas de la lista y textos
// de la interfaz. Lógica pura (sin ventanas) para poder probarla.
//
// Lo usa MainWindow en el hilo de la UI cada vez que llega una instantánea de
// la cola o cambia el filtro/orden/vista: VisibleJobs elige y ordena los
// trabajos, MakeJobRow arma cada fila de ui::JobListView, MakeHistoryEntry
// congela la fila de un trabajo que sale de la cola para guardarla en el
// historial (D41), y las funciones Sum*/Summarize alimentan la barra de
// estado (D44). Las columnas de la tabla (JobColumns/HistoryColumns) y el
// orden de columnas guardado (D36/D38) también se definen aquí.
// Pruebas: tests/test_core_input_ui.cpp, test_print_settings.cpp,
// test_history_export.cpp y test_directory.cpp.

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
// (Se guarda así en config.json; "" también se interpreta como "todas".)
inline constexpr wchar_t kAllPrinters[] = L"*";

// Criterio para ordenar la cola. "Queue" es el orden real de impresión
// (impresora + posición); los demás son vistas para buscar trabajos.
// Pages = páginas que se van a imprimir en total (páginas x copias).
// Los valores se guardan por su id de texto (SortById), no por su número.
// Ordenar es solo una vista (D20): no cambia la prioridad en el spooler.
// El valor numérico también es el sortId de la columna en JobColumns, así un
// clic en el título se traduce directamente a un SortBy (D30).
// Count_ no es un criterio: marca la cantidad de valores (kSortCount).
enum class SortBy { Queue, Machine, Time, User, Carnet, Document, Status, Size, Pages, Printer, Paper, Orientation,
                    Duplex, Binding, Source, Count_ };

// Cantidad de criterios de orden válidos (para recorrerlos).
inline constexpr int kSortCount = static_cast<int>(SortBy::Count_);

// Identificador estable para config.json ("queue", "machine", "time"...).
const wchar_t* SortById(SortBy sort);
// Inverso de SortById (sin distinguir mayúsculas); un id desconocido o vacío
// (config.json viejo o editado a mano) devuelve SortBy::Queue.
SortBy SortByFromId(const std::wstring& id);
// Texto traducido para el menú y la barra de estado.
std::wstring SortName(SortBy sort);

// Texto traducido de un estado genérico del trabajo (En cola, Imprimiendo...).
std::wstring StateText(core::JobState state);
// Estado de un trabajo concreto ("Retenido" si lo pausó la retención automática).
std::wstring JobStatusText(const core::JobInfo& job);
// Texto del resultado de un registro del historial (Impreso, Cancelado...).
std::wstring OutcomeText(config::JobOutcome outcome);
// Texto de un problema de impresora (sin conexión, atasco...); "" si no hay.
std::wstring ProblemText(core::PrinterProblem problem);
// Color con el que se muestra un problema de impresora.
ui::Tone ToneForProblem(core::PrinterProblem problem);
// Texto de páginas según el spooler ("3 de 10" mientras se imprime, "1 página",
// "N páginas" o "desconocido"). Usa TotalPages, no páginas x copias: para la
// columna Páginas se usa TotalPrintPages.
std::wstring PagesText(const core::JobInfo& job);

// Páginas que se van a imprimir en total: páginas x copias
// (PageAccounting::printPages). Con 3 páginas y 3 copias, 9. Si no hay datos
// del trabajo se estima con TotalPages x dmCopies y se marca como estimado.
struct PrintTotal {
    std::uint32_t value = 0;  // páginas a imprimir (0 si no se sabe)
    core::Reliability reliability = core::Reliability::Unknown;  // Fiable / Estimado / No determinable (D24)
};
// Calcula el total con core::ComputePageAccounting. facts = datos analizados
// del trabajo (JobFactsService); nullptr si todavía no se leyeron.
PrintTotal TotalPrintPages(const core::JobInfo& job, const core::ContentFacts* facts);
// "9" o "—" (no determinable). La fiabilidad se ve en la pestaña Configuración.
std::wstring TotalPagesText(const PrintTotal& total);

// Datos analizados de un trabajo (puede devolver nullptr).
// En la app es una lambda sobre preview::JobFactsService::Find; en las
// pruebas, un mapa. Un FactsLookup vacío equivale a "sin datos".
using FactsLookup = std::function<const core::ContentFacts*(const core::JobInfo&)>;
// Carnet del usuario de un trabajo según el Active Directory ("" si no se conoce, D56).
using CarnetLookup = std::function<std::wstring(const std::wstring& user)>;

// Columnas de la tabla. En la cola, sortId es el SortBy de la columna.
// showPrinter: agrega la columna Impresora (solo en la vista "Todas").
// El orden aquí es el predeterminado; el operador puede reordenarlas (D38) y
// cambiar su ancho (D36), guardados por ListColumn::id.
std::vector<ui::ListColumn> JobColumns(bool showPrinter);
// Columnas del historial (D41): las de la cola en "Todas", con Resultado en
// lugar de Estado y Finalizado al final. No se ordenan con clic (sortId -1).
// Sus ids llevan el prefijo "history." para no chocar con los de la cola.
std::vector<ui::ListColumn> HistoryColumns();

// Orden de las columnas (config.json "columnOrder"): ids de la cola (con
// Impresora) y del historial en el orden predeterminado...
std::vector<std::wstring> DefaultColumnOrder();
// ...el orden completo a usar a partir del guardado...
std::vector<std::wstring> EffectiveColumnOrder(const std::vector<std::wstring>& saved);
// ...y el que se guarda cuando el operador mueve columnas en una vista
// (`view` = ids de esa vista en el orden nuevo). Vacío si quedó el predeterminado.
std::vector<std::wstring> SaveColumnOrder(const std::vector<std::wstring>& saved, const std::vector<std::wstring>& view);

// Clic en el encabezado de una columna, como en la cola de Windows: la misma
// columna invierte el orden; otra columna ordena de forma ascendente.
std::pair<SortBy, bool> NextSort(SortBy current, bool descending, SortBy clicked);

// true si el trabajo coincide con el texto de búsqueda (documento, usuario,
// carnet, equipo, impresora, estado). Varias palabras = todas deben coincidir.
// Búsqueda sin distinguir mayúsculas; un filtro vacío o solo con espacios
// acepta todo. carnet: el del usuario del trabajo (D56), "" si no se conoce.
bool JobMatchesFilter(const core::JobInfo& job, const std::wstring& filter, const std::wstring& carnet = {});
// Igual que JobMatchesFilter para un registro del historial (documento,
// usuario, carnet, equipo, impresora y resultado).
bool HistoryMatchesFilter(const config::HistoryEntry& entry, const std::wstring& filter);

// Trabajos visibles para la impresora elegida ("*" = todas) y el filtro,
// ordenados según el criterio pedido (por defecto, el orden de la cola).
// Devuelve punteros a los JobInfo de `snapshot`: valen mientras esa
// instantánea siga viva (MainWindow la conserva hasta la siguiente).
// El orden es estable y los empates se resuelven con el orden de impresión
// (D20). facts/carnets solo se usan para filtrar u ordenar por esas columnas.
std::vector<const core::JobInfo*> VisibleJobs(const core::QueueSnapshot& snapshot, const std::wstring& printer,
                                              const std::wstring& filter, SortBy sort = SortBy::Queue,
                                              bool descending = false, const FactsLookup& facts = {},
                                              const CarnetLookup& carnets = {});

// Nombre del equipo sin las barras iniciales ("\\\\LAB-PC07" -> "LAB-PC07").
std::wstring MachineName(const std::wstring& machine);

// Celdas en el orden de JobColumns(showPrinter).
// printer (puede ser nullptr) aporta los nombres de bandejas y papeles del controlador.
// carnet: el del usuario según el Active Directory ("" = "—").
ui::ListRow MakeJobRow(const core::JobInfo& job, bool showPrinter, const core::ContentFacts* facts = nullptr,
                       const core::PrinterInfo* printer = nullptr, const std::wstring& carnet = {});
// Registro del historial de un trabajo que salió de la cola, con los mismos
// valores que tenía su fila en la tabla (páginas, papel, orientación, doble
// cara, origen, enviado...). El servidor lo completa quien llama.
config::HistoryEntry MakeHistoryEntry(const core::JobInfo& job, config::JobOutcome outcome, std::int64_t timeUnix,
                                      const core::ContentFacts* facts = nullptr,
                                      const core::PrinterInfo* printer = nullptr, const std::wstring& carnet = {});
// Celdas en el orden de HistoryColumns().
// index: posición del registro en el historial (forma parte de la clave).
ui::ListRow MakeHistoryRow(const config::HistoryEntry& entry, size_t index);
// Clave única de la fila de un registro ("h<horaUnix>_<índice>"): sirve para
// conservar la selección de la lista entre refrescos del historial.
std::wstring HistoryKey(const config::HistoryEntry& entry, size_t index);

// Estado al que debe llevarse un conjunto de trabajos al alternar la pausa:
// true = pausar (hay alguno corriendo), false = reanudar (todos están en pausa).
// Es el criterio de Ctrl+P y del botón de pausa de la cabecera (D37).
bool ShouldPauseJobs(const std::vector<core::JobInfo>& jobs);

// Impresoras distintas a las que pertenecen esos trabajos.
// Conserva el orden de primera aparición; compara el nombre exacto.
std::vector<std::wstring> PrintersOf(const std::vector<core::JobInfo>& jobs);

// Construye los botones del acceso rápido a impresoras a partir de las
// impresoras fijadas por el operador. Devuelve solo las que existen en el
// servidor actual, en el orden en que están fijadas, con su cuenta de trabajos
// y su problema más grave. El primer botón siempre es "Todas".
std::vector<ui::PrinterChip> BuildQuickAccess(const core::QueueSnapshot& snapshot,
                                              const std::vector<std::wstring>& favorites);

// Alterna una impresora en la lista de acceso rápido y devuelve la lista nueva.
// "Todas" ("*") o un nombre vacío no se pueden fijar: se devuelve igual.
std::vector<std::wstring> TogglePinned(std::vector<std::wstring> favorites, const std::wstring& printer);
// true si la impresora está fijada (sin distinguir mayúsculas).
bool IsPinned(const std::vector<std::wstring>& favorites, const std::wstring& printer);

// Total de páginas de la selección (barra inferior): las mismas que muestra la
// columna "Páginas". `unknown` = trabajos con "—", que no suman.
struct PagesTotal {
    unsigned jobs = 0;        // trabajos/registros seleccionados
    std::uint64_t pages = 0;  // suma de las páginas conocidas
    unsigned unknown = 0;     // cuántos no se pudieron sumar ("—")
};
// Suma la columna Páginas (TotalPrintPages) de los trabajos de la cola.
PagesTotal SumPrintPages(const std::vector<const core::JobInfo*>& jobs, const FactsLookup& facts = {});
// Suma la columna Páginas de registros del historial (HistoryEntry::PrintPagesCount).
PagesTotal SumHistoryPages(const std::vector<const config::HistoryEntry*>& entries);
// "Selección: 2 trabajo(s) · 8 páginas a imprimir (1 sin determinar)"; vacío sin selección.
std::wstring SelectionText(const PagesTotal& total, bool history);

// Resumen para la barra de estado.
struct QueueSummary {
    unsigned total = 0;     // trabajos visibles
    unsigned printing = 0;  // imprimiéndose
    unsigned paused = 0;    // en pausa por un operador (o por otra causa)
    unsigned held = 0;  // retenidos automáticamente (no cuentan como "en pausa")
    unsigned errors = 0;    // con error o impresora sin conexión
};
// Cuenta los estados de los trabajos visibles.
QueueSummary Summarize(const std::vector<const core::JobInfo*>& jobs);
// "5 trabajos · 1 imprimiendo · 3 retenidos · ..." (solo los conteos > 0).
std::wstring SummaryText(const QueueSummary& summary);

}  // namespace cola::app
