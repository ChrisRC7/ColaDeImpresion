#include "app/RowBuilder.h"

#include <algorithm>

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

std::wstring JobStatusText(const core::JobInfo& job) {
    return job.heldByApp ? std::wstring(Tr(S::StateHeld)) : StateText(job.state);
}

std::wstring OutcomeText(config::JobOutcome outcome) {
    switch (outcome) {
        case config::JobOutcome::Printed: return Tr(S::OutcomePrinted);
        case config::JobOutcome::Cancelled: return Tr(S::OutcomeCancelled);
        case config::JobOutcome::Failed: return Tr(S::OutcomeFailed);
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

ui::Tone ToneForProblem(core::PrinterProblem problem) {
    using namespace core;
    if (problem == ProblemNone) return ui::Tone::Neutral;
    if (problem == ProblemPaused || problem == ProblemTonerLow) return ui::Tone::Warning;
    return ui::Tone::Danger;
}

std::wstring PagesText(const core::JobInfo& job) {
    if (job.state == JobState::Printing && job.pagesPrinted > 0 && job.totalPages > 0)
        return util::Format(Tr(S::PagesProgress), job.pagesPrinted, job.totalPages);
    if (job.totalPages == 0) return Tr(S::PagesUnknown);
    if (job.totalPages == 1) return Tr(S::PagesOne);
    return util::Format(Tr(S::PagesCount), job.totalPages);
}

namespace {
std::wstring PrinterShortName(const std::wstring& name) {
    core::PrinterInfo p;
    p.name = name;
    return p.DisplayName();
}

bool AllWordsMatch(const std::wstring& haystack, const std::wstring& filter) {
    for (const auto& word : util::Split(util::Trim(filter), L' ')) {
        if (word.empty()) continue;
        if (!util::ContainsInsensitive(haystack, word)) return false;
    }
    return true;
}

ui::Tone ToneForOutcome(config::JobOutcome outcome) {
    switch (outcome) {
        case config::JobOutcome::Printed: return ui::Tone::Success;
        case config::JobOutcome::Failed: return ui::Tone::Danger;
        default: return ui::Tone::Neutral;
    }
}

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

bool JobMatchesFilter(const core::JobInfo& job, const std::wstring& filter) {
    if (util::Trim(filter).empty()) return true;
    std::wstring haystack = job.document + L" " + job.user + L" " + job.machine + L" " +
                            PrinterShortName(job.printer) + L" " + JobStatusText(job) + L" " + job.statusText;
    return AllWordsMatch(haystack, filter);
}

bool HistoryMatchesFilter(const config::HistoryEntry& e, const std::wstring& filter) {
    if (util::Trim(filter).empty()) return true;
    std::wstring haystack = e.document + L" " + e.user + L" " + e.machine + L" " + PrinterShortName(e.printer) + L" " +
                            OutcomeText(e.outcome);
    return AllWordsMatch(haystack, filter);
}

std::wstring MachineName(const std::wstring& machine) {
    std::wstring name = machine;
    while (!name.empty() && name.front() == L'\\') name.erase(name.begin());
    return name;
}

const wchar_t* SortById(SortBy sort) {
    switch (sort) {
        case SortBy::Machine: return L"machine";
        case SortBy::Time: return L"time";
        case SortBy::User: return L"user";
        case SortBy::Document: return L"document";
        case SortBy::Status: return L"status";
        case SortBy::Size: return L"size";
        case SortBy::Pages: return L"pages";
        case SortBy::Printer: return L"printer";
        case SortBy::Paper: return L"paper";
        case SortBy::Orientation: return L"orientation";
        case SortBy::Duplex: return L"duplex";
        case SortBy::Source: return L"source";
        default: return L"queue";
    }
}

SortBy SortByFromId(const std::wstring& id) {
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
        case SortBy::Source: return Tr(S::SortSource);
        default: return Tr(S::SortQueue);
    }
}

namespace {
// Orden natural de la cola: impresora, posición y, si empatan, id.
bool BeforeInQueue(const core::JobInfo& a, const core::JobInfo& b) {
    int printer = _wcsicmp(a.printer.c_str(), b.printer.c_str());
    if (printer != 0) return printer < 0;
    if (a.position != b.position) return a.position < b.position;
    return a.id < b.id;
}

// Compara por el criterio elegido. 0 = empate (se desempata con la cola).
int CompareBy(SortBy sort, const core::JobInfo& a, const core::JobInfo& b, const FactsLookup& facts) {
    auto text = [](const std::wstring& x, const std::wstring& y) { return _wcsicmp(x.c_str(), y.c_str()); };
    auto number = [](std::uint64_t x, std::uint64_t y) { return x < y ? -1 : (x > y ? 1 : 0); };
    switch (sort) {
        case SortBy::Pages:
            return number(TotalPrintPages(a, facts ? facts(a) : nullptr).value,
                          TotalPrintPages(b, facts ? facts(b) : nullptr).value);
        case SortBy::Printer: return text(PrinterShortName(a.printer), PrinterShortName(b.printer));
        case SortBy::Paper:
        case SortBy::Orientation:
        case SortBy::Duplex:
        case SortBy::Source: {
            // Por el valor mostrado, sin la marca de estimado.
            auto value = [&](const core::JobInfo& job) {
                ListSettings s = ResolveListSettings(job, facts ? facts(job) : nullptr);
                std::wstring v = sort == SortBy::Paper ? s.paper
                                 : sort == SortBy::Orientation ? s.orientation
                                 : sort == SortBy::Duplex ? s.duplex
                                                          : s.source;
                if (!v.empty() && v[0] == L'≈') v.erase(0, 1);
                return v;
            };
            return text(value(a), value(b));
        }
        case SortBy::Machine: return text(MachineName(a.machine), MachineName(b.machine));
        case SortBy::Time:
            return number(static_cast<std::uint64_t>(util::ToUnixSeconds(a.submitted)),
                          static_cast<std::uint64_t>(util::ToUnixSeconds(b.submitted)));
        case SortBy::User: return text(a.user, b.user);
        case SortBy::Document: return text(a.document, b.document);
        case SortBy::Status: return text(JobStatusText(a), JobStatusText(b));
        case SortBy::Size: return number(a.sizeBytes, b.sizeBytes);
        default: return 0;
    }
}
}  // namespace

std::vector<const core::JobInfo*> VisibleJobs(const core::QueueSnapshot& snapshot, const std::wstring& printer,
                                              const std::wstring& filter, SortBy sort, bool descending,
                                              const FactsLookup& facts) {
    std::vector<const core::JobInfo*> jobs;
    const bool all = printer == kAllPrinters || printer.empty();
    for (const auto& job : snapshot.jobs) {
        if (!all && job.printer != printer) continue;
        if (!JobMatchesFilter(job, filter)) continue;
        jobs.push_back(&job);
    }
    std::stable_sort(jobs.begin(), jobs.end(), [&](const core::JobInfo* a, const core::JobInfo* b) {
        int cmp = CompareBy(sort, *a, *b, facts);
        if (descending) cmp = -cmp;
        if (cmp != 0) return cmp < 0;
        // A igualdad de criterio (y siempre en "Cola"), el orden real de impresión.
        return descending ? BeforeInQueue(*b, *a) : BeforeInQueue(*a, *b);
    });
    return jobs;
}

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

std::wstring TotalPagesText(const PrintTotal& total) {
    if (total.reliability == core::Reliability::Unknown || total.value == 0) return L"—";
    std::wstring text = std::to_wstring(total.value);
    return total.reliability == core::Reliability::Reliable ? text : L"≈" + text;
}

std::vector<ui::ListColumn> JobColumns(bool showPrinter) {
    using ui::Align;
    auto id = [](SortBy s) { return static_cast<int>(s); };
    std::vector<ui::ListColumn> columns;
    // Desde Estado en adelante los valores van centrados bajo su título.
    // En ventanas angostas se ocultan primero las de número mayor: #, Impresora,
    // Tamaño, Enviado, Origen, Orientación, Doble cara, Papel. Usuario, Equipo
    // (de qué PC se envió) y Páginas se mantienen: son las que más usa el operador.
    //               texto                 ancho alineación     pill   énfasis orden                     ocultar
    columns.push_back({Tr(S::ColNumber), 30, Align::Right, false, false, id(SortBy::Queue), 12});
    columns.push_back({Tr(S::ColDocument), 0, Align::Left, false, true, id(SortBy::Document), 0});
    columns.push_back({Tr(S::ColStatus), 104, Align::Center, true, false, id(SortBy::Status), 0});
    columns.push_back({Tr(S::ColUser), 100, Align::Center, false, false, id(SortBy::User), 2});
    columns.push_back({Tr(S::ColMachine), 100, Align::Center, false, false, id(SortBy::Machine), 3});
    if (showPrinter) columns.push_back({Tr(S::ColPrinter), 120, Align::Center, false, false, id(SortBy::Printer), 11});
    columns.push_back({Tr(S::ColPages), 78, Align::Center, false, false, id(SortBy::Pages), 1});
    columns.push_back({Tr(S::ColPaper), 84, Align::Center, false, false, id(SortBy::Paper), 4});
    columns.push_back({Tr(S::ColOrientation), 96, Align::Center, false, false, id(SortBy::Orientation), 6});
    columns.push_back({Tr(S::ColDuplex), 96, Align::Center, false, false, id(SortBy::Duplex), 5});
    columns.push_back({Tr(S::ColSource), 130, Align::Center, false, false, id(SortBy::Source), 7});
    columns.push_back({Tr(S::ColSize), 72, Align::Center, false, false, id(SortBy::Size), 10});
    columns.push_back({Tr(S::ColSubmitted), 124, Align::Center, false, false, id(SortBy::Time), 9});
    return columns;
}

std::vector<ui::ListColumn> HistoryColumns() {
    using ui::Align;
    std::vector<ui::ListColumn> columns;
    columns.push_back({Tr(S::ColDocument), 0, Align::Left, false, true, -1, 0});
    columns.push_back({Tr(S::ColResult), 118, Align::Center, true, false, -1, 0});
    columns.push_back({Tr(S::ColUser), 118, Align::Center, false, false, -1, 3});
    columns.push_back({Tr(S::ColMachine), 118, Align::Center, false, false, -1, 5});
    columns.push_back({Tr(S::ColPrinter), 130, Align::Center, false, false, -1, 4});
    columns.push_back({Tr(S::ColPages), 64, Align::Center, false, false, -1, 1});
    columns.push_back({Tr(S::ColSize), 74, Align::Center, false, false, -1, 2});
    columns.push_back({Tr(S::ColTime), 136, Align::Center, false, false, -1, 2});
    return columns;
}

std::pair<SortBy, bool> NextSort(SortBy current, bool descending, SortBy clicked) {
    if (clicked == current) return {current, !descending};
    return {clicked, false};
}

ui::ListRow MakeJobRow(const core::JobInfo& job, bool showPrinter, const core::ContentFacts* facts) {
    ui::ListRow row;
    row.key = job.Key();
    row.cells.push_back(job.position ? std::to_wstring(job.position) : L"");
    row.cells.push_back(job.document.empty() ? Tr(S::Unknown) : job.document);
    row.cells.push_back(JobStatusText(job));
    row.cells.push_back(job.user);
    row.cells.push_back(MachineName(job.machine));
    if (showPrinter) row.cells.push_back(PrinterShortName(job.printer));
    row.cells.push_back(TotalPagesText(TotalPrintPages(job, facts)));
    const ListSettings settings = ResolveListSettings(job, facts);
    row.cells.push_back(settings.paper);
    row.cells.push_back(settings.orientation);
    row.cells.push_back(settings.duplex);
    row.cells.push_back(settings.source);
    row.cells.push_back(util::FormatBytes(job.sizeBytes));
    row.cells.push_back(util::FormatDateTime(job.submitted));  // fecha y hora completas
    row.tone = job.heldByApp ? ui::Tone::Special : ui::ToneForState(job.state);
    row.glyph = GlyphForState(job.state);
    return row;
}

std::wstring HistoryKey(const config::HistoryEntry& entry, size_t index) {
    return util::Format(L"h%lld_%zu", static_cast<long long>(entry.timeUnix), index);
}

ui::ListRow MakeHistoryRow(const config::HistoryEntry& e, size_t index) {
    ui::ListRow row;
    row.key = HistoryKey(e, index);
    row.cells.push_back(e.document.empty() ? Tr(S::Unknown) : e.document);
    row.cells.push_back(OutcomeText(e.outcome));
    row.cells.push_back(e.user);
    row.cells.push_back(MachineName(e.machine));
    row.cells.push_back(PrinterShortName(e.printer));
    row.cells.push_back(e.pages ? std::to_wstring(e.pages) : L"—");
    row.cells.push_back(util::FormatBytes(e.sizeBytes));
    row.cells.push_back(util::FormatTimeIso(util::FromUnixSeconds(e.timeUnix)));
    row.tone = ToneForOutcome(e.outcome);
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
    core::PrinterProblem worst = core::ProblemNone;
    for (const auto& p : snapshot.printers) {
        auto problem = core::MostSevereProblem(p.problems);
        if (problem != core::ProblemNone && (worst == core::ProblemNone || problem < worst)) worst = problem;
    }
    all.tone = ToneForProblem(worst);
    chips.push_back(std::move(all));

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

QueueSummary Summarize(const std::vector<const core::JobInfo*>& jobs) {
    QueueSummary s;
    for (const auto* job : jobs) {
        ++s.total;
        if (job->state == JobState::Printing) ++s.printing;
        if (job->heldByApp) ++s.held;
        else if (job->state == JobState::Paused) ++s.paused;
        if (job->state == JobState::Error || job->state == JobState::Offline) ++s.errors;
    }
    return s;
}

std::wstring SummaryText(const QueueSummary& s) {
    if (s.total == 0) return Tr(S::StatusNoJobs);
    std::wstring text = s.total == 1 ? std::wstring(Tr(S::StatusOneJob)) : util::Format(Tr(S::StatusJobs), s.total);
    if (s.printing) text += L" · " + util::Format(Tr(S::StatusPrinting), s.printing);
    if (s.held) text += L" · " + util::Format(Tr(S::StatusHeldCount), s.held);
    if (s.paused) text += L" · " + util::Format(Tr(S::StatusPaused), s.paused);
    if (s.errors) text += L" · " + util::Format(Tr(S::StatusErrors), s.errors);
    return text;
}

}  // namespace cola::app
