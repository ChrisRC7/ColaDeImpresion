// Pruebas de core/ (estados, seguimiento de trabajos), input/ (atajos),
// ui/ (contraste del tema) y app/ (filtro de filas).

#include <windows.h>
#include <winspool.h>

#include <algorithm>

#include "TestFramework.h"
#include "app/RowBuilder.h"
#include "config/Settings.h"
#include "core/AutoHold.h"
#include "core/JobStatus.h"
#include "core/JobTracker.h"
#include "core/PrintRelease.h"
#include "core/Spooler.h"
#include "input/Shortcuts.h"
#include "ui/JobListView.h"
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

TEST(CanReadJobDataOnlyWhenHeld) {
    // Solo en pausa o ya impreso: leer a la vez que el spooler lo envía daña la impresión.
    CHECK(core::CanReadJobData(JOB_STATUS_PAUSED));
    CHECK(core::CanReadJobData(JOB_STATUS_PRINTED));
    CHECK(core::CanReadJobData(JOB_STATUS_COMPLETE));
    CHECK(!core::CanReadJobData(0));  // en cola: puede empezar a imprimirse en cualquier momento
    CHECK(!core::CanReadJobData(JOB_STATUS_PRINTING));
    CHECK(!core::CanReadJobData(JOB_STATUS_PAUSED | JOB_STATUS_PRINTING));
    CHECK(!core::CanReadJobData(JOB_STATUS_PAUSED | JOB_STATUS_SPOOLING));
    CHECK(!core::CanReadJobData(JOB_STATUS_PAUSED | JOB_STATUS_DELETING));
    CHECK(!core::CanReadJobData(JOB_STATUS_PAUSED | JOB_STATUS_RESTART));
    CHECK(!core::CanReadJobData(JOB_STATUS_PAUSED | core::kJobStatusRenderingLocally));
}

TEST(LocalCsrCopyIsNeverControlled) {
    // La copia local que esta PC todavía envía al servidor: las acciones la omiten.
    CHECK(core::IsLocalCsrCopy(core::kJobStatusRenderingLocally));
    CHECK(core::IsLocalCsrCopy(core::kJobStatusRenderingLocally | JOB_STATUS_PAUSED));  // 0x4001 de la 0.4.8
    CHECK(core::IsLocalCsrCopy(core::kJobStatusRenderingLocally | JOB_STATUS_SPOOLING));
    // Los trabajos del servidor sí se controlan, en cualquier estado.
    CHECK(!core::IsLocalCsrCopy(0));
    CHECK(!core::IsLocalCsrCopy(JOB_STATUS_PAUSED | JOB_STATUS_SPOOLING));  // 0x9: el del servidor retenido
    CHECK(!core::IsLocalCsrCopy(JOB_STATUS_PRINTING));
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

TEST(JobTrackerKeepsJobsOfUnavailablePrinter) {
    core::JobTracker tracker;
    tracker.Update(MakeSnapshot({MakeJob(1, core::JobState::Paused), MakeJob(2, core::JobState::Paused)}));
    // La impresora deja de responder: su lista llega vacía, pero nada terminó.
    CHECK(tracker.Update(MakeSnapshot({}, core::ProblemNotAvailable)).empty());
    CHECK(tracker.Update(MakeSnapshot({}, core::ProblemNotAvailable)).empty());
    // Vuelve: el 1 sigue en la cola (no es "nuevo") y el 2 se liberó y salió en el corte.
    tracker.MarkReleasedByUser(MakeJob(2, core::JobState::Paused).Key());
    auto events = tracker.Update(MakeSnapshot({MakeJob(1, core::JobState::Paused)}));
    CHECK_EQ(events.size(), size_t(1));
    if (!events.empty()) {
        CHECK(events[0].kind == core::JobEvent::Kind::Finished);
        CHECK_EQ(events[0].job.id, 2u);
        CHECK(events[0].outcome == config::JobOutcome::Printed);
    }

    // El resultado se decide con el estado de la impresora de antes del corte:
    // el primero de una cola operativa que sale durante el corte se imprimió.
    core::JobTracker queued;
    queued.Update(MakeSnapshot({MakeJob(7, core::JobState::Queued, 1)}));
    CHECK(queued.Update(MakeSnapshot({}, core::ProblemNotAvailable)).empty());
    events = queued.Update(MakeSnapshot({}));
    CHECK_EQ(events.size(), size_t(1));
    if (!events.empty()) CHECK(events[0].outcome == config::JobOutcome::Printed);
}

TEST(ReleasedJobsAndLocalCopiesInHistory) {
    // Caso real del LAIMI (0.5.4): trabajo retenido con la impresora en pausa;
    // el operador lo libera (F11) y se imprime antes del siguiente refresco.
    core::JobTracker tracker;
    auto held = MakeJob(221, core::JobState::Paused, 1);
    auto other = MakeJob(222, core::JobState::Paused, 2);
    // Copia local de Client Side Rendering del mismo documento (otra id).
    auto local = MakeJob(5, core::JobState::Spooling, 29999);
    local.rawStatus = core::kJobStatusRenderingLocally;
    tracker.Update(MakeSnapshot({held, other, local}, core::ProblemPaused));
    auto events = tracker.Update(MakeSnapshot({held, other}, core::ProblemPaused));
    CHECK(events.empty());  // la copia local no cuenta como trabajo terminado

    tracker.MarkReleasedByUser(held.Key());
    events = tracker.Update(MakeSnapshot({}, core::ProblemPaused));
    CHECK_EQ(events.size(), size_t(2));
    for (auto& e : events) {
        if (e.job.id == 221) CHECK(e.outcome == config::JobOutcome::Printed);
        if (e.job.id == 222) CHECK(e.outcome == config::JobOutcome::Removed);  // nadie lo liberó
    }

    // Una copia local que aparece en medio tampoco genera "nuevo trabajo".
    events = tracker.Update(MakeSnapshot({local}));
    CHECK(events.empty());
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Paused, 1), core::ProblemPaused, false, true) ==
          config::JobOutcome::Printed);
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Paused, 1), core::ProblemPaused, true, true) ==
          config::JobOutcome::Cancelled);
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

    // La copia local que esta PC todavía envía al servidor (CSR) nunca se pausa:
    // se retiene el trabajo del servidor (8) cuando aparece.
    core::JobInfo local = MakeJob(7, core::JobState::Queued);
    local.rawStatus = core::kJobStatusRenderingLocally;
    CHECK(!core::ShouldHold(local));
    ids = hold.Process(printer, {local, MakeJob(8, core::JobState::Spooling)}, true);
    CHECK_EQ(ids.size(), size_t(1));
    CHECK_EQ(ids[0], 8u);
}

