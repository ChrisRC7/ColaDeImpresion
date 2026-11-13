// Implementación de la ventana principal (ver MainWindow.h). Es la capa de
// orquestación: crea los controles (ui/), arranca los servicios con hilos
// propios (QueueMonitor, PreviewService, JobFactsService, CarnetService,
// JobActionRunner), recibe sus resultados por mensajes WM_APP_* y decide qué
// mostrar, qué guardar y qué acción ejecutar ante cada evento del usuario.
//
// Reglas de hilos (GUIA_IA.md §4):
//  - Todo este archivo corre en el hilo de la UI, salvo el cuerpo de las
//    lambdas que se pasan como callback a los servicios (esas corren en el
//    hilo del servicio y SOLO hacen PostMessage/PostOwned a la ventana).
//  - Nunca se llama a OpenPrinter/EnumJobs/SetJob desde aquí: las acciones van
//    a JobActionRunner y la lectura de la cola a QueueMonitor.
//
// Organización del archivo: constantes y ayudantes; creación; configuración,
// tema e idioma; servidor e impresoras; datos (instantáneas, eventos,
// historial); vista previa; comandos y acciones; menús; ventana y mensajes.
#include "app/MainWindow.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <climits>
#include <set>

#include "app/HistoryExport.h"
#include "app/JobSheet.h"
#include "app/RowBuilder.h"
#include "config/Autostart.h"
#include "config/Paths.h"
#include "core/JobStatus.h"
#include "core/PageAccounting.h"
#include "core/Spooler.h"
#include "i18n/Strings.h"
#include "ui/DarkMode.h"
#include "ui/AboutDialog.h"
#include "ui/ExportDialog.h"
#include "ui/Gfx.h"
#include "ui/Style.h"
#include "util/Format.h"
#include "util/Log.h"
#include "util/Strings.h"

namespace cola::app {

using i18n::S;
using i18n::Tr;
using input::Command;
using ui::Style;

namespace {

// Mensajes internos (los datos viajan como punteros en lParam; el receptor los libera).
// Los envían los hilos de fondo; los recibe HandleMessage en el hilo de la UI.
// WM_APP_SNAPSHOT: lParam = core::SnapshotPtr* (instantánea nueva de la cola, QueueMonitor).
constexpr UINT WM_APP_SNAPSHOT = WM_APP + 1;
// WM_APP_PREVIEW: lParam = std::wstring* (clave de la vista previa que terminó, PreviewService).
constexpr UINT WM_APP_PREVIEW = WM_APP + 2;
// WM_APP_ACTION: lParam = ActionResult* (resultado de JobActionRunner).
constexpr UINT WM_APP_ACTION = WM_APP + 3;
// Los dos siguientes no llevan datos (lParam = 0): solo avisan que hay que
// volver a consultar el servicio.
constexpr UINT WM_APP_FACTS = WM_APP + 4;  // hay datos nuevos de trabajos (páginas x copias)
constexpr UINT WM_APP_CARNETS = WM_APP + 5;  // llegaron carnets del Active Directory

// Temporizadores (SetTimer sobre la ventana principal).
// kTimerSaveHistory: guarda historial.json un rato después del último cambio
// (agrupa varios cambios en una sola escritura).
constexpr UINT_PTR kTimerSaveHistory = 1;
// kTimerTransient: borra el mensaje temporal de la barra de estado.
constexpr UINT_PTR kTimerTransient = 2;

// Identificadores de menú.
// Rangos: los menús dinámicos (impresoras, servidores, destinos de reenvío,
// criterios de orden, impresoras a fijar) usan una base + índice en la lista
// que se guardó al armar el menú (menuPrinters_, menuServers_...).
// Los rangos no se superponen: HandleMenuCommand los distingue por el valor.
enum MenuId : UINT {
    ID_PRINTER_ALL = 1000,   // vista "Todas las impresoras"
    ID_PRINTER_BASE = 1001,  // 1001..1999
    // Menú principal (consecutivos desde 2001).
    ID_CONNECT = 2001,
    ID_REFRESH,
    ID_HISTORY,
    ID_PAUSE_PRINTER,
    ID_RESUME_PRINTER,
    ID_PURGE,
    ID_THEME_SYSTEM,
    ID_THEME_LIGHT,
    ID_THEME_DARK,
    ID_LANGUAGE,
    ID_PREVIEW_PANE,
    ID_CONFIRM_CANCEL,
    ID_NOTIFY_NONE,
    ID_NOTIFY_ERRORS,
    ID_NOTIFY_ALL,
    ID_START_WITH_WINDOWS,
    ID_MINIMIZE_TO_TRAY,
    ID_SHORTCUTS,
    ID_OPEN_DATA,
    ID_CLEAR_HISTORY,
    ID_EXPORT_HISTORY,
    ID_RUN_AS_ADMIN,
    ID_EXIT,
    ID_AUTO_HOLD,
    ID_PAUSE_ON_START,
    ID_PAUSE_BOTH,
    ID_PRINT_REPAUSE,
    ID_RESET_COLUMNS,
    ID_RESET_COLUMN_ORDER,
    ID_SORT_DESC,
    ID_SHOW_PRINTER_BAR,
    ID_PIN_CURRENT,
    // Menú contextual de un chip de la barra de impresoras (chipPrinter_).
    ID_CHIP_PIN,
    ID_CHIP_PAUSE,
    ID_CHIP_RESUME,
    ID_CHIP_PURGE,
    ID_ABOUT,
    ID_CONNECT_LAIMI,
    ID_SET_LAIMI,
    // Menú contextual de la lista de trabajos.
    ID_CTX_PAUSE = 2101,
    ID_CTX_PAUSE_BOTH,
    ID_CTX_PRINT_REPAUSE,
    ID_CTX_RESUME,
    ID_CTX_RESTART,
    ID_CTX_CANCEL,
    ID_CTX_UP,
    ID_CTX_DOWN,
    ID_CTX_REDIRECT,
    ID_CTX_PREVIEW,
    ID_CTX_CANCEL_USER,
    // Menú del ícono de la bandeja.
    ID_TRAY_SHOW = 2201,
    ID_TRAY_EXIT,
    ID_REDIRECT_BASE = 3000,  // 3000..3999
    ID_SERVER_BASE = 4000,    // 4000..4099
    ID_SORT_BASE = 4100,      // 4100..4119 (criterios de orden)
    ID_PIN_BASE = 4200,       // 4200..4999 (fijar impresoras en el acceso rápido)
};

// Envía `data` a la ventana con PostMessage, transfiriendo la propiedad del
// puntero al receptor (HandleMessage lo envuelve en un unique_ptr y lo libera).
// Si no hay ventana o PostMessage falla (cola llena, ventana destruida), nadie
// lo recibiría: se libera aquí para no perder memoria.
// Se llama desde los hilos de fondo (callbacks de los servicios).
template <typename T>
void PostOwned(HWND hwnd, UINT msg, T* data) {
    if (!hwnd || !PostMessageW(hwnd, msg, 0, reinterpret_cast<LPARAM>(data))) delete data;
}

// Nombre visible de una impresora (sin "\\SERVIDOR\").
std::wstring PrinterDisplay(const std::wstring& name) {
    core::PrinterInfo p;
    p.name = name;
    return p.DisplayName();
}

// Criterio de orden guardado en la configuración.
SortBy CurrentSort(const config::Settings& settings) { return SortByFromId(settings.sortBy); }

// true si el proceso corre elevado (como administrador con UAC). Se usa para
// ocultar/mostrar "Reiniciar como administrador" (D15). Ante cualquier error
// se asume que no.
bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                    elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

}  // namespace

// Mensaje registrado (único en todo el sistema para ese nombre): lo usa una
// segunda instancia para pedirle a esta que se muestre (Application.cpp).
// Se registra una sola vez (static local, inicialización segura entre hilos).
UINT MainWindow::ShowInstanceMessage() {
    static UINT message = RegisterWindowMessageW(L"ColaDeImpresion.ShowInstance");
    return message;
}

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() {
    // Detiene los hilos antes de destruir los controles.
    // (Cada reset espera a que su hilo termine; así ningún callback hace
    // PostMessage a una ventana a medio destruir. facts_ y carnets_ se
    // destruyen después, con el resto de los miembros.)
    monitor_.reset();
    previews_.reset();
    actions_.reset();
}

// Crea la ventana y todo lo que cuelga de ella. Hilo de la UI. Pasos:
//  1. Carga config.json y aplica las opciones de línea de comandos.
//  2. Carga el historial y detecta los renderizadores externos (Ghostscript...).
//  3. Íconos, tema (claro/oscuro) y la ventana Win32.
//  4. Controles propios con sus callbacks (todo lo que hace el usuario entra por ahí).
//  5. Servicios con hilos de fondo, cuyos callbacks solo hacen PostMessage.
//  6. Ícono de bandeja, conexión al servidor y posición de la ventana.
// Devuelve false si no se pudo crear la ventana (Application termina con 1).
bool MainWindow::Create(const StartupOptions& options) {
    settings_ = config::Settings::Load(config::SettingsFile());
    // El panel de detalles (vista previa y configuración) arranca siempre oculto:
    // la tabla usa todo el ancho y se abre con el botón de la cabecera, Ctrl+B o F7.
    settings_.showPreviewPane = false;
    // --laimi tiene prioridad sobre --server; sin ninguno se conecta al
    // último servidor usado (settings_.activeServer).
    if (options.laimi) settings_.activeServer = config::LaimiServer(settings_);
    else if (options.hasServer) settings_.activeServer = options.server;
    // El "Iniciar con Windows" real es la clave Run del registro, no lo que
    // diga config.json (alguien pudo quitarla a mano).
    settings_.startWithWindows = config::IsAutostartEnabled();
    i18n::SetLanguage(i18n::LanguageFromCode(settings_.language.c_str()));
    // Atajos personalizados del operador sobre los predeterminados.
    shortcuts_.ApplyOverrides(settings_.shortcuts);
    // historial.json (D13, D41): si está dañado, Load lo copia aparte (D52).
    history_ = config::History(config::HistoryFile(), static_cast<size_t>(settings_.historyLimit));
    history_.Load();
    // Ghostscript/GhostPCL/GhostXPS: rutas configuradas o autodetectadas (D7).
    renderers_ = preview::DetectRenderers({settings_.ghostscriptPath, settings_.ghostpclPath, settings_.ghostxpsPath});
    util::LogInfo(L"Renderizadores: gs='%ls' pcl='%ls' xps='%ls'", renderers_.ghostscript.c_str(),
                  renderers_.ghostpcl.c_str(), renderers_.ghostxps.c_str());

    // Ícono del recurso 1 (res/app.rc) en tamaño grande y pequeño según el
    // sistema; si falta el recurso, el ícono genérico de aplicación.
    HINSTANCE instance = GetModuleHandleW(nullptr);
    icon_ = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                          GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    iconSmall_ = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                               LR_DEFAULTCOLOR));
    if (!icon_) icon_ = LoadIconW(nullptr, IDI_APPLICATION);
    if (!iconSmall_) iconSmall_ = icon_;

    // Tema antes de crear la ventana: los menús y barras de desplazamiento
    // oscuros (D9) se aplican a nivel de aplicación.
    const bool dark = ui::ResolveDark(settings_.theme);
    Style::Get().SetDark(dark);
    ui::ApplyAppDarkMode(dark);

    // WS_CLIPCHILDREN: el fondo de la ventana no pinta sobre los controles
    // hijos (evita parpadeo).
    if (!CreateWin(kClassName, Tr(S::AppTitle), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, CW_USEDEFAULT, CW_USEDEFAULT,
                   settings_.window.width, settings_.window.height, nullptr, nullptr, icon_))
        return false;
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall_));
    // Permite recibir estos mensajes aunque vengan de un proceso con menor
    // nivel de integridad (UIPI): si esta instancia corre como administrador,
    // un doble clic normal en el acceso directo igual puede traerla al frente,
    // y el Explorer puede avisar que se recreó la barra de tareas.
    ChangeWindowMessageFilterEx(hwnd_, ShowInstanceMessage(), MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd_, ui::TrayIcon::TaskbarCreatedMessage(), MSGFLT_ALLOW, nullptr);

    Style::Get().SetDpi(GetDpiForWindow(hwnd_));
    ui::ApplyWindowDarkMode(hwnd_, dark);

    // Controles.
    // Cabecera: botones (el callback recibe cuál y su rectángulo para abrir
    // los menús justo debajo) y cuadro de búsqueda (cada cambio de texto
    // filtra la lista; en modo "servidor" lo maneja HeaderBar).
    header_.Create(hwnd_, {[this](ui::HeaderButton b, RECT r) {
                               POINT below{r.left, r.bottom + Style::Get().Px(4)};
                               switch (b) {
                                   // Los botones de pausa ejecutan los mismos comandos
                                   // que Ctrl+P y F9 (D37).
                                   case ui::HeaderButton::Printer: ShowPrinterMenu(below); break;
                                   case ui::HeaderButton::History: ToggleHistory(); break;
                                   case ui::HeaderButton::Help: Execute(Command::Help); break;
                                   case ui::HeaderButton::Theme: ToggleTheme(); break;
                                   case ui::HeaderButton::Menu: ShowMainMenu(below); break;
                                   case ui::HeaderButton::Details: Execute(Command::TogglePane); break;
                                   case ui::HeaderButton::PauseJobs: Execute(Command::Pause); break;
                                   case ui::HeaderButton::PausePrinter: Execute(Command::PausePrinter); break;
                                   case ui::HeaderButton::Export: ExportHistory(); break;
                                   case ui::HeaderButton::About: ui::ShowAboutDialog(hwnd_); break;
                                   default: break;
                               }
                           },
                           [this](const std::wstring& text) { SetFilter(text); }});
    // Barra de acceso rápido (D19): clic = elegir impresora; clic derecho en
    // un chip = su menú; clic derecho en el fondo = menú de la barra.
    printerBar_.Create(hwnd_, {[this](const std::wstring& printer) { SelectPrinter(printer); },
                               [this](const std::wstring& printer, POINT pt) { ShowPrinterChipMenu(printer, pt); },
                               [this](POINT pt) { ShowQuickAccessMenu(pt); }});
    // Lista de trabajos. Callbacks, en orden: cambió la selección; menú
    // contextual; Espacio/Enter (vista rápida); clic en un título (ordenar,
    // D30); ancho de columna cambiado (D36); clic derecho en los títulos;
    // columnas reordenadas (D38).
    list_.Create(hwnd_, {[this] {
                             UpdatePreview();
                             UpdatePauseButtons();
                             UpdateSelectionTotal();
                         },
                         [this](POINT pt) { ShowJobContextMenu(pt); }, [this] { ToggleQuickLook(); },
                         [this](int sortId) { OnColumnClicked(sortId); },
                         [this](const std::wstring& id, int width) {
                             settings_.columnWidths[id] = width;
                             SaveSettings();
                         },
                         [this](POINT pt) { ShowColumnHeaderMenu(pt); },
                         [this](const std::vector<std::wstring>& order) {
                             // order = ids de la vista actual en el orden nuevo;
                             // se fusiona con el guardado y se vuelve a aplicar
                             // ya normalizado.
                             settings_.columnOrder = SaveColumnOrder(settings_.columnOrder, order);
                             list_.SetColumnOrder(EffectiveColumnOrder(settings_.columnOrder));
                             SaveSettings();
                         }});
    list_.SetColumnWidths(settings_.columnWidths);
    list_.SetColumnOrder(EffectiveColumnOrder(settings_.columnOrder));
    preview_.Create(hwnd_);
    status_.Create(hwnd_);
    // Panel de ayuda (F1) con reasignación de atajos. Callbacks: asignar un
    // atajo (devuelve el aviso si se lo quitó a otro comando), quitarlo,
    // restablecer todos y cerrar el panel. Cada cambio se guarda enseguida.
    help_.Create(hwnd_, &shortcuts_,
                 {[this](Command command, const input::KeyChord& chord) -> std::wstring {
                      auto previous = shortcuts_.Assign(command, chord);
                      settings_.shortcuts = shortcuts_.Overrides();
                      SaveSettings();
                      if (previous)
                          return util::Format(Tr(S::ShortcutConflict), chord.ToString().c_str(),
                                              Tr(input::GetCommandInfo(*previous).label));
                      return L"";
                  },
                  [this](Command command) {
                      shortcuts_.Clear(command);
                      settings_.shortcuts = shortcuts_.Overrides();
                      SaveSettings();
                  },
                  [this] {
                      shortcuts_.ResetDefaults();
                      settings_.shortcuts.clear();
                      SaveSettings();
                  },
                  [this] {
                      help_.Close();
                      SetFocus(list_.hwnd());
                  }});

    // Módulos con hilos: publican resultados a la ventana por PostMessage.
    // Las lambdas capturan solo el HWND (no `this`): corren en el hilo del
    // servicio y no deben tocar miembros de la ventana.
    HWND self = hwnd_;
    monitor_ = std::make_unique<core::QueueMonitor>(
        [self](core::SnapshotPtr s) { PostOwned(self, WM_APP_SNAPSHOT, new core::SnapshotPtr(std::move(s))); });
    previews_ = std::make_unique<preview::PreviewService>(
        [self](const std::wstring& key) { PostOwned(self, WM_APP_PREVIEW, new std::wstring(key)); });
    facts_ = std::make_unique<preview::JobFactsService>([self] { PostMessageW(self, WM_APP_FACTS, 0, 0); });
    carnets_ = std::make_unique<core::CarnetService>([self] { PostMessageW(self, WM_APP_CARNETS, 0, 0); });
    // Registros viejos del historial sin carnet: se piden ya (D56).
    RequestHistoryCarnets();
    actions_ = std::make_unique<JobActionRunner>(
        [self](ActionResult r) { PostOwned(self, WM_APP_ACTION, new ActionResult(std::move(r))); });

    tray_.Add(hwnd_, iconSmall_, Tr(S::AppTitle));

    // Medidas y colores de todos los controles, luego la conexión (arranca
    // el monitor) y por último mostrar la ventana (o dejarla oculta con --tray).
    OnStyleChanged();
    ConnectTo(settings_.activeServer);
    RestoreWindowPlacement(!options.startInTray);
    SetFocus(list_.hwnd());
    return true;
}

