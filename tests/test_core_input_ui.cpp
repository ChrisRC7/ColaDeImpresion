// Pruebas de core/ (estados, seguimiento de trabajos), input/ (atajos),
// ui/ (contraste del tema) y app/ (filtro de filas).
//
// Módulos probados:
//   - core/JobStatus: mapeo de los JOB_STATUS_* / PRINTER_STATUS_* del spooler
//     a estados y problemas propios, CanReadJobData e IsLocalCsrCopy (D39, D50).
//   - core/Spooler: NormalizeServerName; core/PrintTypes: DisplayName.
//   - core/JobTracker: eventos entre instantáneas y resultado del historial
//     (Impreso / Cancelado / Falló / Retirado, D13, D47, D52).
//   - core/AutoHold y core/PrintRelease: retención automática (D17, D39) e
//     "imprimir y volver a pausar" (D35, D50).
//   - input/Shortcuts: parseo y formato de atajos, valores predeterminados,
//     reasignación y duplicados.
//   - ui/Theme y ui/JobListView: contraste WCAG de las paletas, columnas
//     (redimensionar, reordenar, D36/D38/D40) y botones de pausa (D37).
//   - app/RowBuilder: filtro, orden de la cola (D20/D30) y textos de las filas.
// Todas las pruebas son lógica pura: instantáneas (QueueSnapshot) armadas en
// memoria con MakeJob/MakeSnapshot, sin spooler ni ventanas.

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

// JobStateFromFlags reduce la máscara JOB_STATUS_* de un trabajo (más los
// problemas de su impresora) a un único estado para la columna Estado. Las
// banderas se combinan, así que importa la prioridad: error y borrado ganan
// sobre "imprimiendo" o "en pausa".
TEST(JobStateMapping) {
    using core::JobState;
    // Sin banderas: esperando en la cola.
    CHECK(core::JobStateFromFlags(0, 0) == JobState::Queued);
    CHECK(core::JobStateFromFlags(JOB_STATUS_PRINTING, 0) == JobState::Printing);
    CHECK(core::JobStateFromFlags(JOB_STATUS_PAUSED, 0) == JobState::Paused);
    // Sin papel mientras imprime: error (el operador tiene que actuar).
    CHECK(core::JobStateFromFlags(JOB_STATUS_PAPEROUT | JOB_STATUS_PRINTING, 0) == JobState::Error);
    // Un trabajo pausado que se está borrando se muestra como "borrando".
    CHECK(core::JobStateFromFlags(JOB_STATUS_DELETING | JOB_STATUS_PAUSED, 0) == JobState::Deleting);
    CHECK(core::JobStateFromFlags(JOB_STATUS_SPOOLING, 0) == JobState::Spooling);
    // Los problemas de la impresora (segundo argumento) también cuentan: un
    // atasco hace que el trabajo que imprime quede en error, y una impresora
    // fuera de línea deja sus trabajos como "sin conexión".
    CHECK(core::JobStateFromFlags(JOB_STATUS_PRINTING, core::ProblemPaperJam) == JobState::Error);
    CHECK(core::JobStateFromFlags(0, core::ProblemOffline) == JobState::Offline);
}

// D39 (bug del LAIMI 0.4.8): leer los datos de un trabajo con ReadPrinter
// mientras el spooler lo envía a la impresora le quita datos (las lecturas de
// un trabajo comparten su avance, D32) y salían hojas en blanco. Por eso
// CanReadJobData solo permite leer un trabajo quieto.
TEST(CanReadJobDataOnlyWhenHeld) {
    // Solo en pausa o ya impreso: leer a la vez que el spooler lo envía daña la impresión.
    CHECK(core::CanReadJobData(JOB_STATUS_PAUSED));
    CHECK(core::CanReadJobData(JOB_STATUS_PRINTED));
    CHECK(core::CanReadJobData(JOB_STATUS_COMPLETE));
    CHECK(!core::CanReadJobData(0));  // en cola: puede empezar a imprimirse en cualquier momento
    CHECK(!core::CanReadJobData(JOB_STATUS_PRINTING));
    // "En pausa" no basta si además hay otra bandera de actividad: se está
    // imprimiendo (la pausa llegó tarde), todavía se recibe (ReadPrinter
    // esperaría), se borra, se reinicia, o es la copia local de CSR (0x4000)
    // que esta PC está enviando al servidor.
    CHECK(!core::CanReadJobData(JOB_STATUS_PAUSED | JOB_STATUS_PRINTING));
    CHECK(!core::CanReadJobData(JOB_STATUS_PAUSED | JOB_STATUS_SPOOLING));
    CHECK(!core::CanReadJobData(JOB_STATUS_PAUSED | JOB_STATUS_DELETING));
    CHECK(!core::CanReadJobData(JOB_STATUS_PAUSED | JOB_STATUS_RESTART));
    CHECK(!core::CanReadJobData(JOB_STATUS_PAUSED | core::kJobStatusRenderingLocally));
}