TEST(ColumnLayoutWithUserWidths) {
    // A 96 DPI (1 DIP = 1 px): margen izquierdo 18, derecho 16, separación 12, flexible mín. 140.
    std::vector<ui::ListColumn> cols(3);
    cols[0].width = 100;
    cols[0].id = L"a";
    cols[1].width = 0;  // flexible
    cols[1].id = L"doc";
    cols[2].width = 100;
    cols[2].id = L"b";

    // Sin anchos del operador la flexible ocupa lo que sobra.
    auto slots = ui::LayoutColumns(cols, {}, 1000);
    CHECK_EQ(slots.size(), size_t(3));
    CHECK_EQ(slots[1].left, 130);
    CHECK_EQ(slots[1].right, 860);
    CHECK_EQ(slots[2].right, 972);

    // Agrandar una columna no oculta otras: se sale del ancho (barra horizontal).
    slots = ui::LayoutColumns(cols, {{L"b", 600}}, 500);
    CHECK_EQ(slots.size(), size_t(3));
    CHECK_EQ(slots[2].right - slots[2].left, 600);
    CHECK(slots[2].right > 500);

    // En una ventana angosta no se oculta ninguna: la flexible queda en su
    // mínimo y las demás se salen por la derecha (barra horizontal).
    slots = ui::LayoutColumns(cols, {}, 300);
    CHECK_EQ(slots.size(), size_t(3));
    CHECK_EQ(slots[1].right - slots[1].left, 140);
    CHECK_EQ(slots[2].column, size_t(2));
    CHECK(slots[2].right > 300);

    // La flexible con ancho del operador queda fija.
    slots = ui::LayoutColumns(cols, {{L"doc", 200}}, 1000);
    CHECK_EQ(slots[1].right - slots[1].left, 200);
    CHECK_EQ(slots[2].left, 342);
}