// ---------------------------------------------------------------------------
// Configuración, tema e idioma
// ---------------------------------------------------------------------------

// Escribe config.json (por usuario, D49) con el estado actual. Se llama tras
// cada cambio de preferencia; el idioma se toma del módulo i18n.
void MainWindow::SaveSettings() {
    settings_.language = i18n::LanguageCode(i18n::GetLanguage());
    settings_.Save(config::SettingsFile());
}

// Copia la posición "normal" (sin maximizar) y si está maximizada a
// settings_.window. No escribe el archivo: lo hace quien llama.
void MainWindow::SaveWindowPlacement() {
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(hwnd_, &wp)) return;
    const RECT& r = wp.rcNormalPosition;
    settings_.window.x = r.left;
    settings_.window.y = r.top;
    settings_.window.width = r.right - r.left;
    settings_.window.height = r.bottom - r.top;
    settings_.window.maximized = wp.showCmd == SW_SHOWMAXIMIZED || (IsZoomed(hwnd_) && IsWindowVisible(hwnd_));
}

// Pone la ventana donde estaba la última vez. show = false (inicio en la
// bandeja) la deja oculta pero con la posición correcta para cuando se muestre.
void MainWindow::RestoreWindowPlacement(bool show) {
    const auto& w = settings_.window;
    RECT r{w.x, w.y, w.x + w.width, w.y + w.height};
    // Válida si hay una posición guardada y cae en algún monitor conectado
    // (un monitor que ya no está dejaría la ventana fuera de la pantalla).
    bool valid = w.x != config::WindowPlacement::CW_USEDEFAULT_VALUE && MonitorFromRect(&r, MONITOR_DEFAULTTONULL);
    if (valid) {
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        wp.rcNormalPosition = r;
        wp.showCmd = show ? (w.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL) : SW_HIDE;
        SetWindowPlacement(hwnd_, &wp);
    } else {
        // Centrada en el monitor principal, escalada al DPI.
        int width = Style::Get().Px(1100), height = Style::Get().Px(680);
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        width = std::min<int>(width, work.right - work.left);
        height = std::min<int>(height, work.bottom - work.top);
        SetWindowPos(hwnd_, nullptr, work.left + (work.right - work.left - width) / 2,
                     work.top + (work.bottom - work.top - height) / 2, width, height, SWP_NOZORDER);
        if (show) ShowWindow(hwnd_, SW_SHOW);
    }
    if (show) UpdateWindow(hwnd_);
}

// Aplica settings_.theme (Sistema/Claro/Oscuro) a la paleta, a los menús
// (uxtheme, D9), a la barra de título (DWM) y redibuja todo.
void MainWindow::ApplyTheme() {
    const bool dark = ui::ResolveDark(settings_.theme);
    Style::Get().SetDark(dark);
    ui::ApplyAppDarkMode(dark);
    ui::ApplyWindowDarkMode(hwnd_, dark);
    OnStyleChanged();
}

// Tras cambiar el idioma: los controles vuelven a medir sus textos, las filas
// se rearman con los textos nuevos y el panel de detalles se rehace.
void MainWindow::ApplyLanguage() {
    OnStyleChanged();
    RebuildRows();
    UpdatePreview();
}

// Propaga un cambio de estilo (tema, DPI o idioma) a todos los controles,
// recalcula el diseño y refresca cabecera, barra de estado y título.
void MainWindow::OnStyleChanged() {
    header_.OnStyleChanged();
    printerBar_.OnStyleChanged();
    list_.OnStyleChanged();
    preview_.OnStyleChanged();
    status_.OnStyleChanged();
    help_.OnStyleChanged();
    Layout();
    UpdateHeader();
    UpdateStatus();
    UpdateTitle();
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
}

// Botón de tema de la cabecera: alterna entre claro y oscuro explícitos
// (deja de seguir al sistema).
void MainWindow::ToggleTheme() {
    settings_.theme = Style::Get().dark() ? config::ThemeMode::Light : config::ThemeMode::Dark;
    ApplyTheme();
    SaveSettings();
}

// ---------------------------------------------------------------------------
// Servidor e impresoras
// ---------------------------------------------------------------------------

void MainWindow::SetLaimiServer(const std::wstring& text) {
    // Vacío = el predeterminado. Se guarda en servidor-laimi.txt (para todos los
    // usuarios de esta copia y para el .bat); si no se puede escribir junto al
    // programa, solo para este usuario (config.json). D54.
    // ServerFromText normaliza lo escrito: primera línea útil, siempre con
    // las dos barras invertidas delante (acepta "vtec-laimiprint" sin ellas).
    std::wstring server = config::ServerFromText(util::ToUtf8(text));
    if (server.empty()) server = config::kDefaultLaimiServer;
    // shared = se pudo escribir servidor-laimi.txt junto al .exe. En ese caso
    // se borra laimiServer de config.json, que si no tendría prioridad
    // (config::LaimiServer lee primero config.json).
    const bool shared = config::SaveLaimiServerFile(server);
    settings_.laimiServer = shared ? L"" : server;
    SaveSettings();
    util::LogInfo(L"Servidor del LAIMI: %ls (%ls)", server.c_str(), shared ? L"servidor-laimi.txt" : L"config.json");
    ConnectTo(server);
    // Aviso en amarillo si quedó solo para este usuario.
    ShowTransient(util::Format(Tr(shared ? S::LaimiServerSaved : S::LaimiServerSavedUser), server.c_str()),
                  shared ? ui::Tone::Neutral : ui::Tone::Warning);
}

// Conecta (o reconecta) al servidor de impresión ("" = este equipo, D4).
// Hilo de la UI: solo reinicia el estado y le pide al QueueMonitor que
// cambie de servidor; la primera instantánea llega luego por WM_APP_SNAPSHOT.
void MainWindow::ConnectTo(const std::wstring& serverIn) {
    const std::wstring server = core::spooler::NormalizeServerName(serverIn);
    // También cuenta como cambio si todavía no hubo ninguna instantánea
    // (primera conexión al iniciar).
    const bool changed = server != settings_.activeServer || !snapshot_;
    // El monitor ya vigila este servidor (F7): reiniciarlo haría que viera
    // todas las impresoras "por primera vez" y, con "pausar todo al iniciar",
    // volviera a retener los trabajos que el operador ya liberó. Se comprueba
    // antes de tocar settings_: basta un refresco (la reconexión tras un error
    // la hace el propio monitor en cada sondeo).
    const bool sameMonitor = !changed && monitor_->IsRunning() && monitor_->server() == server;
    settings_.activeServer = server;
    // Lista de servidores recientes (el más reciente primero).
    auto& list = settings_.servers;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const std::wstring& s) { return util::EqualsInsensitive(s, server); }),
               list.end());
    list.insert(list.begin(), server);
    if (list.size() > 8) list.resize(8);  // como mucho 8 recientes en el menú

    // Servidor nuevo: se olvida todo lo del anterior (instantánea, eventos
    // pendientes, impresoras de F11, cachés de vista previa y de datos,
    // permisos y problemas ya avisados).
    if (changed) {
        snapshot_.reset();
        tracker_.Reset();
        release_.Reset();
        previews_->Clear();
        facts_->Clear();
        adminRights_.clear();
        lastProblems_.clear();
        connecting_ = true;
        // D18: al conectar se pausan todas las impresoras, pero recién
        // cuando llegue la primera instantánea (hay que saber cuáles son).
        startPausePending_ = settings_.pauseAllOnStart;
        // La impresora elegida era de otro servidor (su nombre no empieza
        // con el del servidor nuevo): se vuelve a "Todas".
        if (!settings_.activePrinter.empty() && settings_.activePrinter != kAllPrinters &&
            settings_.activePrinter.rfind(server, 0) != 0)
            settings_.activePrinter = kAllPrinters;
    }
    // Retención automática de trabajos nuevos (D17) y de los ya encolados al
    // conectar (D18); Start reinicia el hilo del monitor con el servidor nuevo.
    monitor_->SetAutoHold(settings_.autoHoldNewJobs);
    monitor_->SetHoldExistingOnStart(settings_.pauseAllOnStart);
    if (sameMonitor) {
        monitor_->SetPollInterval(settings_.pollIntervalMs);
        monitor_->RequestRefresh();
    } else {
        monitor_->Start(server, settings_.pollIntervalMs);
    }
    SaveSettings();
    RebuildRows();
    UpdateHeader();
    UpdatePrinterBar();
    UpdateStatus();
    UpdateTitle();
    UpdatePreview();
}

// Nombre del servidor para la cabecera y el título ("Este equipo" si es local).
std::wstring MainWindow::ServerDisplayName() const {
    return settings_.activeServer.empty() ? std::wstring(Tr(S::LocalServer)) : settings_.activeServer;
}

// Cambia la impresora que se muestra (nombre completo o kAllPrinters) y
// refresca todo lo que depende de ella. Se guarda en config.json para abrir
// en la misma vista la próxima vez.
void MainWindow::SelectPrinter(const std::wstring& printer) {
    settings_.activePrinter = printer;
    SaveSettings();
    RebuildRows();
    UpdateHeader();
    UpdatePrinterBar();
    UpdateStatus();
    UpdatePreview();
}

// --- Acceso rápido a impresoras -------------------------------------------

void MainWindow::UpdatePrinterBar() {
    // La visibilidad la decide Layout(); aquí solo se actualiza el contenido.
    if (!settings_.showPrinterBar || historyView_) return;
    // Antes de la primera instantánea se arma con una cola vacía (solo "Todas").
    core::QueueSnapshot empty;
    printerBar_.SetChips(BuildQuickAccess(snapshot_ ? *snapshot_ : empty, settings_.favoritePrinters),
                         settings_.activePrinter);
}

// Fija o quita una impresora de la barra de acceso rápido (D19).
void MainWindow::TogglePinnedPrinter(const std::wstring& printer) {
    // "Todas" no se puede fijar (siempre es el primer chip).
    if (printer.empty() || printer == kAllPrinters) {
        ShowTransient(Tr(S::NoPrinters), ui::Tone::Warning);
        return;
    }
    const bool pinned = IsPinned(settings_.favoritePrinters, printer);
    settings_.favoritePrinters = TogglePinned(settings_.favoritePrinters, printer);
    // Fijar con la barra oculta la muestra: si no, el operador no vería el efecto.
    if (!pinned && !settings_.showPrinterBar) settings_.showPrinterBar = true;
    SaveSettings();
    // Layout por si la barra apareció o cambió de alto.
    Layout();
    UpdatePrinterBar();
    ShowTransient(std::wstring(Tr(pinned ? S::MenuUnpinPrinter : S::MenuPinPrinter)) + L": " +
                      PrinterDisplay(printer),
                  ui::Tone::Neutral);
}

bool MainWindow::SelectQuickAccess(int index) {
    // index 1..9 sobre las impresoras fijadas que existen en este servidor.
    // chips[0] es "Todas", así que el índice coincide directamente con Ctrl+N.
    // Devuelve false si no hay impresora en esa posición (la tecla no se consume).
    if (!snapshot_ || index < 1) return false;
    auto chips = BuildQuickAccess(*snapshot_, settings_.favoritePrinters);
    if (static_cast<size_t>(index) >= chips.size()) return false;
    SelectPrinter(chips[static_cast<size_t>(index)].name);
    return true;
}

// Menú de la barra de acceso rápido (clic derecho en su fondo): todas las
// impresoras del servidor con una marca en las fijadas (elegir una alterna
// su fijación, ID_PIN_BASE + índice en menuPrinters_) y "Mostrar la barra".
void MainWindow::ShowQuickAccessMenu(POINT screen) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, Tr(S::MenuQuickAccessHint));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    menuPrinters_.clear();
    if (snapshot_) {
        for (const auto& p : snapshot_->printers) {
            const bool pinned = IsPinned(settings_.favoritePrinters, p.name);
            std::wstring label = p.DisplayName();
            // Muestra el atajo Ctrl+N que le toca a cada impresora fijada.
            // (Su posición entre las fijadas que existen en este servidor,
            // igual que en BuildQuickAccess; el tabulador alinea el atajo a
            // la derecha del menú.)
            if (pinned) {
                size_t position = 0;
                for (const auto& fav : settings_.favoritePrinters) {
                    if (!snapshot_->FindPrinter(fav)) continue;
                    ++position;
                    if (util::EqualsInsensitive(fav, p.name)) break;
                }
                if (position >= 1 && position <= 9) label += L"	Ctrl+" + std::to_wstring(position);
            }
            AppendMenuW(menu, MF_STRING | (pinned ? MF_CHECKED : 0), ID_PIN_BASE + menuPrinters_.size(),
                        label.c_str());
            menuPrinters_.push_back(p.name);
            // El rango ID_PIN_BASE (4200..4999) admite 800 ids.
            if (menuPrinters_.size() >= 799) break;
        }
    }
    if (menuPrinters_.empty()) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, Tr(S::NoPrinters));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (settings_.showPrinterBar ? MF_CHECKED : 0), ID_SHOW_PRINTER_BAR,
                Tr(S::MenuShowPrinterBar));
    // TPM_RETURNCMD: TrackPopupMenu devuelve el id elegido (0 = nada) en vez
    // de enviar WM_COMMAND; así todos los menús pasan por HandleMenuCommand.
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (id) HandleMenuCommand(id);
}

// Menú contextual de un chip: fijar/quitar, pausar, reanudar y vaciar esa
// impresora. En el chip "Todas" pausar/reanudar afectan a todas y no hay
// fijar ni vaciar. La impresora se guarda en chipPrinter_ para HandleMenuCommand.
void MainWindow::ShowPrinterChipMenu(const std::wstring& printer, POINT screen) {
    chipPrinter_ = printer;
    HMENU menu = CreatePopupMenu();
    const bool all = printer == kAllPrinters;
    if (!all) {
        AppendMenuW(menu, MF_STRING, ID_CHIP_PIN,
                    Tr(IsPinned(settings_.favoritePrinters, printer) ? S::MenuUnpinPrinter : S::MenuPinPrinter));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, ID_CHIP_PAUSE, Tr(S::MenuPausePrinter));
    AppendMenuW(menu, MF_STRING, ID_CHIP_RESUME, Tr(S::MenuResumePrinter));
    if (!all) AppendMenuW(menu, MF_STRING, ID_CHIP_PURGE, Tr(S::MenuPurge));
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (id) HandleMenuCommand(id);
}

// Pasa a la impresora siguiente (delta = +1) o anterior (-1) en el orden
// "Todas", impresora 1, impresora 2...; da la vuelta en los extremos.
void MainWindow::CyclePrinter(int delta) {
    std::vector<std::wstring> options = {kAllPrinters};
    if (snapshot_)
        for (const auto& p : snapshot_->printers) options.push_back(p.name);
    auto it = std::find(options.begin(), options.end(), settings_.activePrinter);
    int index = it == options.end() ? 0 : static_cast<int>(it - options.begin());
    int count = static_cast<int>(options.size());
    // Módulo positivo aunque index + delta sea negativo.
    SelectPrinter(options[static_cast<size_t>(((index + delta) % count + count) % count)]);
}

// La impresora elegida; en "Todas", la del trabajo con el foco; "" si no hay.
std::wstring MainWindow::EffectivePrinter() const {
    if (settings_.activePrinter != kAllPrinters && !settings_.activePrinter.empty()) return settings_.activePrinter;
    if (const auto* job = FocusedJob()) return job->printer;
    return L"";
}

