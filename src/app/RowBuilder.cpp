#include "app/RowBuilder.h"

#include <algorithm>

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

std::vector<const core::JobInfo*> VisibleJobs(const core::QueueSnapshot& snapshot, const std::wstring& printer,
                                              const std::wstring& filter) {
    std::vector<const core::JobInfo*> jobs;
    const bool all = printer == kAllPrinters || printer.empty();
    for (const auto& job : snapshot.jobs) {
        if (!all && job.printer != printer) continue;
        if (!JobMatchesFilter(job, filter)) continue;
        jobs.push_back(&job);
    }
    std::stable_sort(jobs.begin(), jobs.end(), [](const core::JobInfo* a, const core::JobInfo* b) {
        if (a->printer != b->printer) return _wcsicmp(a->printer.c_str(), b->printer.c_str()) < 0;
        if (a->position != b->position) return a->position < b->position;
        return a->id < b->id;
    });
    return jobs;
}

ui::ListRow MakeJobRow(const core::JobInfo& job, bool showPrinter) {
    ui::ListRow row;
    row.key = job.Key();
    row.title = job.document.empty() ? Tr(S::Unknown) : job.document;
    std::vector<std::wstring> parts;
    if (!job.user.empty()) parts.push_back(job.user);
    if (!job.machine.empty()) {
        std::wstring machine = job.machine;
        while (!machine.empty() && machine.front() == L'\\') machine.erase(machine.begin());
        parts.push_back(machine);
    }
    std::wstring time = util::FormatTimeShort(job.submitted);
    if (!time.empty()) parts.push_back(time);
    if (showPrinter) parts.push_back(PrinterShortName(job.printer));
    for (const auto& p : parts) row.subtitle += (row.subtitle.empty() ? L"" : L" · ") + p;
    row.pages = PagesText(job);
    row.size = util::FormatBytes(job.sizeBytes);
    row.status = JobStatusText(job);
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
    row.title = e.document.empty() ? Tr(S::Unknown) : e.document;
    std::wstring machine = e.machine;
    while (!machine.empty() && machine.front() == L'\\') machine.erase(machine.begin());
    for (const auto& p : {e.user, machine, PrinterShortName(e.printer), util::FormatTimeIso(util::FromUnixSeconds(e.timeUnix))})
        if (!p.empty()) row.subtitle += (row.subtitle.empty() ? L"" : L" · ") + p;
    if (e.pages == 0) row.pages = Tr(S::PagesUnknown);
    else if (e.pages == 1) row.pages = Tr(S::PagesOne);
    else row.pages = util::Format(Tr(S::PagesCount), e.pages);
    row.size = util::FormatBytes(e.sizeBytes);
    row.status = OutcomeText(e.outcome);
    row.tone = ToneForOutcome(e.outcome);
    row.glyph = e.outcome == config::JobOutcome::Printed ? ui::glyph::Completed
                : e.outcome == config::JobOutcome::Failed ? ui::glyph::Error
                                                          : ui::glyph::Cancel;
    return row;
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