TEST(ColumnOrderNormalizeAndMerge) {
    using V = std::vector<std::wstring>;
    const V defaults = {L"number", L"document", L"status", L"user", L"printer", L"pages"};
    // Vacío = predeterminado; ids desconocidos o repetidos se descartan.
    CHECK(ui::NormalizeColumnOrder({}, defaults) == defaults);
    CHECK(ui::NormalizeColumnOrder({L"number", L"user", L"viejo", L"document", L"user", L"status", L"printer",
                                    L"pages"},
                                   defaults) == (V{L"number", L"user", L"document", L"status", L"printer", L"pages"}));
    // Una columna que falta (nueva) va detrás de la que la precede por defecto.
    CHECK(ui::NormalizeColumnOrder({L"pages", L"document", L"number", L"status", L"user"}, defaults) ==
          (V{L"pages", L"document", L"number", L"status", L"user", L"printer"}));

    // Mover en la vista sin Impresora no le cambia el lugar a Impresora.
    const V merged = ui::MergeColumnOrder(defaults, {L"user", L"number", L"document", L"status", L"pages"});
    CHECK(merged == (V{L"user", L"number", L"document", L"status", L"printer", L"pages"}));
}

TEST(ColumnDisplayOrderAndMove) {
    std::vector<ui::ListColumn> cols(4);
    cols[0].id = L"a";
    cols[1].id = L"b";
    cols[2].id = L"c";
    cols[3].id = L"d";
    using I = std::vector<size_t>;
    CHECK(ui::ColumnDisplayOrder(cols, {}) == (I{0, 1, 2, 3}));
    CHECK(ui::ColumnDisplayOrder(cols, {L"c", L"a", L"d", L"b"}) == (I{2, 0, 3, 1}));
    // Sin id (o fuera del orden) no se mueve; las demás se reparten en sus lugares.
    cols[1].id.clear();
    CHECK(ui::ColumnDisplayOrder(cols, {L"d", L"c", L"a"}) == (I{3, 1, 2, 0}));

    const I display = {0, 1, 2, 3};
    CHECK(ui::MoveDisplayColumn(display, 0, 3, true) == (I{1, 2, 3, 0}));
    CHECK(ui::MoveDisplayColumn(display, 3, 0, false) == (I{3, 0, 1, 2}));
    CHECK(ui::MoveDisplayColumn(display, 1, 2, false) == display);  // ya estaba ahí
    CHECK(ui::MoveDisplayColumn(display, 1, 9, false) == display);  // ancla inexistente
}