// D50: con Client Side Rendering, mientras esta PC convierte y envía un trabajo
// al servidor aparece en la lista una copia local con
// JOB_STATUS_RENDERING_LOCALLY (0x4000). Pausarla o cancelarla deja al
// servidor con datos incompletos (hojas en blanco), así que IsLocalCsrCopy la
// identifica para que pausar/reanudar/cancelar/reiniciar/mover la omitan.
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

// Problemas de la impresora (PRINTER_STATUS_* y atributos) como máscara propia,
// y cuál mostrar si hay varios: sin papel es más grave que tóner bajo (que es
// solo un aviso). "Trabajar sin conexión" es un atributo de la impresora, no
// un estado, y aun así cuenta como fuera de línea.
TEST(PrinterProblems) {
    auto p = core::PrinterProblemsFromFlags(PRINTER_STATUS_PAPER_OUT | PRINTER_STATUS_TONER_LOW, 0);
    CHECK(p & core::ProblemPaperOut);
    CHECK(p & core::ProblemTonerLow);
    CHECK(core::MostSevereProblem(p) == core::ProblemPaperOut);
    CHECK(core::PrinterProblemsFromFlags(0, PRINTER_ATTRIBUTE_WORK_OFFLINE) & core::ProblemOffline);
    CHECK(core::MostSevereProblem(0) == core::ProblemNone);
}

// Lo que el operador escribe como servidor (D4) se normaliza a "\\NOMBRE", la
// forma que espera winspool; "" y "localhost" son este equipo. Se toleran
// espacios, barra final y barras normales ("//10.0.0.5").
TEST(NormalizeServerName) {
    using core::spooler::NormalizeServerName;
    CHECK_EQ(NormalizeServerName(L""), std::wstring(L""));
    CHECK_EQ(NormalizeServerName(L"localhost"), std::wstring(L""));
    CHECK_EQ(NormalizeServerName(L"LAIMI-PS"), std::wstring(L"\\\\LAIMI-PS"));
    CHECK_EQ(NormalizeServerName(L"\\\\LAIMI-PS\\"), std::wstring(L"\\\\LAIMI-PS"));
    CHECK_EQ(NormalizeServerName(L" //10.0.0.5 "), std::wstring(L"\\\\10.0.0.5"));
}

// En la UI una impresora de servidor se muestra sin "\\SERVIDOR\" delante; una
// local ya no tiene prefijo y queda igual.
TEST(PrinterDisplayName) {
    core::PrinterInfo p;
    p.name = L"\\\\LAIMI-PS\\HP Lab 1";
    CHECK_EQ(p.DisplayName(), std::wstring(L"HP Lab 1"));
    p.name = L"Microsoft Print to PDF";
    CHECK_EQ(p.DisplayName(), std::wstring(L"Microsoft Print to PDF"));
}