// ---------------------------------------------------------------------------
// Datos de la cola
// ---------------------------------------------------------------------------

// Llega una instantánea nueva de la cola (WM_APP_SNAPSHOT, hilo de la UI).
// Es el corazón del flujo de datos (CONTEXTO §3.2): eventos e historial,
// problemas de impresoras, F11, análisis de datos, carnets, pausa inicial,
// permisos y finalmente la lista y los indicadores.
void MainWindow::OnSnapshot(core::SnapshotPtr snapshot) {
    // Puede llegar una instantánea en vuelo del servidor anterior tras un
    // ConnectTo: se descarta.
    if (!snapshot || snapshot->server != settings_.activeServer) return;  // de un servidor anterior
    snapshot_ = std::move(snapshot);
    connecting_ = false;

    // JobTracker compara con la instantánea anterior y devuelve los eventos
    // (nuevo, error, terminó con su resultado, D13/D47).
    // El orden importa (D41): ProcessEvents arma el historial con los datos
    // analizados en caché ANTES de que facts_->Request olvide los trabajos
    // que salieron de la cola.
    ProcessEvents(tracker_.Update(*snapshot_));
    CheckPrinterProblems();
    // F11 (D35): vuelve a pausar las impresoras cuyos trabajos liberados ya salieron.
    RepauseReleasedPrinters();
    // Copias reales de cada trabajo (lectura del inicio de sus datos, una vez por trabajo).
    // (D31: en segundo plano; cuando hay resultados llega WM_APP_FACTS.)
    facts_->Request(snapshot_->jobs, settings_.activeServer, SpoolOverrideDir());
    // Carnet de los usuarios nuevos (una consulta al directorio por usuario y sesión).
    {
        std::vector<std::wstring> users;
        for (const auto& job : snapshot_->jobs) users.push_back(job.user);
        carnets_->Request(users);
    }

    // "Al iniciar, pausar todo": pausa las impresoras apenas se conoce la lista.
    // (Los trabajos en cola los retiene el monitor en su primer refresco.)
    // Solo con una instantánea sin error y con impresoras: si la primera
    // falló, se reintenta con la siguiente. Se pausan solo las que no lo
    // están; el resultado se informa en la barra de estado sin diálogos
    // (startPauseInFlight_, D18: puede fallar por permisos).
    if (startPausePending_ && snapshot_->error.empty() && !snapshot_->printers.empty()) {
        startPausePending_ = false;
        std::vector<std::wstring> running;
        for (const auto& p : snapshot_->printers)
            if (!(p.problems & core::ProblemPaused)) running.push_back(p.name);
        if (!running.empty()) {
            startPauseInFlight_ = true;
            actions_->RunOnPrinters(JobAction::PausePrinter, std::move(running));
        }
    }

    // Verifica permisos de administración de impresoras nuevas.
    std::vector<std::wstring> unknown;
    for (const auto& p : snapshot_->printers) {
        if (!adminRights_.count(p.name)) {
            adminRights_[p.name] = true;  // optimista hasta que llegue la respuesta
            unknown.push_back(p.name);
        }
    }
    if (!unknown.empty()) actions_->CheckAdmin(std::move(unknown));

    // Si la impresora elegida ya no existe, vuelve a "todas".
    // (Solo si la instantánea no tiene error: con un error global la lista
    // de impresoras viene vacía y no significa que se haya borrado.)
    if (settings_.activePrinter != kAllPrinters && !snapshot_->FindPrinter(settings_.activePrinter) &&
        snapshot_->error.empty())
        settings_.activePrinter = kAllPrinters;

    // En la vista de historial la lista no muestra la cola: no se rearma
    // (ProcessEvents ya la rearmó si cambió el historial).
    if (!historyView_) RebuildRows();
    UpdateHeader();
    UpdatePrinterBar();
    UpdateStatus();
    UpdatePreview();

    // Descripción del ícono de bandeja: "Cola de impresión — N trabajos".
    unsigned total = static_cast<unsigned>(snapshot_->jobs.size());
    tray_.SetTooltip(std::wstring(Tr(S::AppTitle)) + L" — " +
                     (total == 1 ? std::wstring(Tr(S::StatusOneJob)) : util::Format(Tr(S::StatusJobs), total)));
}

// Reacciona a los eventos de JobTracker: notificaciones de la bandeja según
// settings_.notifications (Ninguna / Solo errores / Todas) y registros nuevos
// del historial. Hilo de la UI.
void MainWindow::ProcessEvents(const std::vector<core::JobEvent>& events) {
    bool historyChanged = false;
    for (const auto& e : events) {
        switch (e.kind) {
            // Trabajo nuevo en la cola.
            case core::JobEvent::Kind::Added:
                // Un trabajo retenido requiere acción del operador: se avisa salvo con "ninguna".
                if (e.job.heldByApp && settings_.notifications != config::NotifyMode::None)
                    Notify(PrinterDisplay(e.job.printer),
                           util::Format(Tr(S::NotifyHeldJob), e.job.user.c_str(), e.job.document.c_str()), false);
                else if (settings_.notifications == config::NotifyMode::All)
                    Notify(Tr(S::AppTitle), util::Format(Tr(S::NotifyNewJob), e.job.user.c_str(), e.job.document.c_str()),
                           false);
                break;
            // El trabajo pasó a estado de error (aviso en rojo salvo con "ninguna").
            case core::JobEvent::Kind::EnteredError:
                if (settings_.notifications != config::NotifyMode::None)
                    Notify(PrinterDisplay(e.job.printer), util::Format(Tr(S::NotifyJobError), e.job.document.c_str()),
                           true);
                break;
            // El trabajo salió de la cola; e.outcome = resultado deducido
            // (Impreso / Cancelado / Falló / Retirado).
            case core::JobEvent::Kind::Finished:
                if (settings_.keepHistory) {
                    // El registro congela lo que mostraba su fila (D41) y el
                    // carnet si ya se conoce (si no, se completa luego, D56).
                    // Los datos analizados del trabajo siguen en caché: facts_->Request
                    // olvida los que salieron de la cola recién después de esto.
                    const auto facts = facts_->Find(e.job);
                    config::HistoryEntry entry =
                        MakeHistoryEntry(e.job, e.outcome, util::ToUnixSeconds(util::NowUtc()), facts.get(),
                                         snapshot_ ? snapshot_->FindPrinter(e.job.printer) : nullptr,
                                         carnets_->Find(e.job.user));
                    // MakeHistoryEntry no conoce el servidor: se completa aquí.
                    entry.server = settings_.activeServer;
                    history_.Add(std::move(entry));
                    historyChanged = true;
                }
                if (e.outcome == config::JobOutcome::Printed && settings_.notifications == config::NotifyMode::All)
                    Notify(PrinterDisplay(e.job.printer), util::Format(Tr(S::NotifyJobDone), e.job.document.c_str()),
                           false);
                if (e.outcome == config::JobOutcome::Failed && settings_.notifications != config::NotifyMode::None)
                    Notify(PrinterDisplay(e.job.printer), util::Format(Tr(S::NotifyJobError), e.job.document.c_str()),
                           true);
                break;
        }
    }
    if (historyChanged) {
        // SetTimer con el mismo id reinicia la cuenta: se guarda 5 s después
        // del ÚLTIMO cambio (WM_TIMER -> history_.Save).
        SetTimer(hwnd_, kTimerSaveHistory, 5000, nullptr);  // guarda agrupando cambios
        if (historyView_) RebuildRows();
    }
}

// Pide al CarnetService (hilo de fondo) los carnets que faltan en el
// historial. La respuesta llega por WM_APP_CARNETS -> OnCarnets.
void MainWindow::RequestHistoryCarnets() {
    if (!carnets_) return;
    std::vector<std::wstring> users;
    for (const auto& e : history_.Entries())
        if (e.carnet.empty()) users.push_back(e.user);
    carnets_->Request(users);
}

void MainWindow::OnCarnets() {
    // Los trabajos que salieron de la cola antes de conocer su carnet (y los
    // registros de versiones anteriores) lo reciben ahora, también para el Excel.
    if (history_.FillCarnets([this](const std::wstring& user) { return carnets_->Find(user); }))
        SetTimer(hwnd_, kTimerSaveHistory, 5000, nullptr);  // guarda agrupando cambios
    // La columna Carnet de la cola también cambia (y el orden si se ordena por ella).
    RebuildRows();
}

// Notifica los problemas NUEVOS de cada impresora (atasco, sin papel...)
// comparando con los de la instantánea anterior (lastProblems_). La primera
// vez que se ve una impresora solo se recuerda su estado: no se avisa de lo
// que ya estaba mal al conectar.
void MainWindow::CheckPrinterProblems() {
    if (!snapshot_) return;
    for (const auto& p : snapshot_->printers) {
        auto it = lastProblems_.find(p.name);
        // La pausa la provoca un operador: no se notifica.
        std::uint32_t relevant = p.problems & ~static_cast<std::uint32_t>(core::ProblemPaused);
        if (it != lastProblems_.end()) {
            // Bits que no estaban antes. Tóner bajo se avisa como
            // información; el resto como error.
            std::uint32_t added = relevant & ~it->second;
            if (added && settings_.notifications != config::NotifyMode::None)
                Notify(PrinterDisplay(p.name), ProblemText(core::MostSevereProblem(added)),
                       added != core::ProblemTonerLow);
        }
        lastProblems_[p.name] = relevant;
    }
}

// Trabajo de la instantánea actual con esa clave (JobInfo::Key), o nullptr.
// El puntero vale hasta la próxima instantánea.
const core::JobInfo* MainWindow::FindJob(const std::wstring& key) const {
    if (!snapshot_ || key.empty()) return nullptr;
    for (const auto& job : snapshot_->jobs)
        if (job.Key() == key) return &job;
    return nullptr;
}

const core::JobInfo* MainWindow::FocusedJob() const {
    // Sin selección no hay trabajo "actual" (se puede dejar la lista sin selección).
    if (historyView_ || !list_.HasSelection()) return nullptr;
    return FindJob(list_.FocusedKey());
}

// Copias de los trabajos seleccionados (vacío en el historial). Se devuelven
// copias, no punteros, porque suelen viajar al hilo de acciones.
// Las claves seleccionadas que ya no están en la cola se ignoran.
std::vector<core::JobInfo> MainWindow::SelectedJobs() const {
    std::vector<core::JobInfo> jobs;
    if (historyView_) return jobs;
    for (const auto& key : list_.SelectedKeys())
        if (const auto* job = FindJob(key)) jobs.push_back(*job);
    return jobs;
}

// Rearma todas las filas de la lista (cola o historial) con el filtro, la
// impresora y el orden actuales. La lista conserva selección y desplazamiento
// por clave de fila. Hilo de la UI; se llama con cada instantánea y con cada
// cambio de vista, filtro, orden, idioma, datos analizados o carnets.
void MainWindow::RebuildRows() {
    std::vector<ui::ListRow> rows;
    if (historyView_) {
        // Historial: columnas propias, sin indicador de orden; filtrado por
        // impresora elegida y texto de búsqueda. La clave de cada fila lleva
        // un ordinal estable (HistoryOrdinals, M1), no el índice.
        list_.SetColumns(HistoryColumns());
        list_.SetSortIndicator(-1, false);
        const auto& entries = history_.Entries();
        const auto ordinals = HistoryOrdinals(entries);
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            if (settings_.activePrinter != kAllPrinters && e.printer != settings_.activePrinter) continue;
            if (!HistoryMatchesFilter(e, filter_)) continue;
            rows.push_back(MakeHistoryRow(e, ordinals[i]));
        }
        list_.SetEmptyText(filter_.empty() ? Tr(S::EmptyHistory) : Tr(S::EmptyFilter), L"", ui::glyph::History);
    } else {
        // Cola: la columna Impresora solo en "Todas".
        const bool showPrinter = settings_.activePrinter == kAllPrinters;
        list_.SetColumns(JobColumns(showPrinter));
        list_.SetSortIndicator(static_cast<int>(CurrentSort(settings_)), settings_.sortDescending);
        if (snapshot_) {
            // Los datos analizados se retienen mientras se arma la lista.
            // facts_->Find devuelve un shared_ptr; FactsLookup da un puntero
            // crudo, así que se guarda el shared_ptr en `held` para que el
            // hilo de JobFactsService no lo libere mientras se usa.
            std::map<std::wstring, std::shared_ptr<const core::ContentFacts>> held;
            FactsLookup lookup = [&](const core::JobInfo& job) -> const core::ContentFacts* {
                auto facts = facts_->Find(job);
                if (!facts) return nullptr;
                auto& slot = held[job.Key()];
                slot = std::move(facts);
                return slot.get();
            };
            // Carnet desde la caché del CarnetService ("" si aún no se sabe).
            CarnetLookup carnet = [this](const std::wstring& user) { return carnets_->Find(user); };
            for (const auto* job : VisibleJobs(*snapshot_, settings_.activePrinter, filter_, CurrentSort(settings_),
                                               settings_.sortDescending, lookup, carnet))
                rows.push_back(MakeJobRow(*job, showPrinter, lookup(*job), snapshot_->FindPrinter(job->printer),
                                          carnet(job->user)));
        }
        // Texto de la lista vacía según el motivo: conectando, error del
        // servidor (con el detalle, D21), filtro sin coincidencias o cola vacía.
        if (connecting_) list_.SetEmptyText(Tr(S::StatusConnecting), ServerDisplayName(), ui::glyph::Server);
        else if (snapshot_ && !snapshot_->error.empty())
            list_.SetEmptyText(Tr(S::ServerError), ServerDisplayName() + L"\n" + snapshot_->error, ui::glyph::Warning);
        else if (!filter_.empty()) list_.SetEmptyText(Tr(S::EmptyFilter), L"", ui::glyph::Search);
        else list_.SetEmptyText(Tr(S::EmptyQueueTitle), Tr(S::EmptyQueueHint), ui::glyph::Printer);
    }
    list_.SetRows(std::move(rows));
    UpdateSelectionTotal();
}

// Total de páginas de la selección en la barra inferior (D44).
void MainWindow::UpdateSelectionTotal() {
    const auto keys = list_.SelectedKeys();
    PagesTotal total;
    if (historyView_) {
        // Historial: las claves son "h<hora>_<ordinal>"; se recorren los
        // registros hasta encontrar todos los seleccionados.
        const std::set<std::wstring> wanted(keys.begin(), keys.end());
        const auto& all = history_.Entries();
        std::vector<const config::HistoryEntry*> entries;
        if (!wanted.empty()) {
            const auto ordinals = HistoryOrdinals(all);
            for (size_t i = 0; i < all.size() && entries.size() < wanted.size(); ++i)
                if (wanted.count(HistoryKey(all[i], ordinals[i]))) entries.push_back(&all[i]);
        }
        total = SumHistoryPages(entries);
    } else {
        std::vector<const core::JobInfo*> jobs;
        for (const auto& key : keys)
            if (const auto* job = FindJob(key)) jobs.push_back(job);
        // Los datos analizados se retienen mientras se suman.
        // (Mismo motivo que en RebuildRows; facts_ puede ser nulo si esto se
        // llama durante Create, antes de crear los servicios.)
        std::vector<std::shared_ptr<const core::ContentFacts>> held;
        FactsLookup lookup = [&](const core::JobInfo& job) -> const core::ContentFacts* {
            if (!facts_) return nullptr;
            held.push_back(facts_->Find(job));
            return held.back().get();
        };
        total = SumPrintPages(jobs, lookup);
    }
    status_.SetSelection(SelectionText(total, historyView_));
}

// Etiqueta de la cabecera: título = impresora elegida (o "Todas"), subtítulo
// = servidor o, si la impresora tiene un problema, ese problema; `warning`
// la pinta en color de aviso. También refleja si se ve el historial.
void MainWindow::UpdateHeader() {
    std::wstring title = Tr(S::AllPrinters);
    std::wstring subtitle = ServerDisplayName();
    bool warning = false;
    if (snapshot_ && settings_.activePrinter != kAllPrinters) {
        if (const auto* p = snapshot_->FindPrinter(settings_.activePrinter)) {
            title = p->DisplayName();
            auto problem = core::MostSevereProblem(p->problems);
            if (problem != core::ProblemNone) {
                subtitle = ProblemText(problem);
                warning = true;
            }
        }
    } else if (snapshot_) {
        // En "Todas": aviso si alguna impresora tiene un problema que no sea
        // solo tóner bajo (la pausa sí cuenta: con D18 suele ser lo normal,
        // pero el operador debe ver que hay colas detenidas).
        warning = std::any_of(snapshot_->printers.begin(), snapshot_->printers.end(), [](const core::PrinterInfo& p) {
            return (p.problems & ~static_cast<std::uint32_t>(core::ProblemTonerLow)) != 0;
        });
    }
    header_.SetPrinterLabel(title, subtitle, warning);
    header_.SetHistoryActive(historyView_);
    UpdatePauseButtons();
}

