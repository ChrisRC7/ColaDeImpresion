// Pruebas de core/ (estados, seguimiento de trabajos), input/ (atajos),
// ui/ (contraste del tema) y app/ (filtro de filas).

#include <windows.h>
#include <winspool.h>

#include "TestFramework.h"
#include "app/RowBuilder.h"
#include "core/AutoHold.h"
#include "core/JobStatus.h"
#include "core/JobTracker.h"
#include "core/Spooler.h"
#include "input/Shortcuts.h"
#include "ui/Theme.h"

using namespace cola;

TEST(JobStateMapping) {
    using core::JobState;
    CHECK(core::JobStateFromFlags(0, 0) == JobState::Queued);
    CHECK(core::JobStateFromFlags(JOB_STATUS_PRINTING, 0) == JobState::Printing);
    CHECK(core::JobStateFromFlags(JOB_STATUS_PAUSED, 0) == JobState::Paused);
    CHECK(core::JobStateFromFlags(JOB_STATUS_PAPEROUT | JOB_STATUS_PRINTING, 0) == JobState::Error);
    CHECK(core::JobStateFromFlags(JOB_STATUS_DELETING | JOB_STATUS_PAUSED, 0) == JobState::Deleting);
    CHECK(core::JobStateFromFlags(JOB_STATUS_SPOOLING, 0) == JobState::Spooling);
    CHECK(core::JobStateFromFlags(JOB_STATUS_PRINTING, core::ProblemPaperJam) == JobState::Error);
    CHECK(core::JobStateFromFlags(0, core::ProblemOffline) == JobState::Offline);
}

TEST(PrinterProblems) {
    auto p = core::PrinterProblemsFromFlags(PRINTER_STATUS_PAPER_OUT | PRINTER_STATUS_TONER_LOW, 0);
    CHECK(p & core::ProblemPaperOut);
    CHECK(p & core::ProblemTonerLow);
    CHECK(core::MostSevereProblem(p) == core::ProblemPaperOut);
    CHECK(core::PrinterProblemsFromFlags(0, PRINTER_ATTRIBUTE_WORK_OFFLINE) & core::ProblemOffline);
    CHECK(core::MostSevereProblem(0) == core::ProblemNone);
}

TEST(NormalizeServerName) {
    using core::spooler::NormalizeServerName;
    CHECK_EQ(NormalizeServerName(L""), std::wstring(L""));
    CHECK_EQ(NormalizeServerName(L"localhost"), std::wstring(L""));
    CHECK_EQ(NormalizeServerName(L"LAIMI-PS"), std::wstring(L"\\\\LAIMI-PS"));
    CHECK_EQ(NormalizeServerName(L"\\\\LAIMI-PS\\"), std::wstring(L"\\\\LAIMI-PS"));
    CHECK_EQ(NormalizeServerName(L" //10.0.0.5 "), std::wstring(L"\\\\10.0.0.5"));
}

TEST(PrinterDisplayName) {
    core::PrinterInfo p;
    p.name = L"\\\\LAIMI-PS\\HP Lab 1";
    CHECK_EQ(p.DisplayName(), std::wstring(L"HP Lab 1"));
    p.name = L"Microsoft Print to PDF";
    CHECK_EQ(p.DisplayName(), std::wstring(L"Microsoft Print to PDF"));
}

namespace {
core::JobInfo MakeJob(std::uint32_t id, core::JobState state, std::uint32_t position = 2) {
    core::JobInfo j;
    j.id = id;
    j.printer = L"\\\\S\\P1";
    j.document = L"doc" + std::to_wstring(id);
    j.user = L"ana";
    j.state = state;
    j.position = position;
    j.totalPages = 3;
    return j;
}
core::QueueSnapshot MakeSnapshot(std::vector<core::JobInfo> jobs, std::uint32_t problems = 0) {
    core::QueueSnapshot s;
    s.server = L"\\\\S";
    core::PrinterInfo p;
    p.name = L"\\\\S\\P1";
    p.problems = problems;
    s.printers.push_back(p);
    s.jobs = std::move(jobs);
    return s;
}
}  // namespace

TEST(JobTrackerEvents) {
    core::JobTracker tracker;
    // Primera instantánea: no genera "Added".
    CHECK(tracker.Update(MakeSnapshot({MakeJob(1, core::JobState::Printing, 1), MakeJob(2, core::JobState::Queued)})).empty());

    // Entra un trabajo nuevo y el 2 pasa a error.
    auto events = tracker.Update(MakeSnapshot(
        {MakeJob(1, core::JobState::Printing, 1), MakeJob(2, core::JobState::Error), MakeJob(3, core::JobState::Queued, 3)}));
    int added = 0, errors = 0;
    for (auto& e : events) {
        if (e.kind == core::JobEvent::Kind::Added) ++added;
        if (e.kind == core::JobEvent::Kind::EnteredError) ++errors;
    }
    CHECK_EQ(added, 1);
    CHECK_EQ(errors, 1);

    // El 1 termina (se imprimió), el 3 lo cancelamos nosotros.
    tracker.MarkCancelledByUser(MakeJob(3, core::JobState::Queued).Key());
    events = tracker.Update(MakeSnapshot({MakeJob(2, core::JobState::Error)}));
    CHECK_EQ(events.size(), size_t(2));
    for (auto& e : events) {
        CHECK(e.kind == core::JobEvent::Kind::Finished);
        if (e.job.id == 1) CHECK(e.outcome == config::JobOutcome::Printed);
        if (e.job.id == 3) CHECK(e.outcome == config::JobOutcome::Cancelled);
    }

    // Un error de conexión no debe interpretarse como "todo terminó".
    auto broken = MakeSnapshot({});
    broken.error = L"RPC server unavailable";
    CHECK(tracker.Update(broken).empty());
}

