// Implementación de RowBuilder (ver RowBuilder.h): textos de estado, filtros,
// orden de la cola, columnas, filas de la cola y del historial, acceso rápido
// a impresoras y totales para la barra de estado. Funciones puras: no tocan
// ventanas ni el spooler, así que se pueden llamar desde cualquier hilo
// (en la app, siempre desde el hilo de la UI).
#include "app/RowBuilder.h"

#include <algorithm>
#include <unordered_map>

#include "app/JobSheet.h"
#include "core/JobStatus.h"
#include "i18n/Strings.h"
#include "ui/Style.h"
#include "util/Format.h"
#include "util/Strings.h"

namespace cola::app {

using core::JobState;
using i18n::S;
using i18n::Tr;

std::wstring StateText(JobState state) {
    switch (state) {
        case JobState::Queued: return Tr(S::StateQueued);
        case JobState::Spooling: return Tr(S::StateSpooling);
        case JobState::Printing: return Tr(S::StatePrinting);
        case JobState::Paused: return Tr(S::StatePaused);
        case JobState::Error: return Tr(S::StateError);
        case JobState::Offline: return Tr(S::StateOffline);
        case JobState::Deleting: return Tr(S::StateDeleting);
        case JobState::Printed: return Tr(S::StatePrinted);
        case JobState::Restarting: return Tr(S::StateRestarting);
        default: return Tr(S::Unknown);
    }
}

// "Retenido" tiene prioridad sobre el estado del spooler (que sería "En
// pausa"): así el operador distingue lo que pausó la retención automática
// (D17) de lo que pausó una persona.
std::wstring JobStatusText(const core::JobInfo& job) {
    return job.heldByApp ? std::wstring(Tr(S::StateHeld)) : StateText(job.state);
}

std::wstring OutcomeText(config::JobOutcome outcome) {
    switch (outcome) {
        case config::JobOutcome::Printed: return Tr(S::OutcomePrinted);
        case config::JobOutcome::Cancelled: return Tr(S::OutcomeCancelled);
        case config::JobOutcome::Failed: return Tr(S::OutcomeFailed);
        // Removed (y cualquier valor desconocido de un historial futuro):
        // salió de la cola sin que se sepa si se imprimió (D13/D47).
        default: return Tr(S::OutcomeRemoved);
    }
}

std::wstring ProblemText(core::PrinterProblem problem) {
    using namespace core;
    switch (problem) {
        case ProblemOffline: return Tr(S::ProblemOffline);
        case ProblemPaperJam: return Tr(S::ProblemPaperJam);
        case ProblemPaperOut: return Tr(S::ProblemPaperOut);
        case ProblemTonerLow: return Tr(S::ProblemTonerLow);
        case ProblemNoToner: return Tr(S::ProblemNoToner);
        case ProblemDoorOpen: return Tr(S::ProblemDoorOpen);
        case ProblemUserIntervention: return Tr(S::ProblemUserIntervention);
        case ProblemError: return Tr(S::ProblemError);
        case ProblemPaused: return Tr(S::ProblemPaused);
        case ProblemNotAvailable: return Tr(S::ProblemNotAvailable);
        case ProblemOutputBinFull: return Tr(S::ProblemOutputBinFull);
        case ProblemPaperProblem: return Tr(S::ProblemPaperProblem);
        case ProblemOutOfMemory: return Tr(S::ProblemOutOfMemory);
        default: return L"";
    }
}

// Sin problema = neutro; pausa y tóner bajo = advertencia (amarillo: la
// impresora puede seguir, y la pausa es lo normal con D18); el resto = peligro.
ui::Tone ToneForProblem(core::PrinterProblem problem) {
    using namespace core;
    if (problem == ProblemNone) return ui::Tone::Neutral;
    if (problem == ProblemPaused || problem == ProblemTonerLow) return ui::Tone::Warning;
    return ui::Tone::Danger;
}

std::wstring PagesText(const core::JobInfo& job) {
    // Mientras se imprime y el spooler informa avance: "impresas de total".
    if (job.state == JobState::Printing && job.pagesPrinted > 0 && job.totalPages > 0)
        return util::Format(Tr(S::PagesProgress), job.pagesPrinted, job.totalPages);
    if (job.totalPages == 0) return Tr(S::PagesUnknown);
    if (job.totalPages == 1) return Tr(S::PagesOne);
    return util::Format(Tr(S::PagesCount), job.totalPages);
}

namespace {
// Nombre visible de una impresora ("\\SRV\LAIMI 1" -> "LAIMI 1"), reutilizando
// la lógica de PrinterInfo::DisplayName con un PrinterInfo temporal.
std::wstring PrinterShortName(const std::wstring& name) {
    core::PrinterInfo p;
    p.name = name;
    return p.DisplayName();
}

// true si cada palabra del filtro (separadas por espacios) aparece en algún
// lugar de `haystack`, sin distinguir mayúsculas. Los espacios dobles generan
// palabras vacías, que se saltan.
bool AllWordsMatch(const std::wstring& haystack, const std::wstring& filter) {
    for (const auto& word : util::Split(util::Trim(filter), L' ')) {
        if (word.empty()) continue;
        if (!util::ContainsInsensitive(haystack, word)) return false;
    }
    return true;
}

// Color de la fila de un registro del historial: verde si se imprimió, rojo
// si falló, neutro si se canceló o se retiró.
ui::Tone ToneForOutcome(config::JobOutcome outcome) {
    switch (outcome) {
        case config::JobOutcome::Printed: return ui::Tone::Success;
        case config::JobOutcome::Failed: return ui::Tone::Danger;
        default: return ui::Tone::Neutral;
    }
}

// Ícono (carácter de la fuente de íconos, ui::glyph) que se dibuja junto a la
// fila según el estado; 0 = sin ícono (En cola, Reiniciando...).
wchar_t GlyphForState(JobState state) {
    switch (state) {
        case JobState::Printing: return ui::glyph::Printer;
        case JobState::Paused: return ui::glyph::Pause;
        case JobState::Error: return ui::glyph::Error;
        case JobState::Offline: return ui::glyph::Warning;
        case JobState::Printed: return ui::glyph::Completed;
        case JobState::Deleting: return ui::glyph::Cancel;
        case JobState::Spooling: return ui::glyph::Clock;
        default: return 0;
    }
}
}  // namespace

bool JobMatchesFilter(const core::JobInfo& job, const std::wstring& filter, const std::wstring& carnet) {
    if (util::Trim(filter).empty()) return true;
    // Todos los campos buscables unidos por espacios: así una palabra puede
    // estar en cualquiera y varias palabras pueden estar en campos distintos
    // ("ana pc07"). Incluye el texto de estado traducido y el del spooler.
    std::wstring haystack = job.document + L" " + job.user + L" " + carnet + L" " + job.machine + L" " +
                            PrinterShortName(job.printer) + L" " + JobStatusText(job) + L" " + job.statusText;
    return AllWordsMatch(haystack, filter);
}

bool HistoryMatchesFilter(const config::HistoryEntry& e, const std::wstring& filter) {
    if (util::Trim(filter).empty()) return true;
    std::wstring haystack = e.document + L" " + e.user + L" " + e.carnet + L" " + e.machine + L" " +
                            PrinterShortName(e.printer) + L" " +
                            OutcomeText(e.outcome);
    return AllWordsMatch(haystack, filter);
}

// El spooler devuelve el equipo de origen como "\\NOMBRE"; se quitan todas
// las barras iniciales (pueden ser una, dos o ninguna).
std::wstring MachineName(const std::wstring& machine) {
    std::wstring name = machine;
    while (!name.empty() && name.front() == L'\\') name.erase(name.begin());
    return name;
}

// Ids estables guardados en config.json (sortBy). No traducir ni cambiar:
// un id distinto haría que los config.json existentes vuelvan a "queue".
const wchar_t* SortById(SortBy sort) {
    switch (sort) {
        case SortBy::Machine: return L"machine";
        case SortBy::Time: return L"time";
        case SortBy::User: return L"user";
        case SortBy::Carnet: return L"carnet";
        case SortBy::Document: return L"document";
        case SortBy::Status: return L"status";
        case SortBy::Size: return L"size";
        case SortBy::Pages: return L"pages";
        case SortBy::Printer: return L"printer";
        case SortBy::Paper: return L"paper";
        case SortBy::Orientation: return L"orientation";
        case SortBy::Duplex: return L"duplex";
        case SortBy::Binding: return L"binding";
        case SortBy::Source: return L"source";
        default: return L"queue";
    }
}

SortBy SortByFromId(const std::wstring& id) {
    // Búsqueda lineal: son pocos valores y se llama solo al cargar la configuración.
    for (int i = 0; i < kSortCount; ++i) {
        auto sort = static_cast<SortBy>(i);
        if (util::EqualsInsensitive(id, SortById(sort))) return sort;
    }
    return SortBy::Queue;
}

std::wstring SortName(SortBy sort) {
    switch (sort) {
        case SortBy::Machine: return Tr(S::SortMachine);
        case SortBy::Time: return Tr(S::SortTime);
        case SortBy::User: return Tr(S::SortUser);
        case SortBy::Document: return Tr(S::SortDocument);
        case SortBy::Status: return Tr(S::SortStatus);
        case SortBy::Size: return Tr(S::SortSize);
        case SortBy::Pages: return Tr(S::SortPages);
        case SortBy::Printer: return Tr(S::SortPrinter);
        case SortBy::Paper: return Tr(S::SortPaper);
        case SortBy::Orientation: return Tr(S::SortOrientation);
        case SortBy::Duplex: return Tr(S::SortDuplex);
        case SortBy::Binding: return Tr(S::SortBinding);
        case SortBy::Carnet: return Tr(S::SortCarnet);
        case SortBy::Source: return Tr(S::SortSource);
        default: return Tr(S::SortQueue);
    }
}

namespace {
// Orden natural de la cola: impresora, posición y, si empatan, id.
// (Las copias locales de CSR no tienen posición útil y pueden empatar; el id
// hace el orden total y determinista.)
bool BeforeInQueue(const core::JobInfo& a, const core::JobInfo& b) {
    int printer = _wcsicmp(a.printer.c_str(), b.printer.c_str());
    if (printer != 0) return printer < 0;
    if (a.position != b.position) return a.position < b.position;
    return a.id < b.id;
}

// Compara por el criterio elegido. 0 = empate (se desempata con la cola).
// Devuelve <0, 0 o >0 (estilo strcmp). snapshot se usa para buscar la
// impresora de cada trabajo (nombres de papel/bandeja del controlador, D34).
// Los textos se comparan sin distinguir mayúsculas (_wcsicmp), igual que el
// usuario los ve; las columnas calculadas se comparan por el valor mostrado.
int CompareBy(SortBy sort, const core::JobInfo& a, const core::JobInfo& b, const FactsLookup& facts,
              const CarnetLookup& carnets, const core::QueueSnapshot& snapshot) {
    auto text = [](const std::wstring& x, const std::wstring& y) { return _wcsicmp(x.c_str(), y.c_str()); };
    auto number = [](std::uint64_t x, std::uint64_t y) { return x < y ? -1 : (x > y ? 1 : 0); };
    switch (sort) {
        // Páginas a imprimir (páginas x copias); "—" cuenta como 0.
        case SortBy::Pages:
            return number(TotalPrintPages(a, facts ? facts(a) : nullptr).value,
                          TotalPrintPages(b, facts ? facts(b) : nullptr).value);
        case SortBy::Printer: return text(PrinterShortName(a.printer), PrinterShortName(b.printer));
        case SortBy::Paper:
        case SortBy::Orientation:
        case SortBy::Duplex:
        case SortBy::Binding:
        case SortBy::Source: {
            // Por el valor mostrado.
            auto value = [&](const core::JobInfo& job) {
                ListSettings s = ResolveListSettings(job, facts ? facts(job) : nullptr, snapshot.FindPrinter(job.printer));
                std::wstring v = sort == SortBy::Paper ? s.paper
                                 : sort == SortBy::Orientation ? s.orientation
                                 : sort == SortBy::Duplex ? s.duplex
                                 : sort == SortBy::Binding ? s.binding
                                                          : s.source;
                return v;
            };
            return text(value(a), value(b));
        }
        case SortBy::Machine: return text(MachineName(a.machine), MachineName(b.machine));
        // Hora de envío (SYSTEMTIME convertido a segundos Unix para comparar).
        case SortBy::Time:
            return number(static_cast<std::uint64_t>(util::ToUnixSeconds(a.submitted)),
                          static_cast<std::uint64_t>(util::ToUnixSeconds(b.submitted)));
        case SortBy::User: return text(a.user, b.user);
        // Carnet del Active Directory (D56); sin consulta disponible, todo
        // empata y queda el orden de la cola. Los "" (sin carnet) van primero.
        case SortBy::Carnet:
            if (!carnets) return 0;
            return text(carnets(a.user), carnets(b.user));
        case SortBy::Document: return text(a.document, b.document);
        case SortBy::Status: return text(JobStatusText(a), JobStatusText(b));
        case SortBy::Size: return number(a.sizeBytes, b.sizeBytes);
        // SortBy::Queue: siempre empate, así decide BeforeInQueue.
        default: return 0;
    }
}
}  // namespace

std::vector<const core::JobInfo*> VisibleJobs(const core::QueueSnapshot& snapshot, const std::wstring& printer,
                                              const std::wstring& filter, SortBy sort, bool descending,
                                              const FactsLookup& facts, const CarnetLookup& carnets) {
    std::vector<const core::JobInfo*> jobs;
    // 1. Filtrar por impresora (salvo en "Todas") y por el texto de búsqueda.
    const bool all = printer == kAllPrinters || printer.empty();
    for (const auto& job : snapshot.jobs) {
        if (!all && job.printer != printer) continue;
        if (!JobMatchesFilter(job, filter, carnets ? carnets(job.user) : std::wstring())) continue;
        jobs.push_back(&job);
    }
    // 2. Ordenar. Al invertir (descending) se invierte también el desempate,
    //    así "Cola" descendente es exactamente la cola al revés.
    std::stable_sort(jobs.begin(), jobs.end(), [&](const core::JobInfo* a, const core::JobInfo* b) {
        int cmp = CompareBy(sort, *a, *b, facts, carnets, snapshot);
        if (descending) cmp = -cmp;
        if (cmp != 0) return cmp < 0;
        // A igualdad de criterio (y siempre en "Cola"), el orden real de impresión.
        return descending ? BeforeInQueue(*b, *a) : BeforeInQueue(*a, *b);
    });
    return jobs;
}

// Orden de preferencia (D24/D31): páginas x copias calculadas por
// PageAccounting; si no, las caras declaradas por el controlador; si no,
// TotalPages x copias del DEVMODE como estimación (nunca como dato fiable).
PrintTotal TotalPrintPages(const core::JobInfo& job, const core::ContentFacts* facts) {
    PrintTotal total;
    const core::PageAccounting acc = core::ComputePageAccounting(job, facts);
    if (acc.printPages.Known()) {
        total.value = acc.printPages.value;
        total.reliability = acc.printPages.reliability;
    } else if (acc.sides.Known()) {
        // Sin conteo de páginas pero con las caras que declaró el controlador (EJL "JI PAGES").
        total.value = acc.sides.value;
        total.reliability = acc.sides.reliability;
    } else if (job.totalPages > 0) {
        // Sin datos suficientes: TotalPages x copias del DEVMODE, siempre como estimación.
        const std::uint32_t copies = job.settings.copies.valid ? static_cast<std::uint32_t>(job.settings.copies.value) : 1;
        total.value = job.totalPages * copies;
        total.reliability = core::Reliability::Estimated;
    }
    return total;
}

// 0 páginas también se muestra como "—": un trabajo vacío o aún sin contar
// no debe aparecer como "0 páginas" en la tabla ni en el historial.
std::wstring TotalPagesText(const PrintTotal& total) {
    if (total.reliability == core::Reliability::Unknown || total.value == 0) return L"—";
    return std::to_wstring(total.value);  // sin marcas: la fiabilidad se ve en la pestaña Configuración
}

std::vector<ui::ListColumn> JobColumns(bool showPrinter) {
    using ui::Align;
    // sortId de la columna = valor numérico del SortBy (ver RowBuilder.h).
    auto id = [](SortBy s) { return static_cast<int>(s); };
    std::vector<ui::ListColumn> columns;
    // Desde Estado en adelante los valores van centrados bajo su título.
    // Anchos en DIP; 0 = columna flexible (Documento ocupa el espacio que
    // sobra, con un mínimo, D40). "pill" dibuja el valor como insignia con
    // el tono de la fila (Estado); "énfasis" lo dibuja en semibold (Documento).
    // El orden de estas líneas debe coincidir con el de las celdas de MakeJobRow.
    // Nunca se ocultan: si no caben, la tabla muestra la barra horizontal.
    //               texto                 ancho alineación     pill   énfasis orden                     id (ancho y orden guardados)
    columns.push_back({Tr(S::ColNumber), 30, Align::Right, false, false, id(SortBy::Queue), L"number"});
    columns.push_back({Tr(S::ColDocument), 0, Align::Left, false, true, id(SortBy::Document), L"document"});
    columns.push_back({Tr(S::ColStatus), 104, Align::Center, true, false, id(SortBy::Status), L"status"});
    columns.push_back({Tr(S::ColUser), 100, Align::Center, false, false, id(SortBy::User), L"user"});
    columns.push_back({Tr(S::ColCarnet), 96, Align::Center, false, false, id(SortBy::Carnet), L"carnet"});
    columns.push_back({Tr(S::ColMachine), 100, Align::Center, false, false, id(SortBy::Machine), L"machine"});
    if (showPrinter) columns.push_back({Tr(S::ColPrinter), 120, Align::Center, false, false, id(SortBy::Printer), L"printer"});
    columns.push_back({Tr(S::ColPages), 78, Align::Center, false, false, id(SortBy::Pages), L"pages"});
    columns.push_back({Tr(S::ColPaper), 84, Align::Center, false, false, id(SortBy::Paper), L"paper"});
    columns.push_back({Tr(S::ColOrientation), 96, Align::Center, false, false, id(SortBy::Orientation), L"orientation"});
    columns.push_back({Tr(S::ColDuplex), 96, Align::Center, false, false, id(SortBy::Duplex), L"duplex"});
    columns.push_back({Tr(S::ColBinding), 110, Align::Center, false, false, id(SortBy::Binding), L"binding"});
    columns.push_back({Tr(S::ColSource), 130, Align::Center, false, false, id(SortBy::Source), L"source"});
    columns.push_back({Tr(S::ColSize), 72, Align::Center, false, false, id(SortBy::Size), L"size"});
    columns.push_back({Tr(S::ColSubmitted), 124, Align::Center, false, false, id(SortBy::Time), L"submitted"});
    return columns;
}

std::vector<ui::ListColumn> HistoryColumns() {
    using ui::Align;
    std::vector<ui::ListColumn> columns;
    // Las mismas columnas que la cola (con Impresora), con el resultado en lugar
    // del estado y al final el momento en que el trabajo salió de la cola.
    // sortId -1: el historial no se ordena con clic en el título (va del más
    // reciente al más antiguo). Orden de celdas = el de MakeHistoryRow.
    columns.push_back({Tr(S::ColNumber), 30, Align::Right, false, false, -1, L"history.number"});
    columns.push_back({Tr(S::ColDocument), 0, Align::Left, false, true, -1, L"history.document"});
    columns.push_back({Tr(S::ColResult), 104, Align::Center, true, false, -1, L"history.result"});
    columns.push_back({Tr(S::ColUser), 100, Align::Center, false, false, -1, L"history.user"});
    columns.push_back({Tr(S::ColCarnet), 96, Align::Center, false, false, -1, L"history.carnet"});
    columns.push_back({Tr(S::ColMachine), 100, Align::Center, false, false, -1, L"history.machine"});
    columns.push_back({Tr(S::ColPrinter), 120, Align::Center, false, false, -1, L"history.printer"});
    columns.push_back({Tr(S::ColPages), 78, Align::Center, false, false, -1, L"history.pages"});
    columns.push_back({Tr(S::ColPaper), 84, Align::Center, false, false, -1, L"history.paper"});
    columns.push_back({Tr(S::ColOrientation), 96, Align::Center, false, false, -1, L"history.orientation"});
    columns.push_back({Tr(S::ColDuplex), 96, Align::Center, false, false, -1, L"history.duplex"});
    columns.push_back({Tr(S::ColBinding), 110, Align::Center, false, false, -1, L"history.binding"});
    columns.push_back({Tr(S::ColSource), 130, Align::Center, false, false, -1, L"history.source"});
    columns.push_back({Tr(S::ColSize), 72, Align::Center, false, false, -1, L"history.size"});
    columns.push_back({Tr(S::ColSubmitted), 124, Align::Center, false, false, -1, L"history.submitted"});
    columns.push_back({Tr(S::ColFinished), 124, Align::Center, false, false, -1, L"history.time"});
    return columns;
}

// Una sola lista con las columnas de la cola (incluida Impresora) seguidas de
// las del historial: es la forma en que se guarda columnOrder (D38).
std::vector<std::wstring> DefaultColumnOrder() {
    std::vector<std::wstring> ids;
    for (const auto& c : JobColumns(true)) ids.push_back(c.id);
    for (const auto& c : HistoryColumns()) ids.push_back(c.id);
    return ids;
}

// Normaliza lo guardado: quita ids desconocidos o repetidos y agrega las
// columnas nuevas de otra versión detrás de la que las precede (D38).
std::vector<std::wstring> EffectiveColumnOrder(const std::vector<std::wstring>& saved) {
    return ui::NormalizeColumnOrder(saved, DefaultColumnOrder());
}

std::vector<std::wstring> SaveColumnOrder(const std::vector<std::wstring>& saved, const std::vector<std::wstring>& view) {
    // MergeColumnOrder coloca las columnas de la vista en el orden nuevo pero
    // solo en los lugares que ocupaban, así las que no se ven (p. ej.
    // Impresora fuera de "Todas") conservan su posición.
    // Si el resultado es el predeterminado se guarda vacío (así una versión
    // futura con otro predeterminado lo aplica).
    auto order = ui::MergeColumnOrder(EffectiveColumnOrder(saved), view);
    if (order == DefaultColumnOrder()) order.clear();
    return order;
}

std::pair<SortBy, bool> NextSort(SortBy current, bool descending, SortBy clicked) {
    if (clicked == current) return {current, !descending};
    return {clicked, false};
}

ui::ListRow MakeJobRow(const core::JobInfo& job, bool showPrinter, const core::ContentFacts* facts,
                       const core::PrinterInfo* printer, const std::wstring& carnet) {
    ui::ListRow row;
    // Clave estable del trabajo (impresora + id): la lista conserva la
    // selección y el foco entre instantáneas comparando claves.
    row.key = job.Key();
    // Celdas en el mismo orden que JobColumns. Posición 0 (sin posición) = vacía.
    row.cells.push_back(job.position ? std::to_wstring(job.position) : L"");
    row.cells.push_back(job.document.empty() ? Tr(S::Unknown) : job.document);
    row.cells.push_back(JobStatusText(job));
    row.cells.push_back(job.user);
    row.cells.push_back(carnet.empty() ? std::wstring(L"—") : carnet);
    row.cells.push_back(MachineName(job.machine));
    if (showPrinter) row.cells.push_back(PrinterShortName(job.printer));
    row.cells.push_back(TotalPagesText(TotalPrintPages(job, facts)));
    // Papel, orientación, doble cara, presentación y origen salen de la
    // misma resolución que la ficha "Configuración" (JobSheet), con la
    // prioridad datos > DEVMODE (D29) y los nombres del controlador (D34).
    const ListSettings settings = ResolveListSettings(job, facts, printer);
    row.cells.push_back(settings.paper);
    row.cells.push_back(settings.orientation);
    row.cells.push_back(settings.duplex);
    row.cells.push_back(settings.binding);
    row.cells.push_back(settings.source);
    row.cells.push_back(util::FormatBytes(job.sizeBytes));
    row.cells.push_back(util::FormatDateTime(job.submitted));  // fecha y hora completas
    // Color de la fila: los retenidos por la app tienen un tono propio
    // (Special) para distinguirlos de una pausa manual.
    row.tone = job.heldByApp ? ui::Tone::Special : ui::ToneForState(job.state);
    row.glyph = GlyphForState(job.state);
    return row;
}

// La hora sola no basta (dos trabajos pueden terminar en el mismo segundo):
// se le agrega un ordinal que los distingue (ver HistoryOrdinals).
std::wstring HistoryKey(const config::HistoryEntry& entry, size_t ordinal) {
    return util::Format(L"h%lld_%zu", static_cast<long long>(entry.timeUnix), ordinal);
}

// M1: antes el ordinal era el índice del registro en la lista, y como
// History::Add inserta al principio, cada trabajo terminado cambiaba todas las
// claves y la lista perdía la selección y el foco. Contando desde el final
// (los más viejos), entre los de la misma hora, un registro nuevo al principio
// no cambia el ordinal de ninguno de los anteriores (tampoco el recorte de los
// más viejos al llegar al límite, salvo empate en el mismo segundo).
std::vector<size_t> HistoryOrdinals(const std::vector<config::HistoryEntry>& entries) {
    std::vector<size_t> ordinals(entries.size(), 0);
    std::unordered_map<std::int64_t, size_t> seen;  // hora -> registros más viejos con esa hora
    for (size_t i = entries.size(); i-- > 0;) ordinals[i] = seen[entries[i].timeUnix]++;
    return ordinals;
}

config::HistoryEntry MakeHistoryEntry(const core::JobInfo& job, config::JobOutcome outcome, std::int64_t timeUnix,
                                      const core::ContentFacts* facts, const core::PrinterInfo* printer,
                                      const std::wstring& carnet) {
    config::HistoryEntry entry;
    entry.timeUnix = timeUnix;  // momento en que salió de la cola (Finalizado)
    entry.printer = job.printer;
    entry.document = job.document;
    entry.user = job.user;
    // carnet puede venir vacío si el directorio todavía no respondió: se
    // completa después, cuando llegue (D56).
    entry.carnet = carnet;
    entry.machine = job.machine;
    // Páginas del spooler (para estadísticas y registros viejos). Se toma el
    // mayor entre TotalPages y las ya impresas: TotalPages puede bajar
    // mientras se imprime (GUIA_IA §6).
    entry.pages = std::max(job.totalPages, job.pagesPrinted);
    entry.sizeBytes = job.sizeBytes;
    entry.outcome = outcome;
    // Los mismos valores que MakeJobRow mostraba en la cola.
    // Se guardan como texto ya traducido (D41): el historial muestra lo que
    // se veía, aunque después cambie el idioma o la lógica de cálculo.
    entry.position = job.position;
    entry.printPages = TotalPagesText(TotalPrintPages(job, facts));
    const ListSettings settings = ResolveListSettings(job, facts, printer);
    entry.paper = settings.paper;
    entry.orientation = settings.orientation;
    entry.duplex = settings.duplex;
    entry.binding = settings.binding;
    entry.source = settings.source;
    // wYear == 0 = el spooler no informó la hora de envío: queda 0 ("—").
    if (job.submitted.wYear) entry.submittedUnix = util::ToUnixSeconds(job.submitted);
    return entry;
}

ui::ListRow MakeHistoryRow(const config::HistoryEntry& e, size_t index) {
    // Registros de versiones anteriores: sin esos datos se muestra "—".
    auto orDash = [](const std::wstring& text) { return text.empty() ? std::wstring(L"—") : text; };
    ui::ListRow row;
    row.key = HistoryKey(e, index);
    row.cells.push_back(e.position ? std::to_wstring(e.position) : L"");
    row.cells.push_back(e.document.empty() ? Tr(S::Unknown) : e.document);
    row.cells.push_back(OutcomeText(e.outcome));
    row.cells.push_back(e.user);
    row.cells.push_back(orDash(e.carnet));
    row.cells.push_back(MachineName(e.machine));
    row.cells.push_back(PrinterShortName(e.printer));
    // Páginas: el texto guardado (páginas x copias, D41); en registros
    // anteriores a la 0.5.0, el TotalPages del spooler; si no hay nada, "—".
    row.cells.push_back(!e.printPages.empty() ? e.printPages : e.pages ? std::to_wstring(e.pages) : L"—");
    row.cells.push_back(orDash(e.paper));
    row.cells.push_back(orDash(e.orientation));
    row.cells.push_back(orDash(e.duplex));
    row.cells.push_back(orDash(e.binding));
    row.cells.push_back(orDash(e.source));
    row.cells.push_back(util::FormatBytes(e.sizeBytes));
    row.cells.push_back(e.submittedUnix ? util::FormatDateTime(util::FromUnixSeconds(e.submittedUnix)) : L"—");
    row.cells.push_back(util::FormatDateTime(util::FromUnixSeconds(e.timeUnix)));
    row.tone = ToneForOutcome(e.outcome);
    // Ícono: impreso = ✓, falló = error, cancelado/retirado = cancelar.
    row.glyph = e.outcome == config::JobOutcome::Printed ? ui::glyph::Completed
                : e.outcome == config::JobOutcome::Failed ? ui::glyph::Error
                                                          : ui::glyph::Cancel;
    return row;
}

bool IsPinned(const std::vector<std::wstring>& favorites, const std::wstring& printer) {
    return std::any_of(favorites.begin(), favorites.end(),
                       [&](const std::wstring& p) { return util::EqualsInsensitive(p, printer); });
}

std::vector<std::wstring> TogglePinned(std::vector<std::wstring> favorites, const std::wstring& printer) {
    if (printer.empty() || printer == kAllPrinters) return favorites;
    // Fijada: se quitan todas sus apariciones (por si config.json tenía
    // duplicados con otras mayúsculas). No fijada: se agrega al final
    // (el orden de la barra es el orden en que se fijaron, D19).
    if (IsPinned(favorites, printer)) {
        favorites.erase(std::remove_if(favorites.begin(), favorites.end(),
                                       [&](const std::wstring& p) { return util::EqualsInsensitive(p, printer); }),
                        favorites.end());
    } else {
        favorites.push_back(printer);
    }
    return favorites;
}

std::vector<ui::PrinterChip> BuildQuickAccess(const core::QueueSnapshot& snapshot,
                                              const std::vector<std::wstring>& favorites) {
    // Cuenta los trabajos de una impresora ("" = todas).
    // `held` recibe cuántos de ellos están retenidos por la app (el chip los
    // muestra aparte).
    auto count = [&snapshot](const std::wstring& printer, unsigned& held) {
        unsigned jobs = 0;
        held = 0;
        for (const auto& job : snapshot.jobs) {
            if (!printer.empty() && job.printer != printer) continue;
            ++jobs;
            if (job.heldByApp) ++held;
        }
        return jobs;
    };

    std::vector<ui::PrinterChip> chips;
    ui::PrinterChip all;
    all.name = kAllPrinters;
    all.label = Tr(S::AllPrinters);
    all.jobs = count(L"", all.held);
    // El chip "Todas" toma el color del problema más grave de cualquier
    // impresora. En core::PrinterProblem un valor menor es más grave
    // (ProblemNone aparte), por eso se busca el mínimo.
    core::PrinterProblem worst = core::ProblemNone;
    for (const auto& p : snapshot.printers) {
        auto problem = core::MostSevereProblem(p.problems);
        if (problem != core::ProblemNone && (worst == core::ProblemNone || problem < worst)) worst = problem;
    }
    all.tone = ToneForProblem(worst);
    chips.push_back(std::move(all));

    // Un chip por impresora fijada, en el orden de la lista (Ctrl+1..9
    // corresponden a estas posiciones, D19).
    for (const auto& name : favorites) {
        const auto* printer = snapshot.FindPrinter(name);
        if (!printer) continue;  // ya no existe en este servidor
        ui::PrinterChip chip;
        chip.name = printer->name;
        chip.label = printer->DisplayName();
        chip.jobs = count(printer->name, chip.held);
        chip.tone = ToneForProblem(core::MostSevereProblem(printer->problems));
        chips.push_back(std::move(chip));
    }
    return chips;
}

bool ShouldPauseJobs(const std::vector<core::JobInfo>& jobs) {
    // Si alguno no está en pausa se pausan todos; si todos están en pausa
    // (o retenidos), el comando los reanuda.
    return std::any_of(jobs.begin(), jobs.end(),
                       [](const core::JobInfo& j) { return j.state != JobState::Paused; });
}

std::vector<std::wstring> PrintersOf(const std::vector<core::JobInfo>& jobs) {
    std::vector<std::wstring> printers;
    for (const auto& job : jobs)
        if (std::find(printers.begin(), printers.end(), job.printer) == printers.end())
            printers.push_back(job.printer);
    return printers;
}

// Mismo criterio que la columna: "—" (desconocido o 0) no suma y se cuenta aparte.
PagesTotal SumPrintPages(const std::vector<const core::JobInfo*>& jobs, const FactsLookup& facts) {
    PagesTotal total;
    for (const auto* job : jobs) {
        ++total.jobs;
        const PrintTotal pages = TotalPrintPages(*job, facts ? facts(*job) : nullptr);
        if (pages.reliability == core::Reliability::Unknown || pages.value == 0) ++total.unknown;
        else total.pages += pages.value;
    }
    return total;
}

PagesTotal SumHistoryPages(const std::vector<const config::HistoryEntry*>& entries) {
    PagesTotal total;
    for (const auto* entry : entries) {
        ++total.jobs;
        const std::uint32_t pages = entry->PrintPagesCount();
        if (pages) total.pages += pages;
        else ++total.unknown;
    }
    return total;
}

// history elige el texto del historial ("N páginas") o el de la cola
// ("N páginas a imprimir").
std::wstring SelectionText(const PagesTotal& total, bool history) {
    if (total.jobs == 0) return L"";
    std::wstring text = util::Format(Tr(history ? S::StatusSelectionHistory : S::StatusSelection), total.jobs,
                                     static_cast<unsigned long long>(total.pages));
    if (total.unknown) text += util::Format(Tr(S::StatusSelectionUnknown), total.unknown);
    return text;
}

QueueSummary Summarize(const std::vector<const core::JobInfo*>& jobs) {
    QueueSummary s;
    for (const auto* job : jobs) {
        ++s.total;
        if (job->state == JobState::Printing) ++s.printing;
        // Un retenido también está en pausa en el spooler, pero se cuenta
        // solo como retenido para no duplicarlo.
        if (job->heldByApp) ++s.held;
        else if (job->state == JobState::Paused) ++s.paused;
        if (job->state == JobState::Error || job->state == JobState::Offline) ++s.errors;
    }
    return s;
}

std::wstring SummaryText(const QueueSummary& s) {
    if (s.total == 0) return Tr(S::StatusNoJobs);
    // Total en singular o plural y luego solo los conteos distintos de cero,
    // separados por " · ".
    std::wstring text = s.total == 1 ? std::wstring(Tr(S::StatusOneJob)) : util::Format(Tr(S::StatusJobs), s.total);
    if (s.printing) text += L" · " + util::Format(Tr(S::StatusPrinting), s.printing);
    if (s.held) text += L" · " + util::Format(Tr(S::StatusHeldCount), s.held);
    if (s.paused) text += L" · " + util::Format(Tr(S::StatusPaused), s.paused);
    if (s.errors) text += L" · " + util::Format(Tr(S::StatusErrors), s.errors);
    return text;
}

}  // namespace cola::app