// Estado de los dos botones de pausa de la cabecera (D37): habilitado, si la
// acción será Pausar o Reanudar y la descripción con el atajo. Se llama al
// cambiar la selección, la instantánea o los atajos.
void MainWindow::UpdatePauseButtons() {
    // El atajo actual (puede estar reasignado) se muestra en la descripción.
    auto withKey = [this](std::wstring text, Command command) {
        auto keys = shortcuts_.Get(command);
        if (!keys.empty()) text += L" (" + keys.front().ToString() + L")";
        return text;
    };
    // Botón de trabajos: mismo criterio que Ctrl+P (ShouldPauseJobs).
    ui::PauseButtonState jobs;
    const auto selected = SelectedJobs();
    jobs.enabled = !selected.empty();
    jobs.resume = jobs.enabled && !ShouldPauseJobs(selected);
    jobs.tooltip = !jobs.enabled ? std::wstring(Tr(S::TipNoJobsSelected))
                                 : withKey(Tr(jobs.resume ? S::TipResumeJobs : S::TipPauseJobs), Command::Pause);

    // Mismo criterio que el atajo (TogglePrinterPause): si alguna está en pausa, reanuda.
    ui::PauseButtonState printer;
    const auto targets = TargetPrinters();
    printer.enabled = !historyView_ && !targets.empty();
    // (targets solo tiene elementos si hay snapshot_, así que no es nulo aquí.)
    for (const auto& name : targets)
        if (const auto* p = snapshot_->FindPrinter(name); p && (p->problems & core::ProblemPaused)) printer.resume = true;
    if (!printer.enabled) {
        printer.tooltip = Tr(S::NoPrinters);
    } else if (settings_.activePrinter == kAllPrinters) {
        printer.tooltip = withKey(Tr(printer.resume ? S::TipResumePrinterAll : S::TipPausePrinterAll), Command::PausePrinter);
    } else {
        printer.tooltip = withKey(util::Format(Tr(printer.resume ? S::TipResumePrinterOne : S::TipPausePrinterOne),
                                               PrinterDisplay(settings_.activePrinter).c_str()),
                                  Command::PausePrinter);
    }
    header_.SetPauseButtons(jobs, printer);
    // El botón "Exportar a Excel" (solo visible en el historial) también
    // muestra su atajo.
    header_.SetExportTooltip(withKey(Tr(S::TipExport), Command::ExportHistory));
}

// Menú del clic derecho en los títulos de la tabla: restablecer anchos (D36)
// y orden (D38). Cada opción aparece deshabilitada si ya está en el valor
// predeterminado. Se atiende aquí mismo (no pasa por HandleMenuCommand).
void MainWindow::ShowColumnHeaderMenu(POINT screen) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (settings_.columnWidths.empty() ? MF_GRAYED : 0), ID_RESET_COLUMNS,
                Tr(S::MenuResetColumns));
    AppendMenuW(menu, MF_STRING | (settings_.columnOrder.empty() ? MF_GRAYED : 0), ID_RESET_COLUMN_ORDER,
                Tr(S::MenuResetColumnOrder));
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (id == ID_RESET_COLUMNS) ResetColumnWidths();
    if (id == ID_RESET_COLUMN_ORDER) ResetColumnOrder();
}

// Vuelve todas las columnas (cola e historial) a su ancho predeterminado.
void MainWindow::ResetColumnWidths() {
    settings_.columnWidths.clear();
    list_.SetColumnWidths({});
    SaveSettings();
}

// Vuelve al orden de columnas predeterminado (columnOrder vacío en config.json).
void MainWindow::ResetColumnOrder() {
    settings_.columnOrder.clear();
    list_.SetColumnOrder(EffectiveColumnOrder({}));
    SaveSettings();
}

// Barra de estado inferior: a la izquierda el resumen (texto) y a la derecha
// los indicadores (ícono + texto + color), de más a menos importante. La
// barra recorta los que no caben; el total de la selección (D44) tiene
// prioridad y se actualiza aparte (UpdateSelectionTotal).
void MainWindow::UpdateStatus() {
    std::wstring summary;
    if (historyView_) {
        // Historial: páginas y trabajos de hoy y páginas de la semana.
        auto stats = history_.ComputeStats(util::ToUnixSeconds(util::NowUtc()));
        summary = util::Format(Tr(S::StatusHistory), static_cast<unsigned long long>(stats.pagesToday),
                               static_cast<unsigned long long>(stats.jobsToday),
                               static_cast<unsigned long long>(stats.pagesWeek));
    } else if (connecting_) {
        summary = Tr(S::StatusConnecting);
    } else if (snapshot_) {
        // Cola: conteos de los visibles; con filtro, "N de M coinciden con ...".
        auto visible = VisibleJobs(*snapshot_, settings_.activePrinter, filter_);
        summary = SummaryText(Summarize(visible));
        if (!filter_.empty()) {
            auto all = VisibleJobs(*snapshot_, settings_.activePrinter, L"");
            summary = util::Format(Tr(S::StatusFiltered), filter_.c_str(), static_cast<unsigned>(visible.size()),
                                   static_cast<unsigned>(all.size()));
        }
        // Trabajos que existen pero no se ven por tener una impresora elegida:
        // así no parece que la app "perdió" lo que sí muestra la cola de Windows.
        if (settings_.activePrinter != kAllPrinters) {
            auto everywhere = VisibleJobs(*snapshot_, kAllPrinters, filter_);
            if (everywhere.size() > visible.size())
                summary += L"  ·  " + util::Format(Tr(S::StatusOtherPrinters),
                                                   static_cast<unsigned>(everywhere.size() - visible.size()));
        }
    }
    summary += L"  ·  " + util::Format(Tr(S::StatusServer), ServerDisplayName().c_str());
    status_.SetSummary(summary);

    std::vector<ui::StatusItem> items;
    // 1. Mensaje temporal (resultado de la última acción, ShowTransient), con
    //    el ícono que corresponde a su tono.
    if (!transientText_.empty()) {
        wchar_t glyph = transientTone_ == ui::Tone::Danger    ? ui::glyph::Error
                        : transientTone_ == ui::Tone::Warning ? ui::glyph::Warning
                        : transientTone_ == ui::Tone::Success ? ui::glyph::Completed
                                                              : ui::glyph::Refresh;
        items.push_back({glyph, transientText_, transientTone_});
    }
    if (snapshot_) {
        // 2. Problema de la impresora elegida (o el más grave de todas).
        //    En la vista "Todas" se antepone el nombre de la impresora.
        core::PrinterProblem worst = core::ProblemNone;
        std::wstring printerName;
        for (const auto& p : snapshot_->printers) {
            if (settings_.activePrinter != kAllPrinters && p.name != settings_.activePrinter) continue;
            auto problem = core::MostSevereProblem(p.problems);
            if (problem != core::ProblemNone && (worst == core::ProblemNone || problem < worst)) {
                worst = problem;
                printerName = p.DisplayName();
            }
        }
        if (worst != core::ProblemNone) {
            ui::Tone tone = (worst == core::ProblemTonerLow || worst == core::ProblemPaused) ? ui::Tone::Warning
                                                                                            : ui::Tone::Danger;
            std::wstring text = ProblemText(worst);
            if (settings_.activePrinter == kAllPrinters) text = printerName + L": " + text;
            items.push_back({ui::glyph::Warning, text, tone});
        }
        // 3. Sin permisos de administración en alguna impresora visible (D15).
        bool noAdmin = false;
        for (const auto& [printer, admin] : adminRights_)
            if (!admin && (settings_.activePrinter == kAllPrinters || printer == settings_.activePrinter)) noAdmin = true;
        if (noAdmin) items.push_back({ui::glyph::Shield, Tr(S::StatusNoAdmin), ui::Tone::Warning});
        // 4. La retención automática no pudo pausar trabajos (D17), con el error.
        if (snapshot_->holdFailures > 0)
            items.push_back({ui::glyph::Warning,
                             util::Format(Tr(S::StatusAutoHoldFailed), snapshot_->holdFailures,
                                          util::Win32ErrorMessage(snapshot_->holdError).c_str()),
                             ui::Tone::Danger});
        // 5. cJobs del servidor mayor que los trabajos enumerados (D21).
        if (snapshot_->missingJobs > 0)
            items.push_back({ui::glyph::Warning, util::Format(Tr(S::StatusMissingJobs), snapshot_->missingJobs),
                             ui::Tone::Danger});
        // 6. Impresoras que no responden (D52).
        if (snapshot_->unreachablePrinters > 0)
            items.push_back({ui::glyph::Warning,
                             util::Format(Tr(S::StatusUnreachablePrinters), snapshot_->unreachablePrinters),
                             ui::Tone::Warning});
        // 7. Orden distinto al de la cola (D20): recuerda que lo que se ve
        //    no es el orden real de impresión.
        if (!historyView_ && CurrentSort(settings_) != SortBy::Queue) {
            items.push_back({ui::glyph::Document,
                             util::Format(Tr(settings_.sortDescending ? S::StatusSortedByDesc : S::StatusSortedBy),
                                          SortName(CurrentSort(settings_)).c_str()),
                             ui::Tone::Info});
        }
        // 8. F11 en curso (D35): cuántos trabajos liberados faltan.
        if (release_.Active())
            items.push_back({ui::glyph::Pause,
                             util::Format(Tr(S::StatusPrintAndRepause), static_cast<unsigned>(release_.PendingJobs())),
                             ui::Tone::Info});
        // 9. Retención automática activa y 10. "En vivo" (notificaciones del
        //    spooler) o "Actualización por sondeo" (D5).
        if (settings_.autoHoldNewJobs) items.push_back({ui::glyph::Pause, Tr(S::StatusAutoHold), ui::Tone::Info});
        items.push_back({snapshot_->notificationsActive ? ui::glyph::Live : ui::glyph::Refresh,
                         snapshot_->notificationsActive ? Tr(S::StatusLive) : Tr(S::StatusPolling), ui::Tone::Neutral});
    }
    status_.SetItems(std::move(items));
}

// Título de la ventana (barra de tareas): "Cola de impresión — \\SERVIDOR (admin)".
void MainWindow::UpdateTitle() {
    std::wstring title = Tr(S::AppTitle);
    if (!settings_.activeServer.empty()) title += L" — " + settings_.activeServer;
    if (IsElevated()) title += L" (admin)";
    SetWindowTextW(hwnd_, title.c_str());
}

// ---------------------------------------------------------------------------
// Vista previa
// ---------------------------------------------------------------------------

// Pedido de vista previa para un trabajo con las opciones actuales (carpeta
// de spool alternativa del servidor, renderizadores, DPI y máximo de páginas).
// La clave del pedido (request.Key()) identifica el resultado en la caché.
preview::PreviewRequest MainWindow::MakePreviewRequest(const core::JobInfo& job) const {
    preview::PreviewRequest request;
    request.server = settings_.activeServer;
    request.job = job;
    auto it = settings_.spoolDirOverrides.find(settings_.activeServer);
    if (it != settings_.spoolDirOverrides.end()) request.options.spoolOverrideDir = it->second;
    request.options.renderers = renderers_;
    request.options.rasterDpi = settings_.previewDpi;
    request.options.maxPages = settings_.previewMaxPages;
    return request;
}

// Líneas del encabezado del panel de detalles: usuario · equipo; páginas ·
// tamaño · enviado; resumen de configuración; estado · impresora; posición y
// prioridad; y el origen de la vista previa si lo hay. doc puede ser nullptr.
std::vector<std::wstring> MainWindow::JobDetails(const core::JobInfo& job, const preview::PreviewDocument* doc) const {
    std::vector<std::wstring> lines;
    std::wstring machine = job.machine;
    while (!machine.empty() && machine.front() == L'\\') machine.erase(machine.begin());
    std::wstring who = job.user;
    if (!machine.empty()) who += (who.empty() ? L"" : L" · ") + machine;
    lines.push_back(who);

    // Las páginas contadas en los datos mandan sobre TotalPages (en las Epson
    // cuenta el dorso en blanco de la última hoja), igual que en la ficha.
    const core::PageAccounting acc = core::ComputePageAccounting(job, doc ? &doc->facts : nullptr);
    std::uint32_t pages = job.totalPages;
    if (acc.dataPages.basis == core::Basis::ContentPages) pages = acc.dataPages.value;
    else if (doc) pages = std::max(pages, doc->totalPages);
    // Copia con las páginas corregidas para reutilizar PagesText.
    core::JobInfo copy = job;
    copy.totalPages = pages;
    lines.push_back(PagesText(copy) + L" · " + util::FormatBytes(job.sizeBytes) + L" · " +
                    util::Format(Tr(S::DetailSubmitted), util::FormatTimeShort(job.submitted).c_str()));
    // Papel, orientación, doble cara, copias y páginas a imprimir (fiabilidad en F7).
    lines.push_back(SettingsSummary(job, doc));

    std::wstring state = JobStatusText(job);
    if (!job.statusText.empty()) state += L" — " + job.statusText;
    lines.push_back(state + L" · " + PrinterDisplay(job.printer));
    lines.push_back(util::Format(Tr(S::DetailPosition), job.position, job.priority));
    if (doc && !doc->source.empty()) lines.push_back(doc->source);
    return lines;
}

// Actualiza el panel de detalles según el elemento con el foco. Hilo de la UI.
// Si la vista previa ya está en la caché de PreviewService se muestra; si no,
// se pide (se genera en su hilo) y al terminar llega WM_APP_PREVIEW, que
// vuelve a llamar aquí.
void MainWindow::UpdatePreview() {
    if (historyView_) {
        // Historial: no hay datos que previsualizar (el trabajo ya no está en
        // el spooler); se muestra un resumen en texto del registro.
        currentPreviewKey_.clear();
        std::wstring key = list_.FocusedKey();
        const auto& entries = history_.Entries();
        const auto ordinals = key.empty() ? std::vector<size_t>() : HistoryOrdinals(entries);
        for (size_t i = 0; i < ordinals.size(); ++i) {
            if (HistoryKey(entries[i], ordinals[i]) != key) continue;
            const auto& e = entries[i];
            std::wstring text = e.document + L"\n\n" + e.user + L" · " + e.machine + L"\n" + PrinterDisplay(e.printer) +
                                L"\n" + util::FormatTimeIso(util::FromUnixSeconds(e.timeUnix)) + L"\n" +
                                OutcomeText(e.outcome);
            preview_.ShowEmpty(text);
            return;
        }
        preview_.ShowEmpty(Tr(S::PreviewNoSelection));
        return;
    }

    const core::JobInfo* job = FocusedJob();
    if (!job) {
        currentPreviewKey_.clear();
        preview_.ShowEmpty(Tr(S::PreviewNoSelection));
        return;
    }
    if (!preview_.IsVisible()) return;  // panel oculto: no generamos nada
    // (Leer los datos de un trabajo tiene costo y riesgos, D32/D39: solo se
    // hace si el operador está mirando el panel.)

    auto request = MakePreviewRequest(*job);
    std::wstring key = request.Key();
    // Find devuelve el documento ya generado (shared_ptr) o nullptr.
    auto doc = previews_->Find(key);
    const core::PrinterInfo* printer = snapshot_ ? snapshot_->FindPrinter(job->printer) : nullptr;
    if (doc) {
        currentPreviewKey_ = key;
        // La vista previa lee todos los datos: su análisis es el más completo para la lista.
        if (doc->facts.analyzed) facts_->Store(*job, doc->facts);
        preview_.ShowDocument(job->document, JobDetails(*job, doc.get()), doc);
        preview_.SetSheet(BuildJobSheet(*job, printer, doc.get()));
        return;
    }
    // Mientras se genera, si había una versión anterior del mismo trabajo se sigue mostrando.
    // (La clave empieza con "servidor|impresora|id|" y sigue con datos que
    // cambian: tamaño, TotalPages, si se recibe y si se puede leer. Así se
    // evita parpadear a "Cargando..." cuando cambia uno de ellos.)
    bool sameJob = currentPreviewKey_.rfind(settings_.activeServer + L"|" + job->printer + L"|" + std::to_wstring(job->id) + L"|", 0) == 0;
    currentPreviewKey_ = key;
    if (sameJob && preview_.mode() == ui::PreviewPane::Mode::Document) {
        preview_.SetInfo(job->document, JobDetails(*job, preview_.document().get()));
        preview_.SetSheet(BuildJobSheet(*job, printer, preview_.document().get()));
    } else {
        preview_.ShowLoading(job->document, JobDetails(*job, nullptr));
        preview_.SetSheet(BuildJobSheet(*job, printer, nullptr));
    }
    // Pide la generación en segundo plano. urgent = true descarta los pedidos
    // pendientes (de trabajos que el operador ya dejó de mirar).
    previews_->Request(request, true);
}