TEST(GuessOutcomeHeuristics) {
    using config::JobOutcome;
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Deleting), 0, false) == JobOutcome::Cancelled);
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Queued, 1), 0, false) == JobOutcome::Printed);
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Queued, 1), core::ProblemOffline, false) == JobOutcome::Removed);
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Queued, 4), 0, false) == JobOutcome::Removed);
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Error), 0, false) == JobOutcome::Failed);
}

TEST(KeyChordParseAndFormat) {
    auto c = input::KeyChord::Parse(L"Ctrl+Shift+P");
    CHECK(c.has_value());
    CHECK(c->ctrl && c->shift && !c->alt && c->vk == 'P');
    CHECK_EQ(c->ToString(), std::wstring(L"Ctrl+Shift+P"));
    CHECK_EQ(input::KeyChord::Parse(L"ctrl+up")->vk, static_cast<UINT>(VK_UP));
    CHECK_EQ(input::KeyChord::Parse(L"Ctrl++")->vk, static_cast<UINT>(VK_OEM_PLUS));
    CHECK_EQ(input::KeyChord::Parse(L"F12")->vk, static_cast<UINT>(VK_F12));
    CHECK_EQ(input::KeyChord::Parse(L"Supr")->vk, static_cast<UINT>(VK_DELETE));
    CHECK(!input::KeyChord::Parse(L"Hyper+X").has_value());
    CHECK(!input::KeyChord::Parse(L"").has_value());
    CHECK_EQ(input::ParseChordList(L"PageDown, Right").size(), size_t(2));
}

TEST(ShortcutDefaultsFromTodo) {
    input::ShortcutMap map;
    auto find = [&](const wchar_t* text) { return map.Find(*input::KeyChord::Parse(text)); };
    CHECK(find(L"Ctrl+P") == input::Command::Pause);
    CHECK(find(L"Ctrl+R") == input::Command::Resume);
    CHECK(find(L"Delete") == input::Command::Cancel);
    CHECK(find(L"Up") == input::Command::NavUp);
    CHECK(find(L"Ctrl+Down") == input::Command::MoveDown);
    CHECK(find(L"Space") == input::Command::QuickLook);
    CHECK(find(L"Ctrl+D") == input::Command::ToggleTheme);
    CHECK(find(L"Ctrl+F") == input::Command::Find);
    CHECK(find(L"F1") == input::Command::Help);
}

TEST(ShortcutReassignAndOverrides) {
    input::ShortcutMap map;
    auto chord = *input::KeyChord::Parse(L"Ctrl+P");
    // Asignar Ctrl+P a Cancelar se lo quita a Pausar.
    auto previous = map.Assign(input::Command::Cancel, chord);
    CHECK(previous.has_value() && *previous == input::Command::Pause);
    CHECK(map.Get(input::Command::Pause).empty());
    auto overrides = map.Overrides();
    CHECK_EQ(overrides[L"cancel"], std::wstring(L"Ctrl+P"));
    CHECK_EQ(overrides[L"pause"], std::wstring(L""));

    input::ShortcutMap restored;
    restored.ApplyOverrides(overrides);
    CHECK(restored.Find(chord) == input::Command::Cancel);
    CHECK(restored.Get(input::Command::Pause).empty());
    restored.ResetDefaults();
    CHECK(restored.Find(chord) == input::Command::Pause);
}

TEST(NoDuplicateDefaultShortcuts) {
    input::ShortcutMap map;
    for (int i = 0; i < input::kCommandCount; ++i)
        for (const auto& chord : map.Get(static_cast<input::Command>(i)))
            CHECK(map.Find(chord) == static_cast<input::Command>(i));
}

TEST(TextEditingKeysGoToSearchBox) {
    CHECK(input::IsTextEditingChord(*input::KeyChord::Parse(L"A")));
    CHECK(input::IsTextEditingChord(*input::KeyChord::Parse(L"Space")));
    CHECK(input::IsTextEditingChord(*input::KeyChord::Parse(L"Ctrl+A")));
    CHECK(!input::IsTextEditingChord(*input::KeyChord::Parse(L"Down")));
    CHECK(!input::IsTextEditingChord(*input::KeyChord::Parse(L"Ctrl+P")));
    CHECK(!input::IsTextEditingChord(*input::KeyChord::Parse(L"F5")));
}