namespace {
// Trabajo de prueba de "ana" en la impresora \\S\P1 con el estado ya
// traducido (state) y su posición en la cola (1 = el primero, el que imprime).
// La posición por defecto es 2: "no es el primero", lo que importa para
// GuessOutcome. rawStatus queda en 0 salvo que la prueba lo cambie.
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
// Instantánea del servidor \\S con una sola impresora (P1) y esos trabajos.
// `problems` es la máscara de problemas de la impresora (ProblemPaused,
// ProblemNotAvailable...), que el tracker usa para decidir resultados.
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

// JobTracker compara instantáneas sucesivas y produce los eventos que alimentan
// las notificaciones y el historial (D13).
TEST(JobTrackerEvents) {
    core::JobTracker tracker;
    // Primera instantánea: no genera "Added".
    // (Los trabajos que ya estaban al conectar no son "nuevos": si no, al abrir
    // la app llegaría una notificación por cada trabajo de la cola.)
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
    // El 1 era el primero y se lo vio imprimiéndose -> Impreso. El 3 sale de la
    // cola pero la app lo marcó como cancelado por el operador -> Cancelado
    // (sin la marca, siendo el 3.º de la cola, sería "Retirado").
    tracker.MarkCancelledByUser(MakeJob(3, core::JobState::Queued).Key());
    events = tracker.Update(MakeSnapshot({MakeJob(2, core::JobState::Error)}));
    CHECK_EQ(events.size(), size_t(2));
    for (auto& e : events) {
        CHECK(e.kind == core::JobEvent::Kind::Finished);
        if (e.job.id == 1) CHECK(e.outcome == config::JobOutcome::Printed);
        if (e.job.id == 3) CHECK(e.outcome == config::JobOutcome::Cancelled);
    }

    // Un error de conexión no debe interpretarse como "todo terminó".
    // Una instantánea con error global llega sin trabajos; si se tomara en
    // serio, el 2 pasaría al historial como terminado.
    auto broken = MakeSnapshot({});
    broken.error = L"RPC server unavailable";
    CHECK(tracker.Update(broken).empty());
}

// D52: con un corte de red de una impresora (ProblemNotAvailable) su lista
// llega vacía. Antes el tracker olvidaba sus trabajos: uno impreso durante el
// corte nunca llegaba al historial y los demás se avisaban como nuevos al
// volver. Ahora los recuerda hasta que la impresora responde.
TEST(JobTrackerKeepsJobsOfUnavailablePrinter) {
    core::JobTracker tracker;
    tracker.Update(MakeSnapshot({MakeJob(1, core::JobState::Paused), MakeJob(2, core::JobState::Paused)}));
    // La impresora deja de responder: su lista llega vacía, pero nada terminó.
    CHECK(tracker.Update(MakeSnapshot({}, core::ProblemNotAvailable)).empty());
    CHECK(tracker.Update(MakeSnapshot({}, core::ProblemNotAvailable)).empty());
    // Vuelve: el 1 sigue en la cola (no es "nuevo") y el 2 se liberó y salió en el corte.
    // Único evento: el 2 terminó como Impreso porque el operador lo liberó
    // (MarkReleasedByUser, D47) aunque lo último visto fuera "en pausa".
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
    // (0.5.12: printerProblems_ conserva el estado previo al corte; si se usara
    // el "no disponible" del corte, el trabajo saldría como Retirado.)
    core::JobTracker queued;
    queued.Update(MakeSnapshot({MakeJob(7, core::JobState::Queued, 1)}));
    CHECK(queued.Update(MakeSnapshot({}, core::ProblemNotAvailable)).empty());
    events = queued.Update(MakeSnapshot({}));
    CHECK_EQ(events.size(), size_t(1));
    if (!events.empty()) CHECK(events[0].outcome == config::JobOutcome::Printed);
}

// D47: en el historial.json real había 0 impresos. Los trabajos liberados con
// F11 se imprimían entre dos refrescos y quedaban "Retirado" (lo último visto
// era "en pausa" con la impresora en pausa), y la copia local de CSR
// (equipo \\PC, posición 29999, tamaño 0) aparecía como otro "Retirado".
TEST(ReleasedJobsAndLocalCopiesInHistory) {
    // Caso real del LAIMI (0.5.4): trabajo retenido con la impresora en pausa;
    // el operador lo libera (F11) y se imprime antes del siguiente refresco.
    // 221 se libera; 222 queda retenido y luego alguien lo retira de la cola
    // sin liberarlo; 5 es la copia local de CSR (posición 29999 como en el
    // caso real).
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
    // GuessOutcome directo: (último estado, problemas de la impresora,
    // cancelado por la app, liberado por la app). Liberado -> Impreso aunque
    // estuviera en pausa; pero si además se canceló, gana Cancelado.
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Paused, 1), core::ProblemPaused, false, true) ==
          config::JobOutcome::Printed);
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Paused, 1), core::ProblemPaused, true, true) ==
          config::JobOutcome::Cancelled);
}

// Heurística del resultado de un trabajo que salió de la cola (D13): el
// spooler no dice qué pasó, así que se deduce de lo último que se vio.
TEST(GuessOutcomeHeuristics) {
    using config::JobOutcome;
    // Se lo vio borrándose: alguien lo canceló (desde otra herramienta).
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Deleting), 0, false) == JobOutcome::Cancelled);
    // Era el primero de una cola operativa: se imprimió entre dos refrescos.
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Queued, 1), 0, false) == JobOutcome::Printed);
    // El primero, pero con la impresora fuera de línea: no pudo imprimirse.
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Queued, 1), core::ProblemOffline, false) == JobOutcome::Removed);
    // En la posición 4 no le tocaba imprimirse: lo retiraron.
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Queued, 4), 0, false) == JobOutcome::Removed);
    // Su último estado era error: falló.
    CHECK(core::GuessOutcome(MakeJob(1, core::JobState::Error), 0, false) == JobOutcome::Failed);
}