// ---------------------------------------------------------------------------
// Comandos
// ---------------------------------------------------------------------------

// Ejecuta un comando de input/Commands (por atajo, botón, panel de ayuda o
// menú). Es el único punto de entrada de los comandos con atajo
// (GUIA_IA §5: cada comando nuevo agrega aquí su case). Hilo de la UI.
void MainWindow::Execute(Command command) {
    switch (command) {
        // Pausa/reanuda la selección (Ctrl+P), el combinado con la impresora
        // (Ctrl+Alt+P / F8) y "imprimir y volver a pausar" (Ctrl+Enter / F11).
        case Command::Pause: TogglePauseSelection(false); break;
        case Command::TogglePauseBoth: TogglePauseSelection(true); break;
        case Command::PrintAndRepause: PrintSelectionAndRepause(); break;
        case Command::ClearSelection: list_.ClearSelection(); break;
        case Command::Resume: RunOnSelection(JobAction::Resume); break;
        case Command::Cancel: CancelSelection(SelectedJobs()); break;
        case Command::Restart: RunOnSelection(JobAction::Restart); break;
        // Navegación por la lista; INT_MIN/INT_MAX = primera/última fila.
        case Command::NavUp: list_.MoveFocus(-1, false); break;
        case Command::NavDown: list_.MoveFocus(1, false); break;
        case Command::NavFirst: list_.MoveFocus(INT_MIN, false); break;
        case Command::NavLast: list_.MoveFocus(INT_MAX, false); break;
        // Cambia la posición real en la cola del spooler (Ctrl+↑/↓).
        case Command::MoveUp: MoveFocused(-1); break;
        case Command::MoveDown: MoveFocused(1); break;
        case Command::QuickLook: ToggleQuickLook(); break;
        case Command::ToggleTheme: ToggleTheme(); break;
        case Command::Find:
            // Si el cuadro estaba en modo "servidor", vuelve a ser búsqueda.
            // Y deja de ser la edición del servidor del LAIMI (F2).
            editingLaimi_ = false;
            if (header_.InServerMode()) header_.EndServerInput();
            header_.FocusSearch();
            break;
        case Command::Help: help_.Open(); break;
        case Command::Refresh:
            // Pide al monitor una instantánea inmediata (sin esperar el sondeo).
            monitor_->RequestRefresh();
            ShowTransient(Tr(S::CmdRefresh), ui::Tone::Neutral);
            break;
        case Command::Redirect: {
            // Por teclado no hay posición del ratón: el menú de destinos se
            // abre cerca de la esquina superior izquierda de la lista.
            RECT r{};
            GetWindowRect(list_.hwnd(), &r);
            StartRedirect({r.left + Style::Get().Px(60), r.top + Style::Get().Px(60)});
            break;
        }
        case Command::NextPrinter: CyclePrinter(1); break;
        case Command::PrevPrinter: CyclePrinter(-1); break;
        case Command::SortCycle: CycleSort(1); break;
        case Command::SortReverse: SetSort(settings_.sortBy, !settings_.sortDescending); break;
        case Command::PinPrinter: TogglePinnedPrinter(EffectivePrinter()); break;
        case Command::ChoosePrinter: {
            RECT r = header_.ButtonScreenRect(ui::HeaderButton::Printer);
            ShowPrinterMenu({r.left, r.bottom + Style::Get().Px(4)});
            break;
        }
        // Conectar: el cuadro de búsqueda pasa a modo servidor con el actual;
        // Enter conecta (WM_COMMAND/HeaderBar -> ConnectTo).
        // editingLaimi_ se apaga (F2): si quedó encendida por un "Cambiar
        // servidor del LAIMI…" que se abandonó con un clic fuera del cuadro,
        // este Enter guardaba el servidor escrito en servidor-laimi.txt.
        case Command::Connect:
            editingLaimi_ = false;
            header_.BeginServerInput(settings_.activeServer);
            break;
        case Command::History: ToggleHistory(); break;
        case Command::ExportHistory: ExportHistory(); break;
        // Páginas y zoom de la vista previa.
        case Command::NextPage: preview_.NextPage(); break;
        case Command::PrevPage: preview_.PrevPage(); break;
        case Command::ZoomIn: preview_.ZoomIn(); break;
        case Command::ZoomOut: preview_.ZoomOut(); break;
        case Command::ZoomReset: preview_.ZoomReset(); break;
        // Ctrl+Shift+P / F9: alterna la impresora (todas en "Todas", D18).
        case Command::PausePrinter: TogglePrinterPause(); break;
        case Command::ResumePrinter: SetPrintersPaused(false); break;
        case Command::SelectAll: list_.SelectAll(); break;
        case Command::Menu: {
            RECT r = header_.ButtonScreenRect(ui::HeaderButton::Menu);
            ShowMainMenu({r.left, r.bottom + Style::Get().Px(4)});
            break;
        }
        // Muestra/oculta el panel de detalles (Ctrl+B / F7, D33).
        case Command::TogglePane:
            settings_.showPreviewPane = !settings_.showPreviewPane;
            SaveSettings();
            Layout();
            UpdatePreview();
            break;
        case Command::ToggleSheet:
            // Alterna entre la vista previa y la configuración de impresión del trabajo.
            if (!settings_.showPreviewPane) {
                settings_.showPreviewPane = true;
                SaveSettings();
                Layout();
                preview_.SetTab(ui::PreviewPane::Tab::Sheet);
            } else {
                preview_.ToggleTab();
            }
            UpdatePreview();
            break;
        // Activa/desactiva la retención automática de trabajos nuevos (D17).
        case Command::ToggleAutoHold:
            settings_.autoHoldNewJobs = !settings_.autoHoldNewJobs;
            monitor_->SetAutoHold(settings_.autoHoldNewJobs);
            SaveSettings();
            UpdateStatus();
            break;
        default: break;
    }
}

// Ctrl+P (alsoPrinter = false) y Ctrl+Alt+P / F8 (true).
void MainWindow::TogglePauseSelection(bool alsoPrinter) {
    auto jobs = SelectedJobs();
    if (jobs.empty()) {
        // Sin trabajos seleccionados, el comando combinado igual actúa sobre la impresora.
        if (alsoPrinter) TogglePrinterPause();
        else ShowTransient(Tr(S::NoJobSelected), ui::Tone::Warning);
        return;
    }
    // Si alguno está corriendo se pausa todo; si todos están en pausa, se reanuda.
    const bool pause = ShouldPauseJobs(jobs);
    RunJobAction(pause ? JobAction::Pause : JobAction::Resume, jobs);
    // El combinado deja la(s) impresora(s) de esos trabajos en el mismo estado.
    if (alsoPrinter) SetPrintersPaused(pause, PrintersOf(jobs));
}

// "Imprimir y volver a pausar" (Ctrl+Enter / F11, D35).
void MainWindow::PrintSelectionAndRepause() {
    auto jobs = SelectedJobs();
    if (jobs.empty()) {
        ShowTransient(Tr(S::NoJobSelected), ui::Tone::Warning);
        return;
    }
    // La copia local de CSR no se libera ni se sigue (D50): sin ella no se
    // reanuda la impresora por un trabajo que no se va a tocar.
    const auto selected = jobs.size();
    std::erase_if(jobs, [](const core::JobInfo& job) { return core::IsLocalCsrCopy(job.rawStatus); });
    if (jobs.empty()) {
        ShowTransient(util::Format(Tr(S::ActionSkippedLocalCopy), static_cast<unsigned>(selected)), ui::Tone::Warning);
        return;
    }
    // El tracker empieza a seguir los trabajos (con la hora, para el plazo de
    // 15 s de una reanudación fallida) y devuelve cuántos acepta.
    const auto count = static_cast<unsigned>(release_.Start(jobs, GetTickCount64()));
    if (count == 0) {
        ShowTransient(Tr(S::PrintAndRepauseNothing), ui::Tone::Warning);
        return;
    }
    // El mismo hilo de acciones reanuda primero los trabajos y después las
    // impresoras: así no se cuela otro trabajo de la cola entre ambos pasos.
    RunJobAction(JobAction::Resume, jobs);
    // Solo se reanudan las impresoras que están en pausa.
    std::vector<std::wstring> paused;
    for (const auto& name : PrintersOf(jobs))
        if (const auto* p = snapshot_->FindPrinter(name); p && (p->problems & core::ProblemPaused)) paused.push_back(name);
    if (!paused.empty()) actions_->RunOnPrinters(JobAction::ResumePrinter, std::move(paused));
    util::LogInfo(L"Imprimir y volver a pausar: %u trabajo(s) liberado(s)", count);
    ShowTransient(util::Format(Tr(S::PrintAndRepauseStarted), count), ui::Tone::Success);
}

// Con cada instantánea (desde OnSnapshot): pregunta al PrintReleaseTracker
// qué impresoras ya terminaron sus trabajos liberados y las vuelve a pausar
// SIEMPRE (aunque antes no lo estuvieran, D35).
void MainWindow::RepauseReleasedPrinters() {
    if (!release_.Active()) return;
    auto done = release_.Update(*snapshot_, GetTickCount64());
    if (done.empty()) return;
    std::wstring names;
    for (const auto& name : done) {
        if (!names.empty()) names += L", ";
        names += PrinterDisplay(name);
        util::LogInfo(L"Imprimir y volver a pausar: %ls terminó, se pausa la impresora", name.c_str());
    }
    actions_->RunOnPrinters(JobAction::PausePrinter, std::move(done));
    ShowTransient(util::Format(Tr(S::PrintAndRepauseDone), names.c_str()), ui::Tone::Success);
}

// Aplica una acción a los trabajos seleccionados (aviso si no hay ninguno).
void MainWindow::RunOnSelection(JobAction action) {
    auto jobs = SelectedJobs();
    if (jobs.empty()) {
        ShowTransient(Tr(S::NoJobSelected), ui::Tone::Warning);
        return;
    }
    RunJobAction(action, std::move(jobs));
}

// Toda acción que libere trabajos debe pasar por aquí (GUIA_IA §6, D47):
// reanudar/reiniciar los marca como liberados por el operador, así si salen
// de la cola entre dos refrescos el historial los registra como Impreso; volver
// a pausarlos borra esa marca. Luego se encola en el hilo de acciones.
void MainWindow::RunJobAction(JobAction action, std::vector<core::JobInfo> jobs) {
    for (const auto& job : jobs) {
        if (action == JobAction::Resume || action == JobAction::Restart) tracker_.MarkReleasedByUser(job.Key());
        else if (action == JobAction::Pause) tracker_.ForgetReleasedByUser(job.Key());
    }
    actions_->Run(action, std::move(jobs));
}