TEST(ThemeContrastWcagAA) {
    // Texto normal y tonos de estado deben cumplir WCAG AA (4.5:1) en ambos modos.
    for (const ui::Palette* p : {&ui::LightPalette(), &ui::DarkPalette()}) {
        CHECK(ui::ContrastRatio(p->text, p->surface) >= 7.0);
        CHECK(ui::ContrastRatio(p->textMuted, p->surface) >= 4.5);
        CHECK(ui::ContrastRatio(p->textMuted, p->window) >= 4.5);
        CHECK(ui::ContrastRatio(p->selectionText, p->selection) >= 4.5);
        CHECK(ui::ContrastRatio(p->accentText, p->accent) >= 4.5);
        for (auto tone : {ui::Tone::Neutral, ui::Tone::Info, ui::Tone::Success, ui::Tone::Warning, ui::Tone::Danger,
                          ui::Tone::Special}) {
            CHECK(ui::ContrastRatio(p->ToneColor(tone), p->surface) >= 4.5);
            CHECK(ui::ContrastRatio(p->ToneColor(tone), p->ToneBackground(tone)) >= 4.5);
        }
    }
}

TEST(ContrastRatioReference) {
    CHECK(ui::ContrastRatio(RGB(0, 0, 0), RGB(255, 255, 255)) > 20.9);
    CHECK(ui::ContrastRatio(RGB(10, 10, 10), RGB(10, 10, 10)) < 1.01);
}

TEST(RowFilter) {
    core::JobInfo job = MakeJob(1, core::JobState::Paused);
    job.document = L"Tesis capítulo 3.docx";
    job.user = L"jperez";
    job.machine = L"\\\\LAB-PC07";
    CHECK(app::JobMatchesFilter(job, L""));
    CHECK(app::JobMatchesFilter(job, L"tesis"));
    CHECK(app::JobMatchesFilter(job, L"JPEREZ pc07"));
    CHECK(!app::JobMatchesFilter(job, L"tesis mgomez"));

    auto snapshot = MakeSnapshot({MakeJob(5, core::JobState::Queued, 2), MakeJob(4, core::JobState::Printing, 1)});
    auto visible = app::VisibleJobs(snapshot, app::kAllPrinters, L"");
    CHECK_EQ(visible.size(), size_t(2));
    CHECK_EQ(visible[0]->id, 4u);  // ordenado por posición
    auto summary = app::Summarize(visible);
    CHECK_EQ(summary.total, 2u);
    CHECK_EQ(summary.printing, 1u);
}

TEST(AutoHoldOnlyNewJobsOnce) {
    core::AutoHoldTracker hold;
    const std::wstring printer = L"\\\\S\\P1";
    // Primera vez: los trabajos existentes solo se registran.
    CHECK(hold.Process(printer, {MakeJob(1, core::JobState::Queued)}, true).empty());

    // Llegan dos nuevos: uno en cola y otro que ya empezó a imprimirse; ambos se retienen.
    auto ids = hold.Process(printer,
                            {MakeJob(1, core::JobState::Queued), MakeJob(2, core::JobState::Spooling),
                             MakeJob(3, core::JobState::Printing)},
                            true);
    CHECK_EQ(ids.size(), size_t(2));
    hold.MarkHeld(MakeJob(2, core::JobState::Paused).Key());
    hold.MarkHeld(MakeJob(3, core::JobState::Paused).Key());

    std::vector<core::JobInfo> jobs = {MakeJob(2, core::JobState::Paused), MakeJob(3, core::JobState::Paused)};
    hold.Annotate(jobs);
    CHECK(jobs[0].heldByApp && jobs[1].heldByApp);

    // Un operador libera el 2: deja de estar retenido y no se vuelve a pausar.
    ids = hold.Process(printer, {MakeJob(2, core::JobState::Printing), MakeJob(3, core::JobState::Paused)}, true);
    CHECK(ids.empty());
    CHECK(!hold.IsHeld(MakeJob(2, core::JobState::Printing).Key()));
    CHECK(hold.IsHeld(MakeJob(3, core::JobState::Paused).Key()));

    // Pausados manualmente o cancelándose no se tocan; con la retención apagada tampoco.
    CHECK(hold.Process(printer, {MakeJob(4, core::JobState::Paused), MakeJob(5, core::JobState::Deleting)}, true).empty());
    CHECK(hold.Process(printer, {MakeJob(6, core::JobState::Queued)}, false).empty());
    // ...y un trabajo ya evaluado con la retención apagada no se pausa al encenderla.
    CHECK(hold.Process(printer, {MakeJob(6, core::JobState::Queued)}, true).empty());
}

TEST(HeldJobsShownAsHeld) {
    core::JobInfo job = MakeJob(7, core::JobState::Paused);
    job.heldByApp = true;
    auto row = app::MakeJobRow(job, false);
    CHECK(app::JobMatchesFilter(job, L"retenido"));
    auto summary = app::Summarize({&job});
    CHECK_EQ(summary.held, 1u);
    CHECK_EQ(summary.paused, 0u);
    CHECK(row.tone == ui::Tone::Special);
}