// Atajos configurables: el texto de config.json ("Ctrl+Shift+P") se convierte
// a modificadores + código de tecla virtual (VK_*) y vuelve al mismo texto.
TEST(KeyChordParseAndFormat) {
    auto c = input::KeyChord::Parse(L"Ctrl+Shift+P");
    CHECK(c.has_value());
    CHECK(c->ctrl && c->shift && !c->alt && c->vk == 'P');
    CHECK_EQ(c->ToString(), std::wstring(L"Ctrl+Shift+P"));
    // No distingue mayúsculas; "Ctrl++" es Ctrl con la tecla + (VK_OEM_PLUS:
    // el segundo "+" es la tecla, no un separador); acepta nombres de teclas en
    // español ("Supr" = Delete).
    CHECK_EQ(input::KeyChord::Parse(L"ctrl+up")->vk, static_cast<UINT>(VK_UP));
    CHECK_EQ(input::KeyChord::Parse(L"Ctrl++")->vk, static_cast<UINT>(VK_OEM_PLUS));
    CHECK_EQ(input::KeyChord::Parse(L"F12")->vk, static_cast<UINT>(VK_F12));
    CHECK_EQ(input::KeyChord::Parse(L"Supr")->vk, static_cast<UINT>(VK_DELETE));
    // Modificador desconocido o texto vacío: inválido (el atajo de config.json
    // se ignora y queda el predeterminado). Una lista separada por comas da
    // varios atajos para el mismo comando.
    CHECK(!input::KeyChord::Parse(L"Hyper+X").has_value());
    CHECK(!input::KeyChord::Parse(L"").has_value());
    CHECK_EQ(input::ParseChordList(L"PageDown, Right").size(), size_t(2));
}