// Cancela trabajos, con confirmación si está activada (botón por defecto: No).
// Se marcan como cancelados por el operador para que el historial los
// registre como Cancelado.
void MainWindow::CancelSelection(const std::vector<core::JobInfo>& jobs) {
    if (jobs.empty()) {
        ShowTransient(Tr(S::NoJobSelected), ui::Tone::Warning);
        return;
    }
    if (settings_.confirmCancel) {
        std::wstring question = jobs.size() == 1
                                    ? util::Format(Tr(S::ConfirmCancelOne), jobs.front().document.c_str())
                                    : util::Format(Tr(S::ConfirmCancelMany), static_cast<unsigned>(jobs.size()));
        if (MessageBoxW(hwnd_, question.c_str(), Tr(S::AppTitle), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return;
    }
    for (const auto& job : jobs) tracker_.MarkCancelledByUser(job.Key());
    actions_->Run(JobAction::Cancel, jobs);
}

// Sube (direction < 0) o baja el trabajo con el foco una posición en la cola.
void MainWindow::MoveFocused(int direction) {
    const core::JobInfo* job = FocusedJob();
    if (!job) return;
    actions_->Run(direction < 0 ? JobAction::MoveUp : JobAction::MoveDown, {*job});
}

// Acción sobre UNA impresora (la elegida o la del trabajo con el foco); se usa
// para vaciar la cola. Vaciar pide confirmación y marca todos sus trabajos
// como cancelados por el operador (historial).
void MainWindow::RunPrinterAction(JobAction action) {
    std::wstring printer = EffectivePrinter();
    if (printer.empty()) {
        ShowTransient(Tr(S::NoJobSelected), ui::Tone::Warning);
        return;
    }
    if (action == JobAction::PurgePrinter) {
        std::wstring question = util::Format(Tr(S::ConfirmPurge), PrinterDisplay(printer).c_str());
        if (MessageBoxW(hwnd_, question.c_str(), Tr(S::AppTitle), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
        if (snapshot_)
            for (const auto& job : snapshot_->jobs)
                if (job.printer == printer) tracker_.MarkCancelledByUser(job.Key());
    }
    actions_->RunOnPrinters(action, {printer});
}

// Vacío si todavía no hay instantánea.
std::vector<std::wstring> MainWindow::TargetPrinters() const {
    std::vector<std::wstring> printers;
    if (!snapshot_) return printers;
    for (const auto& p : snapshot_->printers)
        if (settings_.activePrinter == kAllPrinters || p.name == settings_.activePrinter) printers.push_back(p.name);
    return printers;
}

void MainWindow::TogglePrinterPause() {
    // Si hay alguna impresora en pausa se reanudan todas (es lo que más se
    // necesita tras la pausa de inicio); si ninguna lo está, se pausan.
    // Así el atajo sigue sirviendo aunque alguna impresora no se pueda controlar.
    bool anyPaused = false;
    for (const auto& name : TargetPrinters())
        if (const auto* p = snapshot_->FindPrinter(name); p && (p->problems & core::ProblemPaused)) anyPaused = true;
    SetPrintersPaused(!anyPaused);
}

// Pausa (pause = true) o reanuda impresoras. Solo se envían al hilo de
// acciones las que no están ya en el estado pedido.
void MainWindow::SetPrintersPaused(bool pause, std::vector<std::wstring> printers) {
    if (printers.empty()) printers = TargetPrinters();
    // Si printers no quedó vacío, hay instantánea (TargetPrinters o
    // PrintersOf de trabajos de ella), así que snapshot_ no es nulo.
    std::vector<std::wstring> targets;
    for (const auto& name : printers) {
        const auto* p = snapshot_->FindPrinter(name);
        const bool paused = p && (p->problems & core::ProblemPaused);
        if (paused != pause) targets.push_back(name);
    }
    if (printers.empty()) {
        ShowTransient(Tr(S::NoPrinters), ui::Tone::Warning);
        return;
    }
    if (targets.empty()) {  // ya estaban como se pidió
        ShowTransient(util::Format(Tr(pause ? S::PrintersPaused : S::PrintersResumed), 0u), ui::Tone::Neutral);
        return;
    }
    actions_->RunOnPrinters(pause ? JobAction::PausePrinter : JobAction::ResumePrinter, std::move(targets));
}

// Reenviar el trabajo con el foco a otra impresora (D12): muestra el menú de
// destinos (todas menos la suya, con su problema si lo tienen) y, al elegir,
// arma el RedirectRequest y lo envía al hilo de acciones. El resultado llega
// por WM_APP_ACTION -> OnActionResult.
void MainWindow::StartRedirect(POINT screen) {
    const core::JobInfo* job = FocusedJob();
    if (!job || !snapshot_) {
        ShowTransient(Tr(S::NoJobSelected), ui::Tone::Warning);
        return;
    }
    // Copia: el menú es modal y durante él puede llegar una instantánea
    // nueva que invalide el puntero `job`.
    redirectJob_ = *job;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, Tr(S::RedirectChoose));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    menuPrinters_.clear();
    for (const auto& p : snapshot_->printers) {
        if (p.name == job->printer) continue;
        std::wstring label = p.DisplayName();
        auto problem = core::MostSevereProblem(p.problems);
        if (problem != core::ProblemNone) label += L"  —  " + ProblemText(problem);
        AppendMenuW(menu, MF_STRING, ID_REDIRECT_BASE + menuPrinters_.size(), label.c_str());
        menuPrinters_.push_back(p.name);
        if (menuPrinters_.size() >= 999) break;
    }
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    // 0 (cancelado) o un id fuera del rango: nada que hacer.
    if (id < ID_REDIRECT_BASE || id >= ID_REDIRECT_BASE + menuPrinters_.size()) return;

    const std::wstring target = menuPrinters_[id - ID_REDIRECT_BASE];
    RedirectRequest request;
    request.server = settings_.activeServer;
    request.job = redirectJob_;
    request.targetPrinter = target;
    if (const auto* p = snapshot_->FindPrinter(target)) request.targetDriver = p->driver;
    auto it = settings_.spoolDirOverrides.find(settings_.activeServer);
    if (it != settings_.spoolDirOverrides.end()) request.spoolOverrideDir = it->second;
    request.renderers = renderers_;
    // El original se cancelará al terminar bien: en el historial cuenta como
    // cancelado por el operador (el reenviado aparece como trabajo nuevo).
    tracker_.MarkCancelledByUser(redirectJob_.Key());
    actions_->Redirect(std::move(request));
}

// Resultado de una acción (WM_APP_ACTION, hilo de la UI).
void MainWindow::OnActionResult(const ActionResult& result) {
    // Permisos consultados: se guardan y se actualiza el indicador "sin permisos".
    if (result.action == JobAction::CheckAdmin) {
        for (const auto& [printer, admin] : result.adminRights) adminRights_[printer] = admin;
        UpdateStatus();
        return;
    }
    // Reenvío sin forma de convertir: se pregunta si enviar los datos RAW a
    // un driver distinto (botón por defecto: No) y, si acepta, se repite el
    // mismo pedido con allowRaw.
    if (result.action == JobAction::Redirect && result.needsRawConfirmation) {
        std::wstring question = util::Format(Tr(S::ConfirmRedirectRaw), result.dataFormat.c_str(),
                                             result.redirect.job.driver.c_str(),
                                             PrinterDisplay(result.redirect.targetPrinter).c_str());
        if (MessageBoxW(hwnd_, question.c_str(), Tr(S::AppTitle), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
            RedirectRequest again = result.redirect;
            again.allowRaw = true;
            actions_->Redirect(std::move(again));
        } else {
            // El operador no quiso enviar RAW: el original sigue en su cola y
            // no se canceló, así que se quita la marca (F3).
            tracker_.ForgetCancelledByUser(result.redirect.job.Key());
        }
        return;
    }
    // F3: los trabajos se marcan "cancelado por el operador" antes de ejecutar
    // la acción (la marca tiene que existir cuando el trabajo desaparezca de
    // la cola). Si la acción no llegó a quitarlos, la marca se retira: si no,
    // un trabajo que después se imprime quedaba "Cancelado" en el historial.
    if (result.action == JobAction::Cancel) {
        for (const auto& key : result.failedKeys) tracker_.ForgetCancelledByUser(key);
    } else if (result.action == JobAction::Redirect && (result.succeeded == 0 || !result.failedKeys.empty())) {
        // Reenvío fallido u omitido, o reenviado pero sin poder cancelar el
        // original (queda en pausa): el original no se canceló.
        tracker_.ForgetCancelledByUser(result.redirect.job.Key());
    } else if (result.action == JobAction::PurgePrinter && snapshot_) {
        // Cola que no se pudo vaciar: sus trabajos siguen ahí.
        for (const auto& printer : result.failedPrinters)
            for (const auto& job : snapshot_->jobs)
                if (job.printer == printer) tracker_.ForgetCancelledByUser(job.Key());
    }
    // Las acciones sobre impresoras informan solo en la barra de estado: un
    // diálogo modal bloquearía la ventana y es habitual que falten permisos.
    const bool printerAction = result.action == JobAction::PausePrinter ||
                               result.action == JobAction::ResumePrinter || result.action == JobAction::PurgePrinter;
    if (printerAction) startPauseInFlight_ = false;
    const bool quiet = printerAction;
    // Error: siempre en la barra (rojo); falta de permisos en una acción
    // sobre trabajos también con un diálogo, para que no pase inadvertido.
    // Éxito con mensaje: verde, o amarillo si se omitieron copias locales (D50).
    if (!result.ok()) {
        ShowTransient(result.message, ui::Tone::Danger);
        if (result.firstError == ERROR_ACCESS_DENIED && !quiet)
            MessageBoxW(hwnd_, result.message.c_str(), Tr(S::AppTitle), MB_OK | MB_ICONWARNING);
    } else if (!result.message.empty()) {
        ShowTransient(result.message, result.skipped ? ui::Tone::Warning : ui::Tone::Success);
    }
    // Refresco inmediato para que la lista muestre el efecto sin esperar el sondeo.
    monitor_->RequestRefresh();
}

// Mensaje temporal en la barra de estado: 4 s (8 s si es un error); el
// temporizador kTimerTransient lo borra. Un mensaje nuevo reemplaza al anterior.
void MainWindow::ShowTransient(const std::wstring& text, ui::Tone tone) {
    transientText_ = text;
    transientTone_ = tone;
    UpdateStatus();
    SetTimer(hwnd_, kTimerTransient, tone == ui::Tone::Danger ? 8000 : 4000, nullptr);
}

// Vista rápida (Espacio): la vista previa ocupa toda la ventana sobre la
// lista. No existe en el historial.
void MainWindow::ToggleQuickLook() {
    if (historyView_) return;
    quickLook_ = !quickLook_;
    preview_.SetQuickLook(quickLook_);
    Layout();
    UpdatePreview();
}

// Alterna entre la cola y el historial (la misma lista con otras columnas).
// Al entrar al historial se cierra la vista rápida.
void MainWindow::ToggleHistory() {
    historyView_ = !historyView_;
    if (historyView_ && quickLook_) {
        quickLook_ = false;
        preview_.SetQuickLook(false);
    }
    RebuildRows();
    UpdateHeader();
    UpdatePrinterBar();
    UpdateStatus();
    Layout();
    UpdatePreview();
    SetFocus(list_.hwnd());
}

// Exportar a Excel (D43): diálogo de período (ui/ExportDialog), filtro,
// "Guardar como" y escritura (HistoryExport). Todo en el hilo de la UI;
// ofrece abrir el archivo al terminar.
void MainWindow::ExportHistory() {
    if (history_.Entries().empty()) {
        ShowTransient(Tr(S::ExportEmpty), ui::Tone::Warning);
        return;
    }
    // Todo el historial o un rango de los días que tienen registros.
    const auto days = HistoryDays(history_.Entries());
    std::vector<ui::ExportDay> choices;
    for (const auto& day : days) choices.push_back({DayDisplay(day.key), day.records});
    ui::ExportChoice choice;
    choice.to = choices.size() - 1;  // el rango propuesto abarca todo; el operador lo acota
    if (!ui::ShowExportDialog(hwnd_, choices, choice)) return;
    // choice.from/to son índices en `days`; se traducen a claves de día.
    ExportPeriod period;
    if (!choice.all) period = {days[choice.from].key, days[choice.to].key};
    const auto entries = FilterHistory(history_.Entries(), period);

    std::wstring error;
    const std::wstring path =
        RunHistoryExport(hwnd_, entries, EffectiveColumnOrder(settings_.columnOrder), period, error);
    if (!error.empty()) {
        const std::wstring message = util::Format(Tr(S::ExportFailed), error.c_str());
        ShowTransient(message, ui::Tone::Danger);
        MessageBoxW(hwnd_, message.c_str(), Tr(S::AppTitle), MB_OK | MB_ICONWARNING);
        return;
    }
    if (path.empty()) return;  // canceló el diálogo
    ShowTransient(util::Format(Tr(S::ExportDone), path.c_str()), ui::Tone::Success);
    const std::wstring question = util::Format(Tr(S::ExportOpenNow), static_cast<unsigned>(entries.size()), path.c_str());
    // Abre el .xlsx con la aplicación asociada (Excel o la que haya).
    if (MessageBoxW(hwnd_, question.c_str(), Tr(S::AppTitle), MB_YESNO | MB_ICONINFORMATION) == IDYES)
        ShellExecuteW(hwnd_, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Texto del cuadro de búsqueda (se llama con cada tecla): filtra la lista.
void MainWindow::SetFilter(const std::wstring& filter) {
    filter_ = util::Trim(filter);
    RebuildRows();
    UpdateStatus();
}

// Pasa al criterio de orden siguiente/anterior (con vuelta), conservando la dirección.
void MainWindow::CycleSort(int delta) {
    int index = static_cast<int>(CurrentSort(settings_));
    index = ((index + delta) % kSortCount + kSortCount) % kSortCount;
    SetSort(SortById(static_cast<SortBy>(index)), settings_.sortDescending);
}

// Fija el orden de la cola (id de SortById), lo guarda y rearma la lista.
// announce = mostrar el nuevo orden en la barra de estado.
void MainWindow::SetSort(const std::wstring& sortId, bool descending, bool announce) {
    settings_.sortBy = SortById(SortByFromId(sortId));  // normaliza valores desconocidos
    settings_.sortDescending = descending;
    SaveSettings();
    RebuildRows();
    UpdateStatus();
    if (!announce) return;
    const SortBy sort = CurrentSort(settings_);
    ShowTransient(util::Format(Tr(descending ? S::StatusSortedByDesc : S::StatusSortedBy), SortName(sort).c_str()),
                  ui::Tone::Neutral);
}

// Clic en un título (D30): sortId es el SortBy de la columna. El historial y
// las columnas sin orden (-1) no hacen nada.
void MainWindow::OnColumnClicked(int sortId) {
    if (historyView_ || sortId < 0 || sortId >= kSortCount) return;
    auto [sort, descending] = NextSort(CurrentSort(settings_), settings_.sortDescending, static_cast<SortBy>(sortId));
    // La flecha del encabezado ya muestra el orden: sin aviso extra en la barra de estado.
    SetSort(SortById(sort), descending, false);
}

std::wstring MainWindow::SpoolOverrideDir() const {
    auto it = settings_.spoolDirOverrides.find(settings_.activeServer);
    return it == settings_.spoolDirOverrides.end() ? L"" : it->second;
}

// ---------------------------------------------------------------------------
// Menús
// ---------------------------------------------------------------------------

// Menú del botón de impresora de la cabecera: "Todas" y cada impresora con
// su cantidad de trabajos y su problema; al final, el acceso rápido.
// Se atiende aquí mismo (no pasa por HandleMenuCommand).
void MainWindow::ShowPrinterMenu(POINT screen) {
    HMENU menu = CreatePopupMenu();
    // "Todas las impresoras" arriba, marcada si es la vista actual.
    AppendMenuW(menu, MF_STRING | (settings_.activePrinter == kAllPrinters ? MF_CHECKED : 0), ID_PRINTER_ALL,
                Tr(S::AllPrinters));
    // menuPrinters_ guarda el nombre de cada ítem en el orden de sus ids
    // (ID_PRINTER_BASE + índice) para traducir el id elegido a impresora.
    menuPrinters_.clear();
    if (snapshot_ && !snapshot_->printers.empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        for (const auto& p : snapshot_->printers) {
            // Etiqueta: "Nombre  (N)  —  problema", con el conteo solo si
            // tiene trabajos y el problema más grave si lo hay.
            std::wstring label = p.DisplayName();
            unsigned jobs = 0;
            for (const auto& j : snapshot_->jobs)
                if (j.printer == p.name) ++jobs;
            if (jobs) label += L"  (" + std::to_wstring(jobs) + L")";
            auto problem = core::MostSevereProblem(p.problems);
            if (problem != core::ProblemNone) label += L"  —  " + ProblemText(problem);
            AppendMenuW(menu, MF_STRING | (p.name == settings_.activePrinter ? MF_CHECKED : 0),
                        ID_PRINTER_BASE + menuPrinters_.size(), label.c_str());
            menuPrinters_.push_back(p.name);
            // Límite del rango ID_PRINTER_BASE (1001..1999).
            if (menuPrinters_.size() >= 998) break;
        }
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_PIN_CURRENT, Tr(S::MenuQuickAccess));
    // TPM_RETURNCMD: devuelve el id elegido (0 si se cerró sin elegir) en
    // vez de mandar WM_COMMAND.
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (id == ID_PRINTER_ALL) SelectPrinter(kAllPrinters);
    else if (id == ID_PIN_CURRENT) ShowQuickAccessMenu(screen);
    else if (id >= ID_PRINTER_BASE && id < ID_PRINTER_BASE + menuPrinters_.size())
        SelectPrinter(menuPrinters_[id - ID_PRINTER_BASE]);
}

// Menú principal (botón ☰ o su atajo): conexión y servidores recientes,
// vista, orden, columnas, acceso rápido, impresora, opciones, idioma, tema,
// historial y salida. El id elegido se procesa en HandleMenuCommand.
void MainWindow::ShowMainMenu(POINT screen) {
    HMENU menu = CreatePopupMenu();
    // Marca de verificación según un booleano.
    auto check = [](bool on) -> UINT { return on ? MF_CHECKED : MF_UNCHECKED; };
    // Texto del menú con el atajo actual del comando a la derecha (tras "\t").
    auto keyed = [this](S text, Command command) {
        std::wstring label = Tr(text);
        auto keys = shortcuts_.Get(command);
        if (!keys.empty()) label += L"\t" + keys.front().ToString();
        return label;
    };

    AppendMenuW(menu, MF_STRING, ID_CONNECT, keyed(S::CmdConnect, Command::Connect).c_str());
    // El servidor de este botón se cambia sin compilar (D54): desde aquí (para
    // este usuario) o en servidor-laimi.txt junto al .exe (para todos).
    const std::wstring laimi = config::LaimiServer(settings_);
    const bool onLaimi = util::EqualsInsensitive(settings_.activeServer, laimi);
    AppendMenuW(menu, MF_STRING | check(onLaimi), ID_CONNECT_LAIMI,
                util::Format(Tr(S::MenuConnectLaimi), laimi.c_str()).c_str());
    AppendMenuW(menu, MF_STRING, ID_SET_LAIMI, Tr(S::MenuSetLaimi));
    // Servidores recientes (sangrados bajo "Conectar"); "Este equipo" siempre
    // está en la lista. ID_SERVER_BASE + índice en menuServers_ (máx. 99).
    // Se copia la lista porque HandleMenuCommand traduce el id con ella
    // después de cerrar el menú.
    menuServers_ = settings_.servers;
    if (std::find(menuServers_.begin(), menuServers_.end(), L"") == menuServers_.end()) menuServers_.push_back(L"");
    for (size_t i = 0; i < menuServers_.size() && i < 99; ++i) {
        std::wstring label = L"    " + (menuServers_[i].empty() ? std::wstring(Tr(S::LocalServer)) : menuServers_[i]);
        AppendMenuW(menu, MF_STRING | check(util::EqualsInsensitive(menuServers_[i], settings_.activeServer)),
                    ID_SERVER_BASE + i, label.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // Vista: actualizar y alternar cola/historial (el texto dice a cuál se va).
    AppendMenuW(menu, MF_STRING, ID_REFRESH, keyed(S::MenuRefresh, Command::Refresh).c_str());
    AppendMenuW(menu, MF_STRING, ID_HISTORY,
                keyed(historyView_ ? S::MenuQueue : S::MenuHistory, Command::History).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // Orden de la cola (por equipo, por hora...).
    HMENU sortMenu = CreatePopupMenu();
    for (int i = 0; i < kSortCount; ++i) {
        auto sort = static_cast<SortBy>(i);
        std::wstring label = SortName(sort);
        // El atajo de "rotar criterio" se muestra junto al primero (orden de
        // la cola), ya que no corresponde a un criterio en particular.
        if (sort == SortBy::Queue) {
            auto keys = shortcuts_.Get(Command::SortCycle);
            if (!keys.empty()) label += L"	" + keys.front().ToString();
        }
        AppendMenuW(sortMenu, MF_STRING | check(sort == CurrentSort(settings_)), ID_SORT_BASE + i, label.c_str());
    }
    AppendMenuW(sortMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sortMenu, MF_STRING | check(settings_.sortDescending), ID_SORT_DESC,
                keyed(S::MenuSortDescending, Command::SortReverse).c_str());
    // El submenú pasa a ser del menú principal: DestroyMenu(menu) lo libera también.
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sortMenu), Tr(S::MenuSortBy));
    // Restablecer columnas: atenuados si no hay anchos u orden personalizados.
    AppendMenuW(menu, MF_STRING | (settings_.columnWidths.empty() ? MF_GRAYED : 0), ID_RESET_COLUMNS,
                Tr(S::MenuResetColumns));
    AppendMenuW(menu, MF_STRING | (settings_.columnOrder.empty() ? MF_GRAYED : 0), ID_RESET_COLUMN_ORDER,
                Tr(S::MenuResetColumnOrder));
    // Acceso rápido: qué impresoras aparecen en la barra superior.
    HMENU pinMenu = CreatePopupMenu();
    menuPrinters_.clear();
    if (snapshot_) {
        for (const auto& p : snapshot_->printers) {
            AppendMenuW(pinMenu, MF_STRING | check(IsPinned(settings_.favoritePrinters, p.name)),
                        ID_PIN_BASE + menuPrinters_.size(), p.DisplayName().c_str());
            menuPrinters_.push_back(p.name);
            // Límite del rango de ids ID_PIN_BASE.
            if (menuPrinters_.size() >= 799) break;
        }
    }
    if (menuPrinters_.empty()) AppendMenuW(pinMenu, MF_STRING | MF_GRAYED, 0, Tr(S::NoPrinters));
    AppendMenuW(pinMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(pinMenu, MF_STRING, ID_PIN_CURRENT, keyed(S::MenuPinPrinter, Command::PinPrinter).c_str());
    AppendMenuW(pinMenu, MF_STRING | check(settings_.showPrinterBar), ID_SHOW_PRINTER_BAR,
                Tr(S::MenuShowPrinterBar));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(pinMenu), Tr(S::MenuQuickAccess));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // Acciones sobre impresoras / F11.
    AppendMenuW(menu, MF_STRING, ID_PAUSE_BOTH, keyed(S::MenuPauseBoth, Command::TogglePauseBoth).c_str());
    AppendMenuW(menu, MF_STRING, ID_PRINT_REPAUSE, keyed(S::CmdPrintAndRepause, Command::PrintAndRepause).c_str());
    AppendMenuW(menu, MF_STRING, ID_PAUSE_PRINTER, keyed(S::MenuPausePrinter, Command::PausePrinter).c_str());
    AppendMenuW(menu, MF_STRING, ID_RESUME_PRINTER, keyed(S::MenuResumePrinter, Command::ResumePrinter).c_str());
    AppendMenuW(menu, MF_STRING, ID_PURGE, Tr(S::MenuPurge));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // Apariencia y opciones (las de retención y pausa al iniciar, D17/D18).
    AppendMenuW(menu, MF_STRING | check(settings_.theme == config::ThemeMode::System), ID_THEME_SYSTEM,
                Tr(S::MenuThemeSystem));
    AppendMenuW(menu, MF_STRING | check(settings_.theme == config::ThemeMode::Light), ID_THEME_LIGHT,
                Tr(S::MenuThemeLight));
    AppendMenuW(menu, MF_STRING | check(settings_.theme == config::ThemeMode::Dark), ID_THEME_DARK,
                Tr(S::MenuThemeDark));
    AppendMenuW(menu, MF_STRING, ID_LANGUAGE, Tr(S::MenuLanguage));
    AppendMenuW(menu, MF_STRING | check(settings_.showPreviewPane), ID_PREVIEW_PANE,
                keyed(S::MenuPreviewPane, Command::TogglePane).c_str());
    AppendMenuW(menu, MF_STRING | check(settings_.autoHoldNewJobs), ID_AUTO_HOLD,
                keyed(S::MenuAutoHold, Command::ToggleAutoHold).c_str());
    AppendMenuW(menu, MF_STRING | check(settings_.pauseAllOnStart), ID_PAUSE_ON_START, Tr(S::MenuPauseOnStart));
    AppendMenuW(menu, MF_STRING | check(settings_.confirmCancel), ID_CONFIRM_CANCEL, Tr(S::MenuConfirmCancel));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // Notificaciones (las tres opciones se excluyen entre sí), inicio con
    // Windows y minimizar a la bandeja.
    AppendMenuW(menu, MF_STRING | check(settings_.notifications == config::NotifyMode::None), ID_NOTIFY_NONE,
                Tr(S::MenuNotifyNone));
    AppendMenuW(menu, MF_STRING | check(settings_.notifications == config::NotifyMode::ErrorsOnly), ID_NOTIFY_ERRORS,
                Tr(S::MenuNotifyErrors));
    AppendMenuW(menu, MF_STRING | check(settings_.notifications == config::NotifyMode::All), ID_NOTIFY_ALL,
                Tr(S::MenuNotifyAll));
    AppendMenuW(menu, MF_STRING | check(settings_.startWithWindows), ID_START_WITH_WINDOWS, Tr(S::MenuStartWithWindows));
    AppendMenuW(menu, MF_STRING | check(settings_.minimizeToTray), ID_MINIMIZE_TO_TRAY, Tr(S::MenuMinimizeToTray));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // Herramientas: atajos (F1), carpeta de datos, exportar (atenuado sin
    // historial), borrar historial, reiniciar como administrador y "Acerca de".
    AppendMenuW(menu, MF_STRING, ID_SHORTCUTS, keyed(S::MenuShortcuts, Command::Help).c_str());
    AppendMenuW(menu, MF_STRING, ID_OPEN_DATA, Tr(S::MenuOpenDataFolder));
    AppendMenuW(menu, MF_STRING | (history_.Entries().empty() ? MF_GRAYED : 0), ID_EXPORT_HISTORY,
                keyed(S::MenuExportHistory, Command::ExportHistory).c_str());
    AppendMenuW(menu, MF_STRING, ID_CLEAR_HISTORY, Tr(S::MenuClearHistory));
    // Solo si no se está ya como administrador (D15).
    if (!IsElevated()) AppendMenuW(menu, MF_STRING, ID_RUN_AS_ADMIN, Tr(S::MenuRunAsAdmin));
    AppendMenuW(menu, MF_STRING, ID_ABOUT, Tr(S::MenuAbout));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, Tr(S::MenuExit));

    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (id) HandleMenuCommand(id);
}

// Menú contextual de la lista (clic derecho o tecla de menú): las acciones
// sobre la selección con sus atajos y "Cancelar todos los de <usuario>".
// Casi todas las opciones se traducen al Command equivalente (Execute).
void MainWindow::ShowJobContextMenu(POINT screen) {
    if (historyView_) return;
    auto jobs = SelectedJobs();
    if (jobs.empty()) return;
    // Se copia el usuario del trabajo con el foco ANTES de abrir el menú (F1):
    // TrackPopupMenu tiene su propio bucle de mensajes y, mientras el menú
    // está abierto, llegan WM_APP_SNAPSHOT que reemplazan snapshot_ y liberan
    // la instantánea a la que apunta FocusedJob(). El puntero no se guarda.
    std::wstring focusedUser;
    if (const core::JobInfo* focused = FocusedJob()) focusedUser = focused->user;
    HMENU menu = CreatePopupMenu();
    // Igual que en el menú principal: texto, tabulación y el primer atajo del comando.
    auto keyed = [this](S text, Command command) {
        std::wstring label = Tr(text);
        auto keys = shortcuts_.Get(command);
        if (!keys.empty()) label += L"\t" + keys.front().ToString();
        return label;
    };
    // Estado del trabajo: pausar, pausar trabajo e impresora, imprimir y
    // volver a pausar, reanudar, reiniciar y cancelar.
    AppendMenuW(menu, MF_STRING, ID_CTX_PAUSE, keyed(S::CtxPause, Command::Pause).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_PAUSE_BOTH, keyed(S::CtxPauseBoth, Command::TogglePauseBoth).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_PRINT_REPAUSE,
                keyed(S::CmdPrintAndRepause, Command::PrintAndRepause).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_RESUME, keyed(S::CtxResume, Command::Resume).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_RESTART, keyed(S::CtxRestart, Command::Restart).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_CANCEL, keyed(S::CtxCancel, Command::Cancel).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // Posición en la cola y reenvío a otra impresora.
    AppendMenuW(menu, MF_STRING, ID_CTX_UP, keyed(S::CtxMoveUp, Command::MoveUp).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_DOWN, keyed(S::CtxMoveDown, Command::MoveDown).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_REDIRECT, keyed(S::CtxRedirect, Command::Redirect).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_CTX_PREVIEW, keyed(S::CtxPreview, Command::QuickLook).c_str());
    if (!focusedUser.empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_CTX_CANCEL_USER,
                    util::Format(Tr(S::CtxCancelUser), focusedUser.c_str()).c_str());
    }
    // TPM_RIGHTBUTTON: los ítems también se eligen con el botón derecho.
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    switch (id) {
        case ID_CTX_PAUSE: Execute(Command::Pause); break;
        case ID_CTX_PAUSE_BOTH: Execute(Command::TogglePauseBoth); break;
        case ID_CTX_PRINT_REPAUSE: Execute(Command::PrintAndRepause); break;
        case ID_CTX_RESUME: Execute(Command::Resume); break;
        case ID_CTX_RESTART: Execute(Command::Restart); break;
        case ID_CTX_CANCEL: Execute(Command::Cancel); break;
        case ID_CTX_UP: Execute(Command::MoveUp); break;
        case ID_CTX_DOWN: Execute(Command::MoveDown); break;
        // Reenviar abre el menú de destinos donde estaba este menú.
        case ID_CTX_REDIRECT: StartRedirect(screen); break;
        case ID_CTX_PREVIEW: Execute(Command::QuickLook); break;
        // Cancela todos los trabajos visibles (impresora elegida, sin filtro)
        // del mismo usuario que el trabajo que tenía el foco al abrir el menú
        // (focusedUser, copiado antes: F1). Los trabajos se buscan en la
        // instantánea ACTUAL, que puede ser más nueva que la de entonces.
        case ID_CTX_CANCEL_USER:
            if (!focusedUser.empty() && snapshot_) {
                std::vector<core::JobInfo> userJobs;
                for (const auto* job : VisibleJobs(*snapshot_, settings_.activePrinter, L""))
                    if (util::EqualsInsensitive(job->user, focusedUser)) userJobs.push_back(*job);
                CancelSelection(userJobs);
            }
            break;
        default: break;
    }
}

// Menú del ícono de la bandeja (clic derecho): Mostrar (predeterminado, en
// negrita) y Salir.
void MainWindow::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, Tr(S::TrayShow));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, Tr(S::MenuExit));
    SetMenuDefaultItem(menu, ID_TRAY_SHOW, FALSE);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);  // necesario para que el menú se cierre al hacer clic fuera
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (id == ID_TRAY_SHOW) ShowFromTray();
    else if (id == ID_TRAY_EXIT) Quit();
}

// Atiende un id de los menús principal, de acceso rápido y de los chips.
// Primero los rangos dinámicos (servidores, criterios de orden, impresoras a
// fijar), validados contra la lista guardada al armar el menú; luego los ids fijos.
void MainWindow::HandleMenuCommand(UINT id) {
    if (id >= ID_SERVER_BASE && id < ID_SERVER_BASE + menuServers_.size()) {
        ConnectTo(menuServers_[id - ID_SERVER_BASE]);
        return;
    }
    if (id >= ID_SORT_BASE && id < ID_SORT_BASE + static_cast<UINT>(kSortCount)) {
        SetSort(SortById(static_cast<SortBy>(id - ID_SORT_BASE)), settings_.sortDescending);
        return;
    }
    if (id >= ID_PIN_BASE && id < ID_PIN_BASE + menuPrinters_.size()) {
        TogglePinnedPrinter(menuPrinters_[id - ID_PIN_BASE]);
        return;
    }
    switch (id) {
        // Invierte la dirección conservando el criterio.
        case ID_SORT_DESC: SetSort(settings_.sortBy, !settings_.sortDescending); break;
        // Fija/quita la impresora efectiva (la elegida o la del trabajo con el foco).
        case ID_PIN_CURRENT: TogglePinnedPrinter(EffectivePrinter()); break;
        // Mostrar u ocultar la barra de acceso rápido (cambia el layout).
        case ID_SHOW_PRINTER_BAR:
            settings_.showPrinterBar = !settings_.showPrinterBar;
            SaveSettings();
            Layout();
            UpdatePrinterBar();
            break;
        // Opciones del menú de un chip de la barra (la impresora está en chipPrinter_).
        case ID_CHIP_PIN: TogglePinnedPrinter(chipPrinter_); break;
        case ID_CHIP_PAUSE:
        case ID_CHIP_RESUME:
            // La barra actúa sobre su impresora (o sobre todas en el chip "Todas").
            if (chipPrinter_ == kAllPrinters) SetPrintersPaused(id == ID_CHIP_PAUSE, TargetPrinters());
            else SetPrintersPaused(id == ID_CHIP_PAUSE, {chipPrinter_});
            break;
        // Vaciar la impresora del chip: confirmación y marca de cancelados
        // (igual que RunPrinterAction, pero con chipPrinter_).
        case ID_CHIP_PURGE:
            if (!chipPrinter_.empty() && chipPrinter_ != kAllPrinters) {
                std::wstring question = util::Format(Tr(S::ConfirmPurge), PrinterDisplay(chipPrinter_).c_str());
                if (MessageBoxW(hwnd_, question.c_str(), Tr(S::AppTitle), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) ==
                    IDYES) {
                    if (snapshot_)
                        for (const auto& job : snapshot_->jobs)
                            if (job.printer == chipPrinter_) tracker_.MarkCancelledByUser(job.Key());
                    actions_->RunOnPrinters(JobAction::PurgePrinter, {chipPrinter_});
                }
            }
            break;
        case ID_CONNECT: Execute(Command::Connect); break;
        case ID_CONNECT_LAIMI: ConnectTo(config::LaimiServer(settings_)); break;
        // "Cambiar servidor del LAIMI…" (D54): el cuadro de búsqueda pasa a
        // modo servidor con el actual; editingLaimi_ hace que Enter lo guarde
        // con SetLaimiServer en vez de solo conectar.
        case ID_SET_LAIMI:
            editingLaimi_ = true;
            header_.BeginServerInput(config::LaimiServer(settings_), Tr(S::LaimiServerPlaceholder));
            break;
        case ID_REFRESH: Execute(Command::Refresh); break;
        case ID_HISTORY: ToggleHistory(); break;
        case ID_PAUSE_BOTH: Execute(Command::TogglePauseBoth); break;
        case ID_PRINT_REPAUSE: Execute(Command::PrintAndRepause); break;
        case ID_RESET_COLUMNS: ResetColumnWidths(); break;
        case ID_RESET_COLUMN_ORDER: ResetColumnOrder(); break;
        case ID_PAUSE_PRINTER: TogglePrinterPause(); break;
        case ID_RESUME_PRINTER: SetPrintersPaused(false); break;
        // Retener los trabajos existentes al iniciar (D18); se le avisa al
        // monitor para que lo aplique en la próxima conexión.
        case ID_PAUSE_ON_START:
            settings_.pauseAllOnStart = !settings_.pauseAllOnStart;
            monitor_->SetHoldExistingOnStart(settings_.pauseAllOnStart);
            SaveSettings();
            break;
        case ID_PURGE: RunPrinterAction(JobAction::PurgePrinter); break;
        // Tema: seguir a Windows, claro u oscuro (se aplica al instante).
        case ID_THEME_SYSTEM:
        case ID_THEME_LIGHT:
        case ID_THEME_DARK:
            settings_.theme = id == ID_THEME_SYSTEM  ? config::ThemeMode::System
                              : id == ID_THEME_LIGHT ? config::ThemeMode::Light
                                                     : config::ThemeMode::Dark;
            ApplyTheme();
            SaveSettings();
            break;
        // Alterna español/inglés al instante (D16).
        case ID_LANGUAGE:
            i18n::SetLanguage(i18n::GetLanguage() == i18n::Language::Spanish ? i18n::Language::English
                                                                             : i18n::Language::Spanish);
            SaveSettings();
            ApplyLanguage();
            break;
        case ID_PREVIEW_PANE: Execute(Command::TogglePane); break;
        case ID_AUTO_HOLD: Execute(Command::ToggleAutoHold); break;
        // Preguntar antes de cancelar trabajos.
        case ID_CONFIRM_CANCEL:
            settings_.confirmCancel = !settings_.confirmCancel;
            SaveSettings();
            break;
        // Qué notificaciones de la bandeja se muestran.
        case ID_NOTIFY_NONE: settings_.notifications = config::NotifyMode::None; SaveSettings(); break;
        case ID_NOTIFY_ERRORS: settings_.notifications = config::NotifyMode::ErrorsOnly; SaveSettings(); break;
        case ID_NOTIFY_ALL: settings_.notifications = config::NotifyMode::All; SaveSettings(); break;
        // Clave Run del registro; la marca solo cambia si se pudo escribir.
        case ID_START_WITH_WINDOWS:
            if (config::SetAutostart(!settings_.startWithWindows)) settings_.startWithWindows = !settings_.startWithWindows;
            SaveSettings();
            break;
        // La X de la ventana oculta en la bandeja en vez de salir.
        case ID_MINIMIZE_TO_TRAY:
            settings_.minimizeToTray = !settings_.minimizeToTray;
            SaveSettings();
            break;
        case ID_SHORTCUTS: help_.Open(); break;
        // Abre la carpeta de config.json (%LOCALAPPDATA%\ColaDeImpresion, D49).
        case ID_OPEN_DATA:
            ShellExecuteW(hwnd_, L"open", config::SettingsDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case ID_EXPORT_HISTORY: ExportHistory(); break;
        // Borra el historial y lo guarda enseguida (sin confirmación).
        case ID_CLEAR_HISTORY:
            history_.Clear();
            history_.Save();
            if (historyView_) RebuildRows();
            UpdateStatus();
            break;
        case ID_RUN_AS_ADMIN: RestartAsAdmin(); break;
        case ID_ABOUT: ui::ShowAboutDialog(hwnd_); break;
        case ID_EXIT: Quit(); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Ventana
// ---------------------------------------------------------------------------

// Reparte el área cliente: cabecera arriba, barra de estado abajo, barra de
// impresoras bajo la cabecera y, en el cuerpo, la lista y el panel de
// detalles separados por el divisor (splitter_). El panel de ayuda cubre el
// cuerpo. Se llama en WM_SIZE, al cambiar estilo/DPI y al mostrar u ocultar
// partes. Si todavía no existen los controles (durante Create), no hace nada.
void MainWindow::Layout() {
    if (!hwnd_ || !list_.hwnd()) return;
    const auto& style = Style::Get();
    RECT client;
    GetClientRect(hwnd_, &client);
    const int headerH = header_.PreferredHeight();
    const int statusH = status_.PreferredHeight();
    header_.Move({client.left, client.top, client.right, client.top + headerH});
    status_.Move({client.left, client.bottom - statusH, client.right, client.bottom});
    // Barra de acceso rápido a impresoras (bajo la cabecera; se oculta en el historial).
    const bool showBar = settings_.showPrinterBar && !historyView_ && !quickLook_;
    const int barH = showBar ? printerBar_.PreferredHeight() : 0;
    if (showBar)
        printerBar_.Move({client.left, client.top + headerH, client.right, client.top + headerH + barH});
    printerBar_.Show(showBar);
    RECT body{client.left, client.top + headerH + barH, client.right, client.bottom - statusH};
    help_.Move(body);

    splitter_ = {};  // sin divisor salvo que se muestre el panel
    const int width = body.right - body.left;
    // Vista rápida: el panel ocupa todo el cuerpo y la lista se oculta.
    if (quickLook_ && !historyView_) {
        list_.Show(false);
        preview_.Show(true);
        preview_.Move(body);
        return;
    }
    list_.Show(true);
    // Con menos de 720 DIP de ancho no hay lugar para el panel: se oculta
    // aunque esté activado (el botón de la cabecera lo refleja).
    const bool showPane = settings_.showPreviewPane && width >= style.Px(720);
    header_.SetDetailsActive(showPane);
    if (!showPane) {
        preview_.Show(false);
        list_.Move(body);
        return;
    }
    // Ancho del panel = proporción guardada (previewRatio, la cambia el
    // arrastre del divisor), con mínimo 280 DIP y dejando al menos 380 DIP
    // a la lista.
    const int splitW = style.Px(6);
    int paneW = static_cast<int>(width * settings_.previewRatio);
    paneW = std::clamp(paneW, style.Px(280), std::max(style.Px(280), width - style.Px(380)));
    RECT listRect{body.left, body.top, body.right - paneW - splitW, body.bottom};
    splitter_ = {listRect.right, body.top, listRect.right + splitW, body.bottom};
    RECT paneRect{splitter_.right, body.top, body.right, body.bottom};
    list_.Move(listRect);
    preview_.Show(true);
    preview_.Move(paneRect);
    // El divisor lo pinta la ventana principal (WM_PAINT).
    InvalidateRect(hwnd_, &splitter_, FALSE);
}

// Muestra la ventana (desde la bandeja, minimizada o detrás de otras) y la
// pone al frente. UpdatePreview porque oculta no se generaban vistas previas.
void MainWindow::ShowFromTray() {
    if (!IsWindowVisible(hwnd_)) RestoreWindowPlacement(true);
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(hwnd_);
    UpdatePreview();
}

// Oculta la ventana (sigue en la bandeja y el monitor sigue corriendo:
// retención y notificaciones activas). Antes guarda su posición.
void MainWindow::HideToTray() {
    SaveWindowPlacement();
    SaveSettings();
    ShowWindow(hwnd_, SW_HIDE);
}

// Salir de verdad: reallyQuit_ hace que WM_CLOSE cierre en vez de ocultar en
// la bandeja. Se usa PostMessage para salir del menú/llamador actual antes.
void MainWindow::Quit() {
    reallyQuit_ = true;
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

// "Reiniciar como administrador" (D15): lanza el mismo .exe con el verbo
// "runas" (pide UAC) y --elevated-restart, que espera a que esta instancia
// cierre (Application.cpp), conectando al mismo servidor. Si el usuario
// rechaza UAC, ShellExecuteEx falla y esta instancia sigue abierta.
void MainWindow::RestartAsAdmin() {
    std::wstring exe = config::ExecutablePath();
    std::wstring args = L"--elevated-restart --server \"" + settings_.activeServer + L"\"";
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    sei.hwnd = hwnd_;
    if (ShellExecuteExW(&sei)) Quit();
}

// Notificación de la bandeja (globo/toast); error = ícono de error.
void MainWindow::Notify(const std::wstring& title, const std::wstring& text, bool error) {
    tray_.Notify(title, text, error);
}

// Filtro del bucle de mensajes (Application.cpp), antes de TranslateMessage/
// DispatchMessage. Devuelve true si el mensaje ya se atendió aquí (no se
// despacha). Solo mira mensajes de esta ventana o sus hijos (no los de
// diálogos de otras ventanas).
bool MainWindow::PreTranslate(MSG& msg) {
    if (!hwnd_ || (msg.hwnd != hwnd_ && !IsChild(hwnd_, msg.hwnd))) return false;

    // "?" fuera del cuadro de búsqueda abre la ayuda de atajos.

    if (msg.message == WM_CHAR && msg.wParam == L'?' && !header_.IsEditFocused() && !help_.IsOpen()) {
        help_.Open();
        return true;
    }
    // Con el panel de atajos abierto la rueda siempre lo desplaza a él, aunque
    // el foco haya quedado en otro control (p. ej. el cuadro de búsqueda).
    if (msg.message == WM_MOUSEWHEEL && help_.IsOpen() && msg.hwnd != help_.hwnd()) {
        SendMessageW(help_.hwnd(), WM_MOUSEWHEEL, msg.wParam, msg.lParam);
        return true;
    }
    // Del resto solo interesan las pulsaciones de teclas (WM_SYSKEYDOWN
    // incluye las combinaciones con Alt y F10).
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return false;

    const UINT vk = static_cast<UINT>(msg.wParam);
    // Con la ayuda abierta, todas las teclas son para ella (navegar, capturar
    // un atajo nuevo, cerrar).
    if (help_.IsOpen()) return help_.HandleKey(vk);

    // Combinación actual (lee Ctrl/Alt/Mayús del teclado con GetKeyState).
    const input::KeyChord chord = input::KeyChord::FromKeyEvent(vk);

    // Teclas especiales del cuadro de texto.
    if (header_.IsEditFocused()) {
        // Esc: cancela el modo servidor, o borra la búsqueda, y vuelve a la lista.
        if (vk == VK_ESCAPE) {
            editingLaimi_ = false;
            if (header_.InServerMode()) header_.EndServerInput();
            else if (!header_.EditText().empty()) header_.ClearSearch();
            SetFocus(list_.hwnd());
            return true;
        }
        // Enter: en modo servidor conecta (o guarda el del LAIMI, D54); en
        // búsqueda solo devuelve el foco a la lista (el filtro ya se aplicó).
        if (vk == VK_RETURN) {
            if (header_.InServerMode()) {
                std::wstring server = header_.EditText();
                header_.EndServerInput();
                if (editingLaimi_) SetLaimiServer(server);
                else ConnectTo(server);
                editingLaimi_ = false;
            }
            SetFocus(list_.hwnd());
            return true;
        }
        // Teclas de edición (letras, flechas, Ctrl+C/V/A...) son del EDIT:
        // no se interpretan como atajos.
        if (input::IsTextEditingChord(chord)) return false;
    }

    // Teclas fijas.
    // Esc deshace "un nivel": vista rápida, búsqueda, selección, historial.
    if (vk == VK_ESCAPE && !chord.ctrl && !chord.alt) {
        if (quickLook_) ToggleQuickLook();
        else if (!filter_.empty()) header_.ClearSearch();
        else if (list_.HasSelection()) list_.ClearSelection();
        else if (historyView_) ToggleHistory();
        return true;
    }
    // Enter solo: vista rápida (Ctrl+Enter es F11, por el mapa de atajos).
    if (vk == VK_RETURN && !chord.ctrl && !chord.alt && !chord.shift) {
        ToggleQuickLook();
        return true;
    }
    // Ctrl+1..9: impresora del acceso rápido (fijas, no reasignables).
    if (chord.ctrl && !chord.alt && !chord.shift && vk >= '1' && vk <= '9' && !header_.IsEditFocused()) {
        if (SelectQuickAccess(static_cast<int>(vk - '0'))) return true;
    }
    // Mayús + flechas / Inicio / Fin: extender la selección.
    if (chord.shift && !chord.ctrl && !chord.alt && (vk == VK_UP || vk == VK_DOWN)) {
        list_.MoveFocus(vk == VK_UP ? -1 : 1, true);
        return true;
    }
    if (chord.shift && !chord.ctrl && !chord.alt && (vk == VK_HOME || vk == VK_END)) {
        list_.MoveFocus(vk == VK_HOME ? INT_MIN : INT_MAX, true);
        return true;
    }

    // Atajos configurables (input/Shortcuts, config.json "shortcuts").
    if (auto command = shortcuts_.Find(chord)) {
        Execute(*command);
        return true;
    }
    return false;
}

// Procedimiento de ventana (hilo de la UI). Los mensajes registrados no son
// constantes, por eso se comparan antes del switch.
LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    // Otra instancia pidió mostrarse (Application.cpp).
    if (msg == ShowInstanceMessage()) {
        ShowFromTray();
        return 0;
    }
    // El Explorer se reinició y recreó la barra de tareas: hay que volver a
    // agregar el ícono de bandeja (si no, desaparece).
    if (msg == ui::TrayIcon::TaskbarCreatedMessage()) {
        tray_.Readd();
        return 0;
    }

    switch (msg) {
        // Mensajes de los hilos de fondo: el unique_ptr toma la propiedad del
        // dato enviado con PostOwned y lo libera al salir del case.
        case WM_APP_SNAPSHOT: {
            std::unique_ptr<core::SnapshotPtr> data(reinterpret_cast<core::SnapshotPtr*>(lParam));
            OnSnapshot(std::move(*data));
            return 0;
        }
        case WM_APP_PREVIEW: {
            std::unique_ptr<std::wstring> key(reinterpret_cast<std::wstring*>(lParam));
            // Solo interesa si es la vista previa que se está mostrando; las
            // demás quedan en la caché por si el operador vuelve a ellas.
            if (*key == currentPreviewKey_) UpdatePreview();
            return 0;
        }
        case WM_APP_FACTS: {
            // Agrupa los avisos que llegaron seguidos: basta con reconstruir una vez.
            MSG pending;
            while (PeekMessageW(&pending, hwnd_, WM_APP_FACTS, WM_APP_FACTS, PM_REMOVE)) {
            }
            if (!historyView_) RebuildRows();
            UpdateSelectionTotal();
            return 0;
        }
        case WM_APP_CARNETS: {
            // Igual: se descartan los avisos repetidos ya en cola.
            MSG pending;
            while (PeekMessageW(&pending, hwnd_, WM_APP_CARNETS, WM_APP_CARNETS, PM_REMOVE)) {
            }
            OnCarnets();
            return 0;
        }
        case WM_APP_ACTION: {
            std::unique_ptr<ActionResult> result(reinterpret_cast<ActionResult*>(lParam));
            OnActionResult(*result);
            return 0;
        }
        // Eventos del ícono de bandeja (NOTIFYICON_VERSION_4: el evento va
        // en LOWORD(lParam)). Clic, selección con teclado o clic en la
        // notificación = mostrar; clic derecho = menú.
        case ui::TrayIcon::kCallbackMessage:
            switch (LOWORD(lParam)) {
                case WM_LBUTTONUP:
                case NIN_SELECT:
                case NIN_KEYSELECT:
                case NIN_BALLOONUSERCLICK: ShowFromTray(); break;
                case WM_CONTEXTMENU:
                case WM_RBUTTONUP: ShowTrayMenu(); break;
            }
            return 0;
        // Temporizadores de un solo disparo (se matan al llegar).
        case WM_TIMER:
            if (wParam == kTimerSaveHistory) {
                KillTimer(hwnd_, kTimerSaveHistory);
                history_.Save();
                if (historyView_) UpdateStatus();
            } else if (wParam == kTimerTransient) {
                KillTimer(hwnd_, kTimerTransient);
                transientText_.clear();
                UpdateStatus();
            }
            return 0;
        case WM_SIZE:
            Layout();
            return 0;
        // Tamaño mínimo de la ventana (escalado al DPI).
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = {Style::Get().Px(560), Style::Get().Px(380)};
            return 0;
        }
        // Se movió a un monitor con otro DPI: nuevo DPI (HIWORD = vertical,
        // igual al horizontal), tamaño sugerido por Windows y redibujo.
        case WM_DPICHANGED: {
            Style::Get().SetDpi(HIWORD(wParam));
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            OnStyleChanged();
            return 0;
        }
        // "ImmersiveColorSet" = el usuario cambió el modo claro/oscuro de
        // Windows; solo importa si el tema sigue al sistema.
        case WM_SETTINGCHANGE:
            if (lParam && std::wstring(reinterpret_cast<const wchar_t*>(lParam)) == L"ImmersiveColorSet" &&
                settings_.theme == config::ThemeMode::System)
                ApplyTheme();
            return 0;
        // La ventana principal solo pinta el fondo y el divisor (una línea
        // vertical centrada); el resto son controles hijos.
        case WM_PAINT: {
            ui::gfx::BufferedPaint paint(hwnd_);
            const auto& pal = Style::Get().palette();
            ui::gfx::Fill(paint.dc(), paint.rect(), pal.surface);
            if (splitter_.right > splitter_.left) {
                ui::gfx::Fill(paint.dc(), splitter_, pal.surface);
                ui::gfx::VLine(paint.dc(), (splitter_.left + splitter_.right) / 2, splitter_.top, splitter_.bottom,
                               pal.border);
            }
            return 0;
        }
        // Fondo pintado en WM_PAINT con doble búfer: no borrar (evita parpadeo).
        case WM_ERASEBKGND: return 1;
        // Cursor de redimensionar sobre el divisor.
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd_, &pt);
                if (ui::gfx::Contains(splitter_, pt)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        // Arrastre del divisor: captura el ratón al presionar sobre él, en
        // cada movimiento recalcula previewRatio (entre 20 % y 70 % del ancho
        // para el panel) y al soltar lo guarda en config.json.
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (ui::gfx::Contains(splitter_, pt)) {
                draggingSplitter_ = true;
                SetCapture(hwnd_);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (draggingSplitter_) {
                RECT client;
                GetClientRect(hwnd_, &client);
                int width = client.right - client.left;
                if (width > 0) {
                    settings_.previewRatio = std::clamp(1.0 - static_cast<double>(GET_X_LPARAM(lParam)) / width, 0.2, 0.7);
                    Layout();
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if (draggingSplitter_) {
                draggingSplitter_ = false;
                ReleaseCapture();
                SaveSettings();
            }
            return 0;
        // Al activar la ventana el foco va a la lista (o a la ayuda si está abierta).
        case WM_SETFOCUS:
            SetFocus(help_.IsOpen() ? help_.hwnd() : list_.hwnd());
            return 0;
        // Cerrar: con "minimizar a la bandeja" la X solo oculta (la retención
        // sigue funcionando); si es Salir (reallyQuit_), guarda todo y destruye.
        case WM_CLOSE:
            if (settings_.minimizeToTray && !reallyQuit_) {
                HideToTray();
                return 0;
            }
            SaveWindowPlacement();
            SaveSettings();
            if (history_.IsDirty()) history_.Save();
            DestroyWindow(hwnd_);
            return 0;
        // Windows se apaga o cierra sesión: guardar ya (puede que no llegue
        // WM_CLOSE) y aceptar el cierre. NO se marca reallyQuit_ aquí (M3):
        // Windows solo pregunta, y si otra aplicación cancela el cierre la
        // marca quedaba puesta y la X cerraba la app en vez de ocultarla.
        case WM_QUERYENDSESSION:
            SaveWindowPlacement();
            SaveSettings();
            history_.Save();
            return TRUE;
        // Respuesta definitiva: wParam TRUE = la sesión termina de verdad
        // (ahora sí es una salida real); FALSE = el cierre se canceló y la
        // app sigue como estaba.
        case WM_ENDSESSION:
            if (wParam) {
                reallyQuit_ = true;
                if (history_.IsDirty()) history_.Save();
            }
            return 0;
        // Quita el ícono, detiene el monitor (deja de retener trabajos) y
        // termina el bucle de mensajes con código 0. Los demás hilos se
        // detienen en el destructor.
        case WM_DESTROY:
            tray_.Remove();
            monitor_->Stop();
            PostQuitMessage(0);
            return 0;
    }
    return Window::HandleMessage(msg, wParam, lParam);
}

}  // namespace cola::app