TEST(ColumnOrderSavedPerView) {
    // Predeterminado = el orden actual de la tabla: nada guardado.
    CHECK(app::EffectiveColumnOrder({}) == app::DefaultColumnOrder());
    CHECK(app::SaveColumnOrder({}, {L"number", L"document", L"status"}).empty());

    // Mover Usuario al principio en la vista de una impresora (sin Impresora).
    std::vector<std::wstring> view;
    for (const auto& c : app::JobColumns(false)) view.push_back(c.id);
    view.erase(std::find(view.begin(), view.end(), L"user"));
    view.insert(view.begin(), L"user");
    auto saved = app::SaveColumnOrder({}, view);
    CHECK(!saved.empty());
    auto columns = app::JobColumns(true);
    auto display = ui::ColumnDisplayOrder(columns, app::EffectiveColumnOrder(saved));
    CHECK_EQ(columns[display[0]].id, std::wstring(L"user"));
    // En "Todas", Impresora sigue después de Equipo como antes.
    for (size_t i = 0; i + 1 < display.size(); ++i)
        if (columns[display[i]].id == L"machine") CHECK_EQ(columns[display[i + 1]].id, std::wstring(L"printer"));
    // El historial no cambia.
    auto history = app::HistoryColumns();
    CHECK(ui::ColumnDisplayOrder(history, app::EffectiveColumnOrder(saved)) ==
          ui::ColumnDisplayOrder(history, {}));
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

TEST(AutoHoldExistingJobsOnStart) {
    core::AutoHoldTracker hold;
    const std::wstring printer = L"\\\\S\\P1";
    // "Al iniciar, pausar todo": los trabajos que ya estaban también se retienen...
    auto ids = hold.Process(printer,
                            {MakeJob(1, core::JobState::Queued), MakeJob(2, core::JobState::Paused),
                             MakeJob(3, core::JobState::Printing)},
                            true, true);
    CHECK_EQ(ids.size(), size_t(2));  // 1 y 3 (el 2 ya estaba en pausa)
    // ...pero solo la primera vez: después rige la regla de trabajos nuevos.
    ids = hold.Process(printer, {MakeJob(1, core::JobState::Queued), MakeJob(4, core::JobState::Queued)}, true, true);
    CHECK_EQ(ids.size(), size_t(1));
    CHECK_EQ(ids[0], 4u);
}

TEST(PrintReleaseRepausesWhenAllSent) {
    core::PrintReleaseTracker release;
    const std::wstring p1 = L"\\\\S\\P1";
    core::JobInfo other = MakeJob(9, core::JobState::Paused);
    other.printer = L"\\\\S\\P2";
    auto snap = [&](std::vector<core::JobInfo> jobs, std::uint32_t p1Count) {
        core::QueueSnapshot s;
        core::PrinterInfo a;
        a.name = p1;
        a.jobCount = p1Count;
        core::PrinterInfo b;
        b.name = other.printer;
        b.jobCount = 1;
        s.printers = {a, b};
        s.jobs = std::move(jobs);
        return s;
    };
    // Se liberan 1 y 2 de P1 y el 9 de P2; uno ya impreso no se sigue.
    CHECK_EQ(release.Start({MakeJob(1, core::JobState::Paused), MakeJob(2, core::JobState::Paused), other,
                            MakeJob(3, core::JobState::Printed)},
                           0),
             size_t(3));
    // Instantánea anterior a la reanudación: siguen en pausa, no se decide nada.
    CHECK(release.Update(snap({MakeJob(1, core::JobState::Paused), MakeJob(2, core::JobState::Paused), other}, 2), 1000)
              .empty());
    // Imprimiendo el 1, el 2 en cola; el 9 sigue en pausa.
    CHECK(release.Update(snap({MakeJob(1, core::JobState::Printing), MakeJob(2, core::JobState::Queued), other}, 2), 2000)
              .empty());
    // El 1 salió de la cola, pero el spooler cuenta 2 trabajos: el ausente podría seguir ahí.
    CHECK(release.Update(snap({MakeJob(2, core::JobState::Queued), other}, 2), 3000).empty());
    CHECK_EQ(release.PendingJobs(), size_t(3));  // 1 y 2 de P1, 9 de P2
    // El 2 queda "enviado a la impresora": P1 ya se puede volver a pausar.
    core::JobInfo sent = MakeJob(2, core::JobState::Printing);
    sent.rawStatus = JOB_STATUS_COMPLETE;
    auto done = release.Update(snap({sent, other}, 1), 4000);
    CHECK_EQ(done.size(), size_t(1));
    CHECK(done[0] == p1);
    // El 9 nunca se reanudó (sin permisos): pasado el plazo se deja de esperar.
    done = release.Update(snap({other}, 0), core::PrintReleaseTracker::kResumeGraceMs);
    CHECK_EQ(done.size(), size_t(1));
    CHECK(done[0] == other.printer);
    CHECK(!release.Active());
}

TEST(PrintReleaseJobPausedAgainOrSnapshotError) {
    core::PrintReleaseTracker release;
    release.Start({MakeJob(1, core::JobState::Paused)}, 0);
    core::QueueSnapshot s;
    core::PrinterInfo p;
    p.name = L"\\\\S\\P1";
    p.jobCount = 1;
    s.printers = {p};
    s.jobs = {MakeJob(1, core::JobState::Queued)};
    CHECK(release.Update(s, 100).empty());
    // Con un error global o la impresora no disponible no se decide nada.
    s.jobs.clear();
    s.error = L"sin conexión";
    CHECK(release.Update(s, 200).empty());
    s.error.clear();
    s.printers[0].problems = core::ProblemNotAvailable;
    s.printers[0].jobCount = 0;
    CHECK(release.Update(s, 300).empty());
    // Un operador lo vuelve a pausar después de verlo correr: se vuelve a pausar la impresora.
    s.printers[0].problems = core::ProblemNone;
    s.printers[0].jobCount = 1;
    s.jobs = {MakeJob(1, core::JobState::Paused)};
    CHECK_EQ(release.Update(s, 400).size(), size_t(1));
}

TEST(SettingsHoldDefaults) {
    config::Settings s;
    CHECK(s.autoHoldNewJobs);
    CHECK(s.pauseAllOnStart);
    s.pauseAllOnStart = false;
    CHECK(!config::Settings::FromJson(s.ToJson()).pauseAllOnStart);
}

TEST(PauseToggleDecision) {
    // Todo en pausa (o retenido) -> el comando reanuda; si algo corre -> pausa.
    core::JobInfo held = MakeJob(1, core::JobState::Paused);
    held.heldByApp = true;
    CHECK(!app::ShouldPauseJobs({held}));
    CHECK(!app::ShouldPauseJobs({MakeJob(1, core::JobState::Paused), MakeJob(2, core::JobState::Paused)}));
    CHECK(app::ShouldPauseJobs({MakeJob(1, core::JobState::Paused), MakeJob(2, core::JobState::Printing)}));
    CHECK(app::ShouldPauseJobs({MakeJob(1, core::JobState::Queued)}));
    CHECK(!app::ShouldPauseJobs({}));  // sin trabajos no hay nada que pausar

    // El comando combinado usa las impresoras de los trabajos elegidos.
    core::JobInfo other = MakeJob(3, core::JobState::Queued);
    other.printer = L"\\\\S\\P2";
    auto printers = app::PrintersOf({MakeJob(1, core::JobState::Queued), MakeJob(2, core::JobState::Queued), other});
    CHECK_EQ(printers.size(), size_t(2));
    CHECK_EQ(printers[1], std::wstring(L"\\\\S\\P2"));
}

TEST(PrinterShortcutsExist) {
    input::ShortcutMap map;
    auto find = [&](const wchar_t* text) { return map.Find(*input::KeyChord::Parse(text)); };
    CHECK(find(L"Ctrl+P") == input::Command::Pause);              // trabajo (alterna)
    CHECK(find(L"Ctrl+Shift+P") == input::Command::PausePrinter);  // impresora (alterna)
    CHECK(find(L"F9") == input::Command::PausePrinter);
    CHECK(find(L"Ctrl+Alt+P") == input::Command::TogglePauseBoth);  // ambos
    CHECK(find(L"F8") == input::Command::TogglePauseBoth);
}

namespace {
// Trabajos de dos equipos y horas distintas para probar el ordenamiento.
core::QueueSnapshot MakeMixedSnapshot() {
    auto at = [](WORD hour, WORD minute) {
        SYSTEMTIME t{};
        t.wYear = 2026;
        t.wMonth = 9;
        t.wDay = 22;
        t.wHour = hour;
        t.wMinute = minute;
        return t;
    };
    core::JobInfo a = MakeJob(10, core::JobState::Queued, 1);
    a.machine = L"\\\\LAB-PC07";
    a.user = L"ana";
    a.submitted = at(9, 30);
    a.sizeBytes = 100;
    core::JobInfo b = MakeJob(11, core::JobState::Queued, 2);
    b.machine = L"\\\\LAB-PC02";
    b.user = L"bruno";
    b.submitted = at(8, 15);
    b.sizeBytes = 300;
    core::JobInfo c = MakeJob(12, core::JobState::Queued, 3);
    c.machine = L"\\\\LAB-PC02";
    c.user = L"carla";
    c.submitted = at(10, 45);
    c.sizeBytes = 200;
    return MakeSnapshot({a, b, c});
}
}  // namespace

TEST(SortQueueByMachineAndTime) {
    auto snapshot = MakeMixedSnapshot();
    auto ids = [](const std::vector<const core::JobInfo*>& jobs) {
        std::vector<std::uint32_t> out;
        for (const auto* job : jobs) out.push_back(job->id);
        return out;
    };

    // Orden natural: posición en la cola.
    auto queue = ids(app::VisibleJobs(snapshot, app::kAllPrinters, L""));
    CHECK_EQ(queue[0], 10u);
    CHECK_EQ(queue[2], 12u);

    // Por equipo: LAB-PC02 antes que LAB-PC07; dentro del equipo, orden de cola.
    auto machine = ids(app::VisibleJobs(snapshot, app::kAllPrinters, L"", app::SortBy::Machine));
    CHECK_EQ(machine[0], 11u);
    CHECK_EQ(machine[1], 12u);
    CHECK_EQ(machine[2], 10u);

    // Por hora de envío: 8:15, 9:30, 10:45.
    auto time = ids(app::VisibleJobs(snapshot, app::kAllPrinters, L"", app::SortBy::Time));
    CHECK_EQ(time[0], 11u);
    CHECK_EQ(time[1], 10u);
    CHECK_EQ(time[2], 12u);

    // Invertido: el más reciente primero.
    auto desc = ids(app::VisibleJobs(snapshot, app::kAllPrinters, L"", app::SortBy::Time, true));
    CHECK_EQ(desc[0], 12u);
    CHECK_EQ(desc[2], 11u);

    // Y la cola invertida sigue siendo la cola, al revés.
    auto queueDesc = ids(app::VisibleJobs(snapshot, app::kAllPrinters, L"", app::SortBy::Queue, true));
    CHECK_EQ(queueDesc[0], 12u);
    CHECK_EQ(queueDesc[2], 10u);

    // Por usuario y por tamaño.
    CHECK_EQ(ids(app::VisibleJobs(snapshot, app::kAllPrinters, L"", app::SortBy::User))[0], 10u);
    CHECK_EQ(ids(app::VisibleJobs(snapshot, app::kAllPrinters, L"", app::SortBy::Size))[0], 10u);
}

TEST(SortIdsRoundTrip) {
    for (int i = 0; i < app::kSortCount; ++i) {
        auto sort = static_cast<app::SortBy>(i);
        CHECK(app::SortByFromId(app::SortById(sort)) == sort);
    }
    CHECK(app::SortByFromId(L"criterio-inventado") == app::SortBy::Queue);
    CHECK(app::SortByFromId(L"MACHINE") == app::SortBy::Machine);
}

TEST(QuickAccessPrinters) {
    auto snapshot = MakeMixedSnapshot();
    core::PrinterInfo second;
    second.name = L"\\\\S\\P2";
    second.problems = core::ProblemPaperOut;
    snapshot.printers.push_back(second);

    // Sin impresoras fijadas solo está "Todas", con el total de trabajos.
    auto chips = app::BuildQuickAccess(snapshot, {});
    CHECK_EQ(chips.size(), size_t(1));
    CHECK_EQ(chips[0].name, std::wstring(app::kAllPrinters));
    CHECK_EQ(chips[0].jobs, 3u);
    CHECK(chips[0].tone == ui::Tone::Danger);  // P2 está sin papel

    // Fijar respeta el orden e ignora impresoras de otro servidor.
    auto favorites = app::TogglePinned({}, L"\\\\S\\P2");
    favorites = app::TogglePinned(favorites, L"\\\\S\\P1");
    favorites = app::TogglePinned(favorites, L"\\\\OTRO\\P9");
    CHECK(app::IsPinned(favorites, L"\\\\S\\P1"));
    chips = app::BuildQuickAccess(snapshot, favorites);
    CHECK_EQ(chips.size(), size_t(3));
    CHECK_EQ(chips[1].name, std::wstring(L"\\\\S\\P2"));
    CHECK_EQ(chips[1].jobs, 0u);
    CHECK_EQ(chips[2].name, std::wstring(L"\\\\S\\P1"));
    CHECK_EQ(chips[2].jobs, 3u);

    // Volver a fijar la misma impresora la quita.
    favorites = app::TogglePinned(favorites, L"\\\\S\\P2");
    CHECK(!app::IsPinned(favorites, L"\\\\S\\P2"));
    CHECK_EQ(app::BuildQuickAccess(snapshot, favorites).size(), size_t(2));
}

TEST(SettingsSortAndFavoritesRoundTrip) {
    config::Settings settings;
    CHECK_EQ(settings.sortBy, std::wstring(L"queue"));
    CHECK(!settings.sortDescending);
    CHECK(settings.showPrinterBar);
    settings.sortBy = L"machine";
    settings.sortDescending = true;
    settings.favoritePrinters = {L"\\\\S\\P1", L"\\\\S\\P2"};
    settings.showPrinterBar = false;
    auto restored = config::Settings::FromJson(settings.ToJson());
    CHECK_EQ(restored.sortBy, std::wstring(L"machine"));
    CHECK(restored.sortDescending);
    CHECK(!restored.showPrinterBar);
    CHECK_EQ(restored.favoritePrinters.size(), size_t(2));
    CHECK_EQ(restored.favoritePrinters[1], std::wstring(L"\\\\S\\P2"));
}