// Los atajos predeterminados son los que pidió el usuario en el TODO original
// (y documenta el MANUAL): si alguien cambia la tabla por error, esta prueba lo
// detecta.
TEST(ShortcutDefaultsFromTodo) {
    input::ShortcutMap map;
    // Comando asignado al atajo escrito como texto.
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

// Reasignar un atajo desde el panel F1: un atajo pertenece a un solo comando,
// los cambios se guardan en config.json como "diferencias" con lo
// predeterminado (Overrides) y se pueden volver a aplicar o restablecer.
TEST(ShortcutReassignAndOverrides) {
    input::ShortcutMap map;
    auto chord = *input::KeyChord::Parse(L"Ctrl+P");
    // Asignar Ctrl+P a Cancelar se lo quita a Pausar.
    auto previous = map.Assign(input::Command::Cancel, chord);
    CHECK(previous.has_value() && *previous == input::Command::Pause);
    CHECK(map.Get(input::Command::Pause).empty());
    // En config.json (claves = id estable del comando): Cancelar = Ctrl+P y
    // Pausar = "" (sin atajo; distinto de "no guardado", que sería el
    // predeterminado).
    auto overrides = map.Overrides();
    CHECK_EQ(overrides[L"cancel"], std::wstring(L"Ctrl+P"));
    CHECK_EQ(overrides[L"pause"], std::wstring(L""));

    // Al reiniciar la app: un mapa nuevo con esas diferencias queda igual que
    // el reasignado; "Restablecer" vuelve a dar Ctrl+P a Pausar.
    input::ShortcutMap restored;
    restored.ApplyOverrides(overrides);
    CHECK(restored.Find(chord) == input::Command::Cancel);
    CHECK(restored.Get(input::Command::Pause).empty());
    restored.ResetDefaults();
    CHECK(restored.Find(chord) == input::Command::Pause);
}

// Ningún atajo predeterminado está asignado a dos comandos: para cada atajo de
// cada comando, Find debe devolver ese mismo comando (si hubiera un duplicado,
// uno de los dos comandos sería inalcanzable con el teclado).
TEST(NoDuplicateDefaultShortcuts) {
    input::ShortcutMap map;
    for (int i = 0; i < input::kCommandCount; ++i)
        for (const auto& chord : map.Get(static_cast<input::Command>(i)))
            CHECK(map.Find(chord) == static_cast<input::Command>(i));
}

// Con el foco en el cuadro de búsqueda, las teclas que editan texto (letras,
// espacio, Ctrl+A para seleccionar todo) van al cuadro y no disparan atajos
// (Espacio sería Vista rápida). Las de navegación (flechas), los atajos con
// Ctrl que no son de edición y las teclas de función siguen siendo atajos.
TEST(TextEditingKeysGoToSearchBox) {
    CHECK(input::IsTextEditingChord(*input::KeyChord::Parse(L"A")));
    CHECK(input::IsTextEditingChord(*input::KeyChord::Parse(L"Space")));
    CHECK(input::IsTextEditingChord(*input::KeyChord::Parse(L"Ctrl+A")));
    CHECK(!input::IsTextEditingChord(*input::KeyChord::Parse(L"Down")));
    CHECK(!input::IsTextEditingChord(*input::KeyChord::Parse(L"Ctrl+P")));
    CHECK(!input::IsTextEditingChord(*input::KeyChord::Parse(L"F5")));
}

// Las paletas claro/oscuro (D8) se diseñaron a mano: esta prueba evita que un
// ajuste de color deje texto ilegible en alguno de los dos modos.
TEST(ThemeContrastWcagAA) {
    // Texto normal y tonos de estado deben cumplir WCAG AA (4.5:1) en ambos modos.
    for (const ui::Palette* p : {&ui::LightPalette(), &ui::DarkPalette()}) {
        // El texto principal pide 7:1 (nivel AAA); el secundario, la selección
        // y el texto sobre el color de acento, 4,5:1 (AA).
        CHECK(ui::ContrastRatio(p->text, p->surface) >= 7.0);
        CHECK(ui::ContrastRatio(p->textMuted, p->surface) >= 4.5);
        CHECK(ui::ContrastRatio(p->textMuted, p->window) >= 4.5);
        CHECK(ui::ContrastRatio(p->selectionText, p->selection) >= 4.5);
        CHECK(ui::ContrastRatio(p->accentText, p->accent) >= 4.5);
        // Cada tono de estado (insignias de la columna Estado, chips de
        // impresoras) debe leerse sobre la superficie y sobre su propio fondo.
        for (auto tone : {ui::Tone::Neutral, ui::Tone::Info, ui::Tone::Success, ui::Tone::Warning, ui::Tone::Danger,
                          ui::Tone::Special}) {
            CHECK(ui::ContrastRatio(p->ToneColor(tone), p->surface) >= 4.5);
            CHECK(ui::ContrastRatio(p->ToneColor(tone), p->ToneBackground(tone)) >= 4.5);
        }
    }
}

// Valores de referencia de la fórmula WCAG: negro sobre blanco es 21:1 (el
// máximo) y un color contra sí mismo, 1:1 (el mínimo). Si ContrastRatio
// estuviera mal, la prueba anterior no valdría nada.
TEST(ContrastRatioReference) {
    CHECK(ui::ContrastRatio(RGB(0, 0, 0), RGB(255, 255, 255)) > 20.9);
    CHECK(ui::ContrastRatio(RGB(10, 10, 10), RGB(10, 10, 10)) < 1.01);
}

// Filtro del cuadro de búsqueda (Ctrl+F): busca en documento, usuario, equipo,
// etc., sin distinguir mayúsculas. Varias palabras deben coincidir TODAS
// (cada una en cualquier campo): "JPEREZ pc07" encuentra el trabajo, pero
// "tesis mgomez" no, porque mgomez no aparece.
TEST(RowFilter) {
    core::JobInfo job = MakeJob(1, core::JobState::Paused);
    job.document = L"Tesis capítulo 3.docx";
    job.user = L"jperez";
    job.machine = L"\\\\LAB-PC07";
    // Filtro vacío: todo coincide.
    CHECK(app::JobMatchesFilter(job, L""));
    CHECK(app::JobMatchesFilter(job, L"tesis"));
    CHECK(app::JobMatchesFilter(job, L"JPEREZ pc07"));
    CHECK(!app::JobMatchesFilter(job, L"tesis mgomez"));

    // VisibleJobs ordena por posición real en la cola aunque el spooler los
    // entregue en otro orden; Summarize cuenta para la barra de estado.
    auto snapshot = MakeSnapshot({MakeJob(5, core::JobState::Queued, 2), MakeJob(4, core::JobState::Printing, 1)});
    auto visible = app::VisibleJobs(snapshot, app::kAllPrinters, L"");
    CHECK_EQ(visible.size(), size_t(2));
    CHECK_EQ(visible[0]->id, 4u);  // ordenado por posición
    auto summary = app::Summarize(visible);
    CHECK_EQ(summary.total, 2u);
    CHECK_EQ(summary.printing, 1u);
}

// Retención automática (D17): AutoHoldTracker::Process recibe los trabajos de
// una impresora y devuelve los ids que hay que pausar. Reglas: solo trabajos
// nuevos, cada uno se evalúa una sola vez (si el operador lo libera no se
// re-pausa) y nunca la copia local de CSR (D39). El último argumento es si la
// retención está activada.
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
    // La app los pausa y los marca como retenidos por ella (MarkHeld); Annotate
    // pone heldByApp en los JobInfo para que la fila diga "Retenido".
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
    // De los dos nuevos (7 local y 8 del servidor recibiéndose) solo se
    // retiene el 8. Pausar la copia local era la causa de las hojas en blanco
    // de la 0.4.8 (el trabajo nunca llegaba completo al servidor).
    ids = hold.Process(printer, {local, MakeJob(8, core::JobState::Spooling)}, true);
    CHECK_EQ(ids.size(), size_t(1));
    CHECK_EQ(ids[0], 8u);
}

// Posiciones de las columnas (ui::LayoutColumns, D36 y D40): anchos del
// operador, columna flexible (Documento) y que nunca se oculte ninguna.
TEST(ColumnLayoutWithUserWidths) {
    // A 96 DPI (1 DIP = 1 px): margen izquierdo 18, derecho 16, separación 12, flexible mín. 140.
    // Tres columnas: "a" fija de 100, "doc" flexible (ancho 0) y "b" fija de 100.
    std::vector<ui::ListColumn> cols(3);
    cols[0].width = 100;
    cols[0].id = L"a";
    cols[1].width = 0;  // flexible
    cols[1].id = L"doc";
    cols[2].width = 100;
    cols[2].id = L"b";

    // Sin anchos del operador la flexible ocupa lo que sobra.
    // Cuentas con 1000 px: "a" va de 18 a 118; "doc" empieza en 118 + 12 = 130.
    // Se reserva una separación detrás de cada columna (también la última), así
    // que el borde útil es 1000 - 16 = 984 y la flexible mide
    // 984 - 18 - (100+12 + 12 + 100+12) = 730: termina en 860 y "b" va de 872
    // a 972.
    auto slots = ui::LayoutColumns(cols, {}, 1000);
    CHECK_EQ(slots.size(), size_t(3));
    CHECK_EQ(slots[1].left, 130);
    CHECK_EQ(slots[1].right, 860);
    CHECK_EQ(slots[2].right, 972);

    // Agrandar una columna no oculta otras: se sale del ancho (barra horizontal).
    // "b" con 600 en una ventana de 500: siguen las 3 columnas y "b" pasa del
    // borde (bug de D40: antes desaparecían columnas).
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
    // "doc" mide 200 aunque sobre espacio, y "b" empieza en 130 + 200 + 12 = 342.
    slots = ui::LayoutColumns(cols, {{L"doc", 200}}, 1000);
    CHECK_EQ(slots[1].right - slots[1].left, 200);
    CHECK_EQ(slots[2].left, 342);
}

// Orden de columnas guardado (columnOrder, D38). NormalizeColumnOrder limpia lo
// que viene de config.json (puede ser de otra versión o estar editado a mano) y
// MergeColumnOrder combina lo que el operador movió en una vista con el orden
// completo guardado.
TEST(ColumnOrderNormalizeAndMerge) {
    using V = std::vector<std::wstring>;
    const V defaults = {L"number", L"document", L"status", L"user", L"printer", L"pages"};
    // Vacío = predeterminado; ids desconocidos o repetidos se descartan.
    CHECK(ui::NormalizeColumnOrder({}, defaults) == defaults);
    CHECK(ui::NormalizeColumnOrder({L"number", L"user", L"viejo", L"document", L"user", L"status", L"printer",
                                    L"pages"},
                                   defaults) == (V{L"number", L"user", L"document", L"status", L"printer", L"pages"}));
    // Una columna que falta (nueva) va detrás de la que la precede por defecto.
    // "printer" no está en lo guardado; en el predeterminado la precede "user",
    // así que se inserta justo después de "user" (al final, en este caso).
    CHECK(ui::NormalizeColumnOrder({L"pages", L"document", L"number", L"status", L"user"}, defaults) ==
          (V{L"pages", L"document", L"number", L"status", L"user", L"printer"}));

    // Mover en la vista sin Impresora no le cambia el lugar a Impresora.
    // La vista de una impresora (sin la columna Impresora) se reordenó con
    // Usuario primero; las columnas de la vista ocupan solo sus lugares y
    // Impresora sigue entre "status" y "pages", como antes.
    const V merged = ui::MergeColumnOrder(defaults, {L"user", L"number", L"document", L"status", L"pages"});
    CHECK(merged == (V{L"user", L"number", L"document", L"status", L"printer", L"pages"}));
}

// ColumnDisplayOrder traduce el orden guardado (ids) a índices de dibujo
// (JobListView dibuja en ese orden sin reordenar las celdas de las filas), y
// MoveDisplayColumn aplica el arrastre de un título.
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
    // "b" (índice 1) queda sin id: conserva su lugar (2.º) y d, c, a ocupan los
    // otros tres lugares en ese orden.
    cols[1].id.clear();
    CHECK(ui::ColumnDisplayOrder(cols, {L"d", L"c", L"a"}) == (I{3, 1, 2, 0}));

    // MoveDisplayColumn(orden, columna movida, columna ancla, después): la 0
    // detrás de la 3 queda al final; la 3 delante de la 0 queda al principio.
    const I display = {0, 1, 2, 3};
    CHECK(ui::MoveDisplayColumn(display, 0, 3, true) == (I{1, 2, 3, 0}));
    CHECK(ui::MoveDisplayColumn(display, 3, 0, false) == (I{3, 0, 1, 2}));
    CHECK(ui::MoveDisplayColumn(display, 1, 2, false) == display);  // ya estaba ahí
    CHECK(ui::MoveDisplayColumn(display, 1, 9, false) == display);  // ancla inexistente
}

// Guardado del orden de columnas desde una vista concreta (D38): una sola
// lista para cola e historial, vacía si coincide con el predeterminado.
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
    // (Sus columnas tienen ids propios "history.*", así que mover Usuario en la
    // cola no mueve el Usuario del historial.)
    auto history = app::HistoryColumns();
    CHECK(ui::ColumnDisplayOrder(history, app::EffectiveColumnOrder(saved)) ==
          ui::ColumnDisplayOrder(history, {}));
}

// Un trabajo pausado por la retención automática (heldByApp) se presenta como
// "Retenido", no como "En pausa": se puede buscar por esa palabra, cuenta en
// el resumen como retenido (no como pausado) y usa el tono Special.
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

// D18: al iniciar/conectar se retienen también los trabajos que ya estaban en
// la cola. El quinto argumento de Process (true) es "pausar todo al iniciar";
// solo afecta a la primera llamada de cada impresora.
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

// "Imprimir y volver a pausar" (Ctrl+Enter / F11, D35): PrintReleaseTracker
// sigue los trabajos liberados y, con cada instantánea, devuelve las
// impresoras que ya terminaron y deben volver a pausarse. Los tiempos (0, 1000,
// 2000... ms) son el reloj que se le pasa, para no depender del real.
TEST(PrintReleaseRepausesWhenAllSent) {
    core::PrintReleaseTracker release;
    const std::wstring p1 = L"\\\\S\\P1";
    // El 9 está en otra impresora (P2) para comprobar que cada una se decide
    // por separado.
    core::JobInfo other = MakeJob(9, core::JobState::Paused);
    other.printer = L"\\\\S\\P2";
    // Instantánea con P1 (con p1Count = cJobs que informa el spooler) y P2
    // (siempre 1 trabajo).
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
    // (cJobs = 2 y solo se enumeró 1: la lista está incompleta, D35/D21; no se
    // puede dar al 1 por terminado sin arriesgarse a pausar antes de tiempo.)
    CHECK(release.Update(snap({MakeJob(2, core::JobState::Queued), other}, 2), 3000).empty());
    CHECK_EQ(release.PendingJobs(), size_t(3));  // 1 y 2 de P1, 9 de P2
    // El 2 queda "enviado a la impresora": P1 ya se puede volver a pausar.
    // JOB_STATUS_COMPLETE = el spooler terminó de enviarlo (aunque siga en la
    // lista); ahora cJobs = 1 coincide con lo enumerado, así que el 1 ausente sí
    // terminó. P1 se devuelve una sola vez.
    core::JobInfo sent = MakeJob(2, core::JobState::Printing);
    sent.rawStatus = JOB_STATUS_COMPLETE;
    auto done = release.Update(snap({sent, other}, 1), 4000);
    CHECK_EQ(done.size(), size_t(1));
    CHECK(done[0] == p1);
    // El 9 nunca se reanudó (sin permisos): pasado el plazo se deja de esperar.
    // kResumeGraceMs (15 s) después de Start sigue en pausa: la reanudación
    // falló y P2 se vuelve a pausar para no dejarla abierta para siempre.
    // Ya no queda nada pendiente (Active() = false).
    done = release.Update(snap({other}, 0), core::PrintReleaseTracker::kResumeGraceMs);
    CHECK_EQ(done.size(), size_t(1));
    CHECK(done[0] == other.printer);
    CHECK(!release.Active());
}

// Otros criterios de D35: una instantánea poco fiable no decide nada (si no,
// los trabajos "desaparecidos" harían pausar la impresora antes de tiempo), y
// un trabajo que el operador vuelve a pausar después de verlo correr cuenta
// como terminado.
TEST(PrintReleaseJobPausedAgainOrSnapshotError) {
    core::PrintReleaseTracker release;
    // Se libera el 1 y en la primera instantánea ya está en cola (corriendo).
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

// Retención automática (D17) y pausa al iniciar (D18) vienen activadas por
// defecto (pedido del usuario: nada se imprime sin un operador), y apagar la
// pausa al iniciar se conserva en config.json.
TEST(SettingsHoldDefaults) {
    config::Settings s;
    CHECK(s.autoHoldNewJobs);
    CHECK(s.pauseAllOnStart);
    s.pauseAllOnStart = false;
    CHECK(!config::Settings::FromJson(s.ToJson()).pauseAllOnStart);
}

// Ctrl+P alterna: ShouldPauseJobs decide si el comando pausa o reanuda la
// selección. Es también lo que muestra el botón "Pausar / Reanudar trabajos"
// de la cabecera (D37).
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
    // (F8: trabajo + su impresora.) PrintersOf da cada impresora una vez, en
    // el orden en que aparece: P1 (de los trabajos 1 y 2) y P2.
    core::JobInfo other = MakeJob(3, core::JobState::Queued);
    other.printer = L"\\\\S\\P2";
    auto printers = app::PrintersOf({MakeJob(1, core::JobState::Queued), MakeJob(2, core::JobState::Queued), other});
    CHECK_EQ(printers.size(), size_t(2));
    CHECK_EQ(printers[1], std::wstring(L"\\\\S\\P2"));
}

// Atajos de pausa de D18: cada comando tiene un atajo con Ctrl y una tecla de
// función (más fácil de pulsar con una mano).
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
// En orden de cola (posición 1, 2, 3):
//   10: ana   en LAB-PC07, enviado 9:30,  100 bytes
//   11: bruno en LAB-PC02, enviado 8:15,  300 bytes
//   12: carla en LAB-PC02, enviado 10:45, 200 bytes
// Así cada criterio de orden da un resultado distinto del orden de la cola.
core::QueueSnapshot MakeMixedSnapshot() {
    // Hora del 22/9/2026 (fecha fija; solo importa la hora).
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

// Orden de la cola configurable (D20): es solo una vista (no cambia la
// prioridad en el spooler) y, a igualdad de criterio, se conserva el orden de
// impresión.
TEST(SortQueueByMachineAndTime) {
    auto snapshot = MakeMixedSnapshot();
    // Ids de los trabajos en el orden devuelto, para compararlos fácilmente.
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
    // Por usuario ascendente, "ana" (10) va primero; por tamaño ascendente,
    // 100 bytes (10) también.
    CHECK_EQ(ids(app::VisibleJobs(snapshot, app::kAllPrinters, L"", app::SortBy::User))[0], 10u);
    CHECK_EQ(ids(app::VisibleJobs(snapshot, app::kAllPrinters, L"", app::SortBy::Size))[0], 10u);
}

// El criterio de orden se guarda en config.json como texto (sortBy): cada
// criterio debe volver igual, un texto desconocido (de otra versión) cae en el
// orden de la cola y no se distinguen mayúsculas.
TEST(SortIdsRoundTrip) {
    for (int i = 0; i < app::kSortCount; ++i) {
        auto sort = static_cast<app::SortBy>(i);
        CHECK(app::SortByFromId(app::SortById(sort)) == sort);
    }
    CHECK(app::SortByFromId(L"criterio-inventado") == app::SortBy::Queue);
    CHECK(app::SortByFromId(L"MACHINE") == app::SortBy::Machine);
}

// Barra de acceso rápido a impresoras (D19): chips con "Todas" y las
// impresoras fijadas (favoritePrinters), cada uno con su cuenta de trabajos y
// un tono según el problema más grave.
TEST(QuickAccessPrinters) {
    // Los 3 trabajos de MakeMixedSnapshot están en P1; se agrega P2 vacía y
    // sin papel.
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
    // Se fijan P2, P1 y una de \\OTRO (queda guardada pero no aparece en este
    // servidor, es intencional): chips = Todas, P2 (0 trabajos), P1 (3).
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

// Orden de la cola, impresoras fijadas y visibilidad de la barra se guardan y
// vuelven igual desde config.json. Predeterminados: orden de cola ascendente y
// barra visible.
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
