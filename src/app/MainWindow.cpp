#include "app/MainWindow.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <climits>

#include "app/JobSheet.h"
#include "app/RowBuilder.h"
#include "config/Autostart.h"
#include "config/Paths.h"
#include "core/JobStatus.h"
#include "core/Spooler.h"
#include "i18n/Strings.h"
#include "ui/DarkMode.h"
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
constexpr UINT WM_APP_SNAPSHOT = WM_APP + 1;
constexpr UINT WM_APP_PREVIEW = WM_APP + 2;
constexpr UINT WM_APP_ACTION = WM_APP + 3;
constexpr UINT WM_APP_FACTS = WM_APP + 4;  // hay datos nuevos de trabajos (páginas x copias)

constexpr UINT_PTR kTimerSaveHistory = 1;
constexpr UINT_PTR kTimerTransient = 2;

// Identificadores de menú.
enum MenuId : UINT {
    ID_PRINTER_ALL = 1000,
    ID_PRINTER_BASE = 1001,  // 1001..1999
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
    ID_RUN_AS_ADMIN,
    ID_EXIT,
    ID_AUTO_HOLD,
    ID_PAUSE_ON_START,
    ID_PAUSE_BOTH,
    ID_PRINT_REPAUSE,
    ID_SORT_DESC,
    ID_SHOW_PRINTER_BAR,
    ID_PIN_CURRENT,
    ID_CHIP_PIN,
    ID_CHIP_PAUSE,
    ID_CHIP_RESUME,
    ID_CHIP_PURGE,
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
    ID_TRAY_SHOW = 2201,
    ID_TRAY_EXIT,
    ID_REDIRECT_BASE = 3000,  // 3000..3999
    ID_SERVER_BASE = 4000,    // 4000..4099
    ID_SORT_BASE = 4100,      // 4100..4119 (criterios de orden)
    ID_PIN_BASE = 4200,       // 4200..4999 (fijar impresoras en el acceso rápido)
};

template <typename T>
void PostOwned(HWND hwnd, UINT msg, T* data) {
    if (!hwnd || !PostMessageW(hwnd, msg, 0, reinterpret_cast<LPARAM>(data))) delete data;
}

std::wstring PrinterDisplay(const std::wstring& name) {
    core::PrinterInfo p;
    p.name = name;
    return p.DisplayName();
}

// Criterio de orden guardado en la configuración.
SortBy CurrentSort(const config::Settings& settings) { return SortByFromId(settings.sortBy); }

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

UINT MainWindow::ShowInstanceMessage() {
    static UINT message = RegisterWindowMessageW(L"ColaDeImpresion.ShowInstance");
    return message;
}

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() {
    // Detiene los hilos antes de destruir los controles.
    monitor_.reset();
    previews_.reset();
    actions_.reset();
}

bool MainWindow::Create(const StartupOptions& options) {
    settings_ = config::Settings::Load(config::SettingsFile());
    // El panel de detalles (vista previa y configuración) arranca siempre oculto:
    // la tabla usa todo el ancho y se abre con el botón de la cabecera, Ctrl+B o F7.
    settings_.showPreviewPane = false;
    if (options.hasServer) settings_.activeServer = options.server;
    settings_.startWithWindows = config::IsAutostartEnabled();
    i18n::SetLanguage(i18n::LanguageFromCode(settings_.language.c_str()));
    shortcuts_.ApplyOverrides(settings_.shortcuts);
    history_ = config::History(config::HistoryFile(), static_cast<size_t>(settings_.historyLimit));
    history_.Load();
    renderers_ = preview::DetectRenderers({settings_.ghostscriptPath, settings_.ghostpclPath, settings_.ghostxpsPath});
    util::LogInfo(L"Renderizadores: gs='%ls' pcl='%ls' xps='%ls'", renderers_.ghostscript.c_str(),
                  renderers_.ghostpcl.c_str(), renderers_.ghostxps.c_str());

    HINSTANCE instance = GetModuleHandleW(nullptr);
    icon_ = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                          GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    iconSmall_ = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                               LR_DEFAULTCOLOR));
    if (!icon_) icon_ = LoadIconW(nullptr, IDI_APPLICATION);
    if (!iconSmall_) iconSmall_ = icon_;

    const bool dark = ui::ResolveDark(settings_.theme);
    Style::Get().SetDark(dark);
    ui::ApplyAppDarkMode(dark);

    if (!CreateWin(kClassName, Tr(S::AppTitle), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, CW_USEDEFAULT, CW_USEDEFAULT,
                   settings_.window.width, settings_.window.height, nullptr, nullptr, icon_))
        return false;
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall_));
    ChangeWindowMessageFilterEx(hwnd_, ShowInstanceMessage(), MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd_, ui::TrayIcon::TaskbarCreatedMessage(), MSGFLT_ALLOW, nullptr);

    Style::Get().SetDpi(GetDpiForWindow(hwnd_));
    ui::ApplyWindowDarkMode(hwnd_, dark);

    // Controles.
    header_.Create(hwnd_, {[this](ui::HeaderButton b, RECT r) {
                               POINT below{r.left, r.bottom + Style::Get().Px(4)};
                               switch (b) {
                                   case ui::HeaderButton::Printer: ShowPrinterMenu(below); break;
                                   case ui::HeaderButton::History: ToggleHistory(); break;
                                   case ui::HeaderButton::Help: Execute(Command::Help); break;
                                   case ui::HeaderButton::Theme: ToggleTheme(); break;
                                   case ui::HeaderButton::Menu: ShowMainMenu(below); break;
                                   case ui::HeaderButton::Details: Execute(Command::TogglePane); break;
                                   default: break;
                               }
                           },
                           [this](const std::wstring& text) { SetFilter(text); }});
    printerBar_.Create(hwnd_, {[this](const std::wstring& printer) { SelectPrinter(printer); },
                               [this](const std::wstring& printer, POINT pt) { ShowPrinterChipMenu(printer, pt); },
                               [this](POINT pt) { ShowQuickAccessMenu(pt); }});
    list_.Create(hwnd_, {[this] { UpdatePreview(); }, [this](POINT pt) { ShowJobContextMenu(pt); },
                         [this] { ToggleQuickLook(); }, [this](int sortId) { OnColumnClicked(sortId); }});
    preview_.Create(hwnd_);
    status_.Create(hwnd_);
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
    HWND self = hwnd_;
    monitor_ = std::make_unique<core::QueueMonitor>(
        [self](core::SnapshotPtr s) { PostOwned(self, WM_APP_SNAPSHOT, new core::SnapshotPtr(std::move(s))); });
    previews_ = std::make_unique<preview::PreviewService>(
        [self](const std::wstring& key) { PostOwned(self, WM_APP_PREVIEW, new std::wstring(key)); });
    facts_ = std::make_unique<preview::JobFactsService>([self] { PostMessageW(self, WM_APP_FACTS, 0, 0); });
    actions_ = std::make_unique<JobActionRunner>(
        [self](ActionResult r) { PostOwned(self, WM_APP_ACTION, new ActionResult(std::move(r))); });

    tray_.Add(hwnd_, iconSmall_, Tr(S::AppTitle));

    OnStyleChanged();
    ConnectTo(settings_.activeServer);
    RestoreWindowPlacement(!options.startInTray);
    SetFocus(list_.hwnd());
    return true;
}

// ---------------------------------------------------------------------------
// Configuración, tema e idioma
// ---------------------------------------------------------------------------

void MainWindow::SaveSettings() {
    settings_.language = i18n::LanguageCode(i18n::GetLanguage());
    settings_.Save(config::SettingsFile());
}

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

void MainWindow::RestoreWindowPlacement(bool show) {
    const auto& w = settings_.window;
    RECT r{w.x, w.y, w.x + w.width, w.y + w.height};
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

void MainWindow::ApplyTheme() {
    const bool dark = ui::ResolveDark(settings_.theme);
    Style::Get().SetDark(dark);
    ui::ApplyAppDarkMode(dark);
    ui::ApplyWindowDarkMode(hwnd_, dark);
    OnStyleChanged();
}

void MainWindow::ApplyLanguage() {
    OnStyleChanged();
    RebuildRows();
    UpdatePreview();
}

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

void MainWindow::ToggleTheme() {
    settings_.theme = Style::Get().dark() ? config::ThemeMode::Light : config::ThemeMode::Dark;
    ApplyTheme();
    SaveSettings();
}

// ---------------------------------------------------------------------------
// Servidor e impresoras
// ---------------------------------------------------------------------------

void MainWindow::ConnectTo(const std::wstring& serverIn) {
    const std::wstring server = core::spooler::NormalizeServerName(serverIn);
    const bool changed = server != settings_.activeServer || !snapshot_;
    settings_.activeServer = server;
    // Lista de servidores recientes (el más reciente primero).
    auto& list = settings_.servers;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const std::wstring& s) { return util::EqualsInsensitive(s, server); }),
               list.end());
    list.insert(list.begin(), server);
    if (list.size() > 8) list.resize(8);

    if (changed) {
        snapshot_.reset();
        tracker_.Reset();
        release_.Reset();
        previews_->Clear();
        facts_->Clear();
        adminRights_.clear();
        lastProblems_.clear();
        connecting_ = true;
        startPausePending_ = settings_.pauseAllOnStart;
        if (!settings_.activePrinter.empty() && settings_.activePrinter != kAllPrinters &&
            settings_.activePrinter.rfind(server, 0) != 0)
            settings_.activePrinter = kAllPrinters;
    }
    monitor_->SetAutoHold(settings_.autoHoldNewJobs);
    monitor_->SetHoldExistingOnStart(settings_.pauseAllOnStart);
    monitor_->Start(server, settings_.pollIntervalMs);
    SaveSettings();
    RebuildRows();
    UpdateHeader();
    UpdatePrinterBar();
    UpdateStatus();
    UpdateTitle();
    UpdatePreview();
}

std::wstring MainWindow::ServerDisplayName() const {
    return settings_.activeServer.empty() ? std::wstring(Tr(S::LocalServer)) : settings_.activeServer;
}

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
    core::QueueSnapshot empty;
    printerBar_.SetChips(BuildQuickAccess(snapshot_ ? *snapshot_ : empty, settings_.favoritePrinters),
                         settings_.activePrinter);
}

void MainWindow::TogglePinnedPrinter(const std::wstring& printer) {
    if (printer.empty() || printer == kAllPrinters) {
        ShowTransient(Tr(S::NoPrinters), ui::Tone::Warning);
        return;
    }
    const bool pinned = IsPinned(settings_.favoritePrinters, printer);
    settings_.favoritePrinters = TogglePinned(settings_.favoritePrinters, printer);
    if (!pinned && !settings_.showPrinterBar) settings_.showPrinterBar = true;
    SaveSettings();
    Layout();
    UpdatePrinterBar();
    ShowTransient(std::wstring(Tr(pinned ? S::MenuUnpinPrinter : S::MenuPinPrinter)) + L": " +
                      PrinterDisplay(printer),
                  ui::Tone::Neutral);
}

bool MainWindow::SelectQuickAccess(int index) {
    // index 1..9 sobre las impresoras fijadas que existen en este servidor.
    if (!snapshot_ || index < 1) return false;
    auto chips = BuildQuickAccess(*snapshot_, settings_.favoritePrinters);
    if (static_cast<size_t>(index) >= chips.size()) return false;
    SelectPrinter(chips[static_cast<size_t>(index)].name);
    return true;
}

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
            if (menuPrinters_.size() >= 799) break;
        }
    }
    if (menuPrinters_.empty()) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, Tr(S::NoPrinters));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (settings_.showPrinterBar ? MF_CHECKED : 0), ID_SHOW_PRINTER_BAR,
                Tr(S::MenuShowPrinterBar));
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (id) HandleMenuCommand(id);
}

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

void MainWindow::CyclePrinter(int delta) {
    std::vector<std::wstring> options = {kAllPrinters};
    if (snapshot_)
        for (const auto& p : snapshot_->printers) options.push_back(p.name);
    auto it = std::find(options.begin(), options.end(), settings_.activePrinter);
    int index = it == options.end() ? 0 : static_cast<int>(it - options.begin());
    int count = static_cast<int>(options.size());
    SelectPrinter(options[static_cast<size_t>(((index + delta) % count + count) % count)]);
}

std::wstring MainWindow::EffectivePrinter() const {
    if (settings_.activePrinter != kAllPrinters && !settings_.activePrinter.empty()) return settings_.activePrinter;
    if (const auto* job = FocusedJob()) return job->printer;
    return L"";
}

// ---------------------------------------------------------------------------
// Datos de la cola
// ---------------------------------------------------------------------------

void MainWindow::OnSnapshot(core::SnapshotPtr snapshot) {
    if (!snapshot || snapshot->server != settings_.activeServer) return;  // de un servidor anterior
    snapshot_ = std::move(snapshot);
    connecting_ = false;

    ProcessEvents(tracker_.Update(*snapshot_));
    CheckPrinterProblems();
    RepauseReleasedPrinters();
    // Copias reales de cada trabajo (lectura del inicio de sus datos, una vez por trabajo).
    facts_->Request(snapshot_->jobs, settings_.activeServer, SpoolOverrideDir());

    // "Al iniciar, pausar todo": pausa las impresoras apenas se conoce la lista.
    // (Los trabajos en cola los retiene el monitor en su primer refresco.)
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
    if (settings_.activePrinter != kAllPrinters && !snapshot_->FindPrinter(settings_.activePrinter) &&
        snapshot_->error.empty())
        settings_.activePrinter = kAllPrinters;

    if (!historyView_) RebuildRows();
    UpdateHeader();
    UpdatePrinterBar();
    UpdateStatus();
    UpdatePreview();

    unsigned total = static_cast<unsigned>(snapshot_->jobs.size());
    tray_.SetTooltip(std::wstring(Tr(S::AppTitle)) + L" — " +
                     (total == 1 ? std::wstring(Tr(S::StatusOneJob)) : util::Format(Tr(S::StatusJobs), total)));
}

void MainWindow::ProcessEvents(const std::vector<core::JobEvent>& events) {
    bool historyChanged = false;
    for (const auto& e : events) {
        switch (e.kind) {
            case core::JobEvent::Kind::Added:
                // Un trabajo retenido requiere acción del operador: se avisa salvo con "ninguna".
                if (e.job.heldByApp && settings_.notifications != config::NotifyMode::None)
                    Notify(PrinterDisplay(e.job.printer),
                           util::Format(Tr(S::NotifyHeldJob), e.job.user.c_str(), e.job.document.c_str()), false);
                else if (settings_.notifications == config::NotifyMode::All)
                    Notify(Tr(S::AppTitle), util::Format(Tr(S::NotifyNewJob), e.job.user.c_str(), e.job.document.c_str()),
                           false);
                break;
            case core::JobEvent::Kind::EnteredError:
                if (settings_.notifications != config::NotifyMode::None)
                    Notify(PrinterDisplay(e.job.printer), util::Format(Tr(S::NotifyJobError), e.job.document.c_str()),
                           true);
                break;
            case core::JobEvent::Kind::Finished:
                if (settings_.keepHistory) {
                    config::HistoryEntry entry;
                    entry.timeUnix = util::ToUnixSeconds(util::NowUtc());
                    entry.server = settings_.activeServer;
                    entry.printer = e.job.printer;
                    entry.document = e.job.document;
                    entry.user = e.job.user;
                    entry.machine = e.job.machine;
                    entry.pages = std::max(e.job.totalPages, e.job.pagesPrinted);
                    entry.sizeBytes = e.job.sizeBytes;
                    entry.outcome = e.outcome;
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
        SetTimer(hwnd_, kTimerSaveHistory, 5000, nullptr);  // guarda agrupando cambios
        if (historyView_) RebuildRows();
    }
}

void MainWindow::CheckPrinterProblems() {
    if (!snapshot_) return;
    for (const auto& p : snapshot_->printers) {
        auto it = lastProblems_.find(p.name);
        // La pausa la provoca un operador: no se notifica.
        std::uint32_t relevant = p.problems & ~static_cast<std::uint32_t>(core::ProblemPaused);
        if (it != lastProblems_.end()) {
            std::uint32_t added = relevant & ~it->second;
            if (added && settings_.notifications != config::NotifyMode::None)
                Notify(PrinterDisplay(p.name), ProblemText(core::MostSevereProblem(added)),
                       added != core::ProblemTonerLow);
        }
        lastProblems_[p.name] = relevant;
    }
}

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

std::vector<core::JobInfo> MainWindow::SelectedJobs() const {
    std::vector<core::JobInfo> jobs;
    if (historyView_) return jobs;
    for (const auto& key : list_.SelectedKeys())
        if (const auto* job = FindJob(key)) jobs.push_back(*job);
    return jobs;
}

void MainWindow::RebuildRows() {
    std::vector<ui::ListRow> rows;
    if (historyView_) {
        list_.SetColumns(HistoryColumns());
        list_.SetSortIndicator(-1, false);
        const auto& entries = history_.Entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            if (settings_.activePrinter != kAllPrinters && e.printer != settings_.activePrinter) continue;
            if (!HistoryMatchesFilter(e, filter_)) continue;
            rows.push_back(MakeHistoryRow(e, i));
        }
        list_.SetEmptyText(filter_.empty() ? Tr(S::EmptyHistory) : Tr(S::EmptyFilter), L"", ui::glyph::History);
    } else {
        const bool showPrinter = settings_.activePrinter == kAllPrinters;
        list_.SetColumns(JobColumns(showPrinter));
        list_.SetSortIndicator(static_cast<int>(CurrentSort(settings_)), settings_.sortDescending);
        if (snapshot_) {
            // Los datos analizados se retienen mientras se arma la lista.
            std::map<std::wstring, std::shared_ptr<const core::ContentFacts>> held;
            FactsLookup lookup = [&](const core::JobInfo& job) -> const core::ContentFacts* {
                auto facts = facts_->Find(job);
                if (!facts) return nullptr;
                auto& slot = held[job.Key()];
                slot = std::move(facts);
                return slot.get();
            };
            for (const auto* job : VisibleJobs(*snapshot_, settings_.activePrinter, filter_,
                                               CurrentSort(settings_), settings_.sortDescending, lookup))
                rows.push_back(MakeJobRow(*job, showPrinter, lookup(*job), snapshot_->FindPrinter(job->printer)));
        }
        if (connecting_) list_.SetEmptyText(Tr(S::StatusConnecting), ServerDisplayName(), ui::glyph::Server);
        else if (snapshot_ && !snapshot_->error.empty())
            list_.SetEmptyText(Tr(S::ServerError), ServerDisplayName() + L"\n" + snapshot_->error, ui::glyph::Warning);
        else if (!filter_.empty()) list_.SetEmptyText(Tr(S::EmptyFilter), L"", ui::glyph::Search);
        else list_.SetEmptyText(Tr(S::EmptyQueueTitle), Tr(S::EmptyQueueHint), ui::glyph::Printer);
    }
    list_.SetRows(std::move(rows));
}

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
        warning = std::any_of(snapshot_->printers.begin(), snapshot_->printers.end(), [](const core::PrinterInfo& p) {
            return (p.problems & ~static_cast<std::uint32_t>(core::ProblemTonerLow)) != 0;
        });
    }
    header_.SetPrinterLabel(title, subtitle, warning);
    header_.SetHistoryActive(historyView_);
}

void MainWindow::UpdateStatus() {
    std::wstring summary;
    if (historyView_) {
        auto stats = history_.ComputeStats(util::ToUnixSeconds(util::NowUtc()));
        summary = util::Format(Tr(S::StatusHistory), static_cast<unsigned long long>(stats.pagesToday),
                               static_cast<unsigned long long>(stats.jobsToday),
                               static_cast<unsigned long long>(stats.pagesWeek));
    } else if (connecting_) {
        summary = Tr(S::StatusConnecting);
    } else if (snapshot_) {
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
    if (!transientText_.empty()) {
        wchar_t glyph = transientTone_ == ui::Tone::Danger    ? ui::glyph::Error
                        : transientTone_ == ui::Tone::Warning ? ui::glyph::Warning
                        : transientTone_ == ui::Tone::Success ? ui::glyph::Completed
                                                              : ui::glyph::Refresh;
        items.push_back({glyph, transientText_, transientTone_});
    }
    if (snapshot_) {
        // Problema de la impresora elegida (o el más grave de todas).
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
        bool noAdmin = false;
        for (const auto& [printer, admin] : adminRights_)
            if (!admin && (settings_.activePrinter == kAllPrinters || printer == settings_.activePrinter)) noAdmin = true;
        if (noAdmin) items.push_back({ui::glyph::Shield, Tr(S::StatusNoAdmin), ui::Tone::Warning});
        if (snapshot_->holdFailures > 0)
            items.push_back({ui::glyph::Warning,
                             util::Format(Tr(S::StatusAutoHoldFailed), snapshot_->holdFailures,
                                          util::Win32ErrorMessage(snapshot_->holdError).c_str()),
                             ui::Tone::Danger});
        if (snapshot_->missingJobs > 0)
            items.push_back({ui::glyph::Warning, util::Format(Tr(S::StatusMissingJobs), snapshot_->missingJobs),
                             ui::Tone::Danger});
        if (snapshot_->unreachablePrinters > 0)
            items.push_back({ui::glyph::Warning,
                             util::Format(Tr(S::StatusUnreachablePrinters), snapshot_->unreachablePrinters),
                             ui::Tone::Warning});
        if (!historyView_ && CurrentSort(settings_) != SortBy::Queue) {
            items.push_back({ui::glyph::Document,
                             util::Format(Tr(settings_.sortDescending ? S::StatusSortedByDesc : S::StatusSortedBy),
                                          SortName(CurrentSort(settings_)).c_str()),
                             ui::Tone::Info});
        }
        if (release_.Active())
            items.push_back({ui::glyph::Pause,
                             util::Format(Tr(S::StatusPrintAndRepause), static_cast<unsigned>(release_.PendingJobs())),
                             ui::Tone::Info});
        if (settings_.autoHoldNewJobs) items.push_back({ui::glyph::Pause, Tr(S::StatusAutoHold), ui::Tone::Info});
        items.push_back({snapshot_->notificationsActive ? ui::glyph::Live : ui::glyph::Refresh,
                         snapshot_->notificationsActive ? Tr(S::StatusLive) : Tr(S::StatusPolling), ui::Tone::Neutral});
    }
    status_.SetItems(std::move(items));
}

void MainWindow::UpdateTitle() {
    std::wstring title = Tr(S::AppTitle);
    if (!settings_.activeServer.empty()) title += L" — " + settings_.activeServer;
    if (IsElevated()) title += L" (admin)";
    SetWindowTextW(hwnd_, title.c_str());
}

// ---------------------------------------------------------------------------
// Vista previa
// ---------------------------------------------------------------------------

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

std::vector<std::wstring> MainWindow::JobDetails(const core::JobInfo& job, const preview::PreviewDocument* doc) const {
    std::vector<std::wstring> lines;
    std::wstring machine = job.machine;
    while (!machine.empty() && machine.front() == L'\\') machine.erase(machine.begin());
    std::wstring who = job.user;
    if (!machine.empty()) who += (who.empty() ? L"" : L" · ") + machine;
    lines.push_back(who);

    std::uint32_t pages = job.totalPages;
    if (doc) pages = std::max(pages, doc->totalPages);
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

void MainWindow::UpdatePreview() {
    if (historyView_) {
        currentPreviewKey_.clear();
        std::wstring key = list_.FocusedKey();
        const auto& entries = history_.Entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (HistoryKey(entries[i], i) != key) continue;
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

    auto request = MakePreviewRequest(*job);
    std::wstring key = request.Key();
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
    bool sameJob = currentPreviewKey_.rfind(settings_.activeServer + L"|" + job->printer + L"|" + std::to_wstring(job->id) + L"|", 0) == 0;
    currentPreviewKey_ = key;
    if (sameJob && preview_.mode() == ui::PreviewPane::Mode::Document) {
        preview_.SetInfo(job->document, JobDetails(*job, preview_.document().get()));
        preview_.SetSheet(BuildJobSheet(*job, printer, preview_.document().get()));
    } else {
        preview_.ShowLoading(job->document, JobDetails(*job, nullptr));
        preview_.SetSheet(BuildJobSheet(*job, printer, nullptr));
    }
    previews_->Request(request, true);
}

// ---------------------------------------------------------------------------
// Comandos
// ---------------------------------------------------------------------------

void MainWindow::Execute(Command command) {
    switch (command) {
        case Command::Pause: TogglePauseSelection(false); break;
        case Command::TogglePauseBoth: TogglePauseSelection(true); break;
        case Command::PrintAndRepause: PrintSelectionAndRepause(); break;
        case Command::ClearSelection: list_.ClearSelection(); break;
        case Command::Resume: RunOnSelection(JobAction::Resume); break;
        case Command::Cancel: CancelSelection(SelectedJobs()); break;
        case Command::Restart: RunOnSelection(JobAction::Restart); break;
        case Command::NavUp: list_.MoveFocus(-1, false); break;
        case Command::NavDown: list_.MoveFocus(1, false); break;
        case Command::NavFirst: list_.MoveFocus(INT_MIN, false); break;
        case Command::NavLast: list_.MoveFocus(INT_MAX, false); break;
        case Command::MoveUp: MoveFocused(-1); break;
        case Command::MoveDown: MoveFocused(1); break;
        case Command::QuickLook: ToggleQuickLook(); break;
        case Command::ToggleTheme: ToggleTheme(); break;
        case Command::Find:
            if (header_.InServerMode()) header_.EndServerInput();
            header_.FocusSearch();
            break;
        case Command::Help: help_.Open(); break;
        case Command::Refresh:
            monitor_->RequestRefresh();
            ShowTransient(Tr(S::CmdRefresh), ui::Tone::Neutral);
            break;
        case Command::Redirect: {
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
        case Command::Connect: header_.BeginServerInput(settings_.activeServer); break;
        case Command::History: ToggleHistory(); break;
        case Command::NextPage: preview_.NextPage(); break;
        case Command::PrevPage: preview_.PrevPage(); break;
        case Command::ZoomIn: preview_.ZoomIn(); break;
        case Command::ZoomOut: preview_.ZoomOut(); break;
        case Command::ZoomReset: preview_.ZoomReset(); break;
        case Command::PausePrinter: TogglePrinterPause(); break;
        case Command::ResumePrinter: SetPrintersPaused(false); break;
        case Command::SelectAll: list_.SelectAll(); break;
        case Command::Menu: {
            RECT r = header_.ButtonScreenRect(ui::HeaderButton::Menu);
            ShowMainMenu({r.left, r.bottom + Style::Get().Px(4)});
            break;
        }
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
        case Command::ToggleAutoHold:
            settings_.autoHoldNewJobs = !settings_.autoHoldNewJobs;
            monitor_->SetAutoHold(settings_.autoHoldNewJobs);
            SaveSettings();
            UpdateStatus();
            break;
        default: break;
    }
}

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
    actions_->Run(pause ? JobAction::Pause : JobAction::Resume, jobs);
    // El combinado deja la(s) impresora(s) de esos trabajos en el mismo estado.
    if (alsoPrinter) SetPrintersPaused(pause, PrintersOf(jobs));
}

void MainWindow::PrintSelectionAndRepause() {
    auto jobs = SelectedJobs();
    if (jobs.empty()) {
        ShowTransient(Tr(S::NoJobSelected), ui::Tone::Warning);
        return;
    }
    const auto count = static_cast<unsigned>(release_.Start(jobs, GetTickCount64()));
    if (count == 0) {
        ShowTransient(Tr(S::PrintAndRepauseNothing), ui::Tone::Warning);
        return;
    }
    // El mismo hilo de acciones reanuda primero los trabajos y después las
    // impresoras: así no se cuela otro trabajo de la cola entre ambos pasos.
    actions_->Run(JobAction::Resume, jobs);
    std::vector<std::wstring> paused;
    for (const auto& name : PrintersOf(jobs))
        if (const auto* p = snapshot_->FindPrinter(name); p && (p->problems & core::ProblemPaused)) paused.push_back(name);
    if (!paused.empty()) actions_->RunOnPrinters(JobAction::ResumePrinter, std::move(paused));
    util::LogInfo(L"Imprimir y volver a pausar: %u trabajo(s) liberado(s)", count);
    ShowTransient(util::Format(Tr(S::PrintAndRepauseStarted), count), ui::Tone::Success);
}

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

void MainWindow::RunOnSelection(JobAction action) {
    auto jobs = SelectedJobs();
    if (jobs.empty()) {
        ShowTransient(Tr(S::NoJobSelected), ui::Tone::Warning);
        return;
    }
    actions_->Run(action, std::move(jobs));
}

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

void MainWindow::MoveFocused(int direction) {
    const core::JobInfo* job = FocusedJob();
    if (!job) return;
    actions_->Run(direction < 0 ? JobAction::MoveUp : JobAction::MoveDown, {*job});
}

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

void MainWindow::SetPrintersPaused(bool pause, std::vector<std::wstring> printers) {
    if (printers.empty()) printers = TargetPrinters();
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

void MainWindow::StartRedirect(POINT screen) {
    const core::JobInfo* job = FocusedJob();
    if (!job || !snapshot_) {
        ShowTransient(Tr(S::NoJobSelected), ui::Tone::Warning);
        return;
    }
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
    tracker_.MarkCancelledByUser(redirectJob_.Key());
    actions_->Redirect(std::move(request));
}

void MainWindow::OnActionResult(const ActionResult& result) {
    if (result.action == JobAction::CheckAdmin) {
        for (const auto& [printer, admin] : result.adminRights) adminRights_[printer] = admin;
        UpdateStatus();
        return;
    }
    if (result.action == JobAction::Redirect && result.needsRawConfirmation) {
        std::wstring question = util::Format(Tr(S::ConfirmRedirectRaw), result.dataFormat.c_str(),
                                             result.redirect.job.driver.c_str(),
                                             PrinterDisplay(result.redirect.targetPrinter).c_str());
        if (MessageBoxW(hwnd_, question.c_str(), Tr(S::AppTitle), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
            RedirectRequest again = result.redirect;
            again.allowRaw = true;
            actions_->Redirect(std::move(again));
        }
        return;
    }
    // Las acciones sobre impresoras informan solo en la barra de estado: un
    // diálogo modal bloquearía la ventana y es habitual que falten permisos.
    const bool printerAction = result.action == JobAction::PausePrinter ||
                               result.action == JobAction::ResumePrinter || result.action == JobAction::PurgePrinter;
    if (printerAction) startPauseInFlight_ = false;
    const bool quiet = printerAction;
    if (!result.ok()) {
        ShowTransient(result.message, ui::Tone::Danger);
        if (result.firstError == ERROR_ACCESS_DENIED && !quiet)
            MessageBoxW(hwnd_, result.message.c_str(), Tr(S::AppTitle), MB_OK | MB_ICONWARNING);
    } else if (!result.message.empty()) {
        ShowTransient(result.message, ui::Tone::Success);
    }
    monitor_->RequestRefresh();
}

void MainWindow::ShowTransient(const std::wstring& text, ui::Tone tone) {
    transientText_ = text;
    transientTone_ = tone;
    UpdateStatus();
    SetTimer(hwnd_, kTimerTransient, tone == ui::Tone::Danger ? 8000 : 4000, nullptr);
}

void MainWindow::ToggleQuickLook() {
    if (historyView_) return;
    quickLook_ = !quickLook_;
    preview_.SetQuickLook(quickLook_);
    Layout();
    UpdatePreview();
}

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

void MainWindow::SetFilter(const std::wstring& filter) {
    filter_ = util::Trim(filter);
    RebuildRows();
    UpdateStatus();
}

void MainWindow::CycleSort(int delta) {
    int index = static_cast<int>(CurrentSort(settings_));
    index = ((index + delta) % kSortCount + kSortCount) % kSortCount;
    SetSort(SortById(static_cast<SortBy>(index)), settings_.sortDescending);
}

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

void MainWindow::ShowPrinterMenu(POINT screen) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (settings_.activePrinter == kAllPrinters ? MF_CHECKED : 0), ID_PRINTER_ALL,
                Tr(S::AllPrinters));
    menuPrinters_.clear();
    if (snapshot_ && !snapshot_->printers.empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        for (const auto& p : snapshot_->printers) {
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
            if (menuPrinters_.size() >= 998) break;
        }
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_PIN_CURRENT, Tr(S::MenuQuickAccess));
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (id == ID_PRINTER_ALL) SelectPrinter(kAllPrinters);
    else if (id == ID_PIN_CURRENT) ShowQuickAccessMenu(screen);
    else if (id >= ID_PRINTER_BASE && id < ID_PRINTER_BASE + menuPrinters_.size())
        SelectPrinter(menuPrinters_[id - ID_PRINTER_BASE]);
}

void MainWindow::ShowMainMenu(POINT screen) {
    HMENU menu = CreatePopupMenu();
    auto check = [](bool on) -> UINT { return on ? MF_CHECKED : MF_UNCHECKED; };
    auto keyed = [this](S text, Command command) {
        std::wstring label = Tr(text);
        auto keys = shortcuts_.Get(command);
        if (!keys.empty()) label += L"\t" + keys.front().ToString();
        return label;
    };

    AppendMenuW(menu, MF_STRING, ID_CONNECT, keyed(S::CmdConnect, Command::Connect).c_str());
    menuServers_ = settings_.servers;
    if (std::find(menuServers_.begin(), menuServers_.end(), L"") == menuServers_.end()) menuServers_.push_back(L"");
    for (size_t i = 0; i < menuServers_.size() && i < 99; ++i) {
        std::wstring label = L"    " + (menuServers_[i].empty() ? std::wstring(Tr(S::LocalServer)) : menuServers_[i]);
        AppendMenuW(menu, MF_STRING | check(util::EqualsInsensitive(menuServers_[i], settings_.activeServer)),
                    ID_SERVER_BASE + i, label.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_REFRESH, keyed(S::MenuRefresh, Command::Refresh).c_str());
    AppendMenuW(menu, MF_STRING, ID_HISTORY,
                keyed(historyView_ ? S::MenuQueue : S::MenuHistory, Command::History).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // Orden de la cola (por equipo, por hora...).
    HMENU sortMenu = CreatePopupMenu();
    for (int i = 0; i < kSortCount; ++i) {
        auto sort = static_cast<SortBy>(i);
        std::wstring label = SortName(sort);
        if (sort == SortBy::Queue) {
            auto keys = shortcuts_.Get(Command::SortCycle);
            if (!keys.empty()) label += L"	" + keys.front().ToString();
        }
        AppendMenuW(sortMenu, MF_STRING | check(sort == CurrentSort(settings_)), ID_SORT_BASE + i, label.c_str());
    }
    AppendMenuW(sortMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sortMenu, MF_STRING | check(settings_.sortDescending), ID_SORT_DESC,
                keyed(S::MenuSortDescending, Command::SortReverse).c_str());
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sortMenu), Tr(S::MenuSortBy));
    // Acceso rápido: qué impresoras aparecen en la barra superior.
    HMENU pinMenu = CreatePopupMenu();
    menuPrinters_.clear();
    if (snapshot_) {
        for (const auto& p : snapshot_->printers) {
            AppendMenuW(pinMenu, MF_STRING | check(IsPinned(settings_.favoritePrinters, p.name)),
                        ID_PIN_BASE + menuPrinters_.size(), p.DisplayName().c_str());
            menuPrinters_.push_back(p.name);
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
    AppendMenuW(menu, MF_STRING, ID_PAUSE_BOTH, keyed(S::MenuPauseBoth, Command::TogglePauseBoth).c_str());
    AppendMenuW(menu, MF_STRING, ID_PRINT_REPAUSE, keyed(S::CmdPrintAndRepause, Command::PrintAndRepause).c_str());
    AppendMenuW(menu, MF_STRING, ID_PAUSE_PRINTER, keyed(S::MenuPausePrinter, Command::PausePrinter).c_str());
    AppendMenuW(menu, MF_STRING, ID_RESUME_PRINTER, keyed(S::MenuResumePrinter, Command::ResumePrinter).c_str());
    AppendMenuW(menu, MF_STRING, ID_PURGE, Tr(S::MenuPurge));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
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
    AppendMenuW(menu, MF_STRING | check(settings_.notifications == config::NotifyMode::None), ID_NOTIFY_NONE,
                Tr(S::MenuNotifyNone));
    AppendMenuW(menu, MF_STRING | check(settings_.notifications == config::NotifyMode::ErrorsOnly), ID_NOTIFY_ERRORS,
                Tr(S::MenuNotifyErrors));
    AppendMenuW(menu, MF_STRING | check(settings_.notifications == config::NotifyMode::All), ID_NOTIFY_ALL,
                Tr(S::MenuNotifyAll));
    AppendMenuW(menu, MF_STRING | check(settings_.startWithWindows), ID_START_WITH_WINDOWS, Tr(S::MenuStartWithWindows));
    AppendMenuW(menu, MF_STRING | check(settings_.minimizeToTray), ID_MINIMIZE_TO_TRAY, Tr(S::MenuMinimizeToTray));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_SHORTCUTS, keyed(S::MenuShortcuts, Command::Help).c_str());
    AppendMenuW(menu, MF_STRING, ID_OPEN_DATA, Tr(S::MenuOpenDataFolder));
    AppendMenuW(menu, MF_STRING, ID_CLEAR_HISTORY, Tr(S::MenuClearHistory));
    if (!IsElevated()) AppendMenuW(menu, MF_STRING, ID_RUN_AS_ADMIN, Tr(S::MenuRunAsAdmin));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, Tr(S::MenuExit));

    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (id) HandleMenuCommand(id);
}

void MainWindow::ShowJobContextMenu(POINT screen) {
    if (historyView_) return;
    auto jobs = SelectedJobs();
    if (jobs.empty()) return;
    const core::JobInfo* focused = FocusedJob();
    HMENU menu = CreatePopupMenu();
    auto keyed = [this](S text, Command command) {
        std::wstring label = Tr(text);
        auto keys = shortcuts_.Get(command);
        if (!keys.empty()) label += L"\t" + keys.front().ToString();
        return label;
    };
    AppendMenuW(menu, MF_STRING, ID_CTX_PAUSE, keyed(S::CtxPause, Command::Pause).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_PAUSE_BOTH, keyed(S::CtxPauseBoth, Command::TogglePauseBoth).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_PRINT_REPAUSE,
                keyed(S::CmdPrintAndRepause, Command::PrintAndRepause).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_RESUME, keyed(S::CtxResume, Command::Resume).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_RESTART, keyed(S::CtxRestart, Command::Restart).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_CANCEL, keyed(S::CtxCancel, Command::Cancel).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_CTX_UP, keyed(S::CtxMoveUp, Command::MoveUp).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_DOWN, keyed(S::CtxMoveDown, Command::MoveDown).c_str());
    AppendMenuW(menu, MF_STRING, ID_CTX_REDIRECT, keyed(S::CtxRedirect, Command::Redirect).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_CTX_PREVIEW, keyed(S::CtxPreview, Command::QuickLook).c_str());
    if (focused && !focused->user.empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_CTX_CANCEL_USER,
                    util::Format(Tr(S::CtxCancelUser), focused->user.c_str()).c_str());
    }
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
        case ID_CTX_REDIRECT: StartRedirect(screen); break;
        case ID_CTX_PREVIEW: Execute(Command::QuickLook); break;
        case ID_CTX_CANCEL_USER:
            if (focused && snapshot_) {
                std::wstring user = focused->user;
                std::vector<core::JobInfo> userJobs;
                for (const auto* job : VisibleJobs(*snapshot_, settings_.activePrinter, L""))
                    if (util::EqualsInsensitive(job->user, user)) userJobs.push_back(*job);
                CancelSelection(userJobs);
            }
            break;
        default: break;
    }
}

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
        case ID_SORT_DESC: SetSort(settings_.sortBy, !settings_.sortDescending); break;
        case ID_PIN_CURRENT: TogglePinnedPrinter(EffectivePrinter()); break;
        case ID_SHOW_PRINTER_BAR:
            settings_.showPrinterBar = !settings_.showPrinterBar;
            SaveSettings();
            Layout();
            UpdatePrinterBar();
            break;
        case ID_CHIP_PIN: TogglePinnedPrinter(chipPrinter_); break;
        case ID_CHIP_PAUSE:
        case ID_CHIP_RESUME:
            // La barra actúa sobre su impresora (o sobre todas en el chip "Todas").
            if (chipPrinter_ == kAllPrinters) SetPrintersPaused(id == ID_CHIP_PAUSE, TargetPrinters());
            else SetPrintersPaused(id == ID_CHIP_PAUSE, {chipPrinter_});
            break;
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
        case ID_REFRESH: Execute(Command::Refresh); break;
        case ID_HISTORY: ToggleHistory(); break;
        case ID_PAUSE_BOTH: Execute(Command::TogglePauseBoth); break;
        case ID_PRINT_REPAUSE: Execute(Command::PrintAndRepause); break;
        case ID_PAUSE_PRINTER: TogglePrinterPause(); break;
        case ID_RESUME_PRINTER: SetPrintersPaused(false); break;
        case ID_PAUSE_ON_START:
            settings_.pauseAllOnStart = !settings_.pauseAllOnStart;
            monitor_->SetHoldExistingOnStart(settings_.pauseAllOnStart);
            SaveSettings();
            break;
        case ID_PURGE: RunPrinterAction(JobAction::PurgePrinter); break;
        case ID_THEME_SYSTEM:
        case ID_THEME_LIGHT:
        case ID_THEME_DARK:
            settings_.theme = id == ID_THEME_SYSTEM  ? config::ThemeMode::System
                              : id == ID_THEME_LIGHT ? config::ThemeMode::Light
                                                     : config::ThemeMode::Dark;
            ApplyTheme();
            SaveSettings();
            break;
        case ID_LANGUAGE:
            i18n::SetLanguage(i18n::GetLanguage() == i18n::Language::Spanish ? i18n::Language::English
                                                                             : i18n::Language::Spanish);
            SaveSettings();
            ApplyLanguage();
            break;
        case ID_PREVIEW_PANE: Execute(Command::TogglePane); break;
        case ID_AUTO_HOLD: Execute(Command::ToggleAutoHold); break;
        case ID_CONFIRM_CANCEL:
            settings_.confirmCancel = !settings_.confirmCancel;
            SaveSettings();
            break;
        case ID_NOTIFY_NONE: settings_.notifications = config::NotifyMode::None; SaveSettings(); break;
        case ID_NOTIFY_ERRORS: settings_.notifications = config::NotifyMode::ErrorsOnly; SaveSettings(); break;
        case ID_NOTIFY_ALL: settings_.notifications = config::NotifyMode::All; SaveSettings(); break;
        case ID_START_WITH_WINDOWS:
            if (config::SetAutostart(!settings_.startWithWindows)) settings_.startWithWindows = !settings_.startWithWindows;
            SaveSettings();
            break;
        case ID_MINIMIZE_TO_TRAY:
            settings_.minimizeToTray = !settings_.minimizeToTray;
            SaveSettings();
            break;
        case ID_SHORTCUTS: help_.Open(); break;
        case ID_OPEN_DATA:
            ShellExecuteW(hwnd_, L"open", config::DataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case ID_CLEAR_HISTORY:
            history_.Clear();
            history_.Save();
            if (historyView_) RebuildRows();
            UpdateStatus();
            break;
        case ID_RUN_AS_ADMIN: RestartAsAdmin(); break;
        case ID_EXIT: Quit(); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Ventana
// ---------------------------------------------------------------------------

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

    splitter_ = {};
    const int width = body.right - body.left;
    if (quickLook_ && !historyView_) {
        list_.Show(false);
        preview_.Show(true);
        preview_.Move(body);
        return;
    }
    list_.Show(true);
    const bool showPane = settings_.showPreviewPane && width >= style.Px(720);
    header_.SetDetailsActive(showPane);
    if (!showPane) {
        preview_.Show(false);
        list_.Move(body);
        return;
    }
    const int splitW = style.Px(6);
    int paneW = static_cast<int>(width * settings_.previewRatio);
    paneW = std::clamp(paneW, style.Px(280), std::max(style.Px(280), width - style.Px(380)));
    RECT listRect{body.left, body.top, body.right - paneW - splitW, body.bottom};
    splitter_ = {listRect.right, body.top, listRect.right + splitW, body.bottom};
    RECT paneRect{splitter_.right, body.top, body.right, body.bottom};
    list_.Move(listRect);
    preview_.Show(true);
    preview_.Move(paneRect);
    InvalidateRect(hwnd_, &splitter_, FALSE);
}

void MainWindow::ShowFromTray() {
    if (!IsWindowVisible(hwnd_)) RestoreWindowPlacement(true);
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(hwnd_);
    UpdatePreview();
}

void MainWindow::HideToTray() {
    SaveWindowPlacement();
    SaveSettings();
    ShowWindow(hwnd_, SW_HIDE);
}

void MainWindow::Quit() {
    reallyQuit_ = true;
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

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

void MainWindow::Notify(const std::wstring& title, const std::wstring& text, bool error) {
    tray_.Notify(title, text, error);
}

bool MainWindow::PreTranslate(MSG& msg) {
    if (!hwnd_ || (msg.hwnd != hwnd_ && !IsChild(hwnd_, msg.hwnd))) return false;

    if (msg.message == WM_CHAR && msg.wParam == L'?' && !header_.IsEditFocused() && !help_.IsOpen()) {
        help_.Open();
        return true;
    }
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return false;

    const UINT vk = static_cast<UINT>(msg.wParam);
    if (help_.IsOpen()) return help_.HandleKey(vk);

    const input::KeyChord chord = input::KeyChord::FromKeyEvent(vk);

    // Teclas especiales del cuadro de texto.
    if (header_.IsEditFocused()) {
        if (vk == VK_ESCAPE) {
            if (header_.InServerMode()) header_.EndServerInput();
            else if (!header_.EditText().empty()) header_.ClearSearch();
            SetFocus(list_.hwnd());
            return true;
        }
        if (vk == VK_RETURN) {
            if (header_.InServerMode()) {
                std::wstring server = header_.EditText();
                header_.EndServerInput();
                ConnectTo(server);
            }
            SetFocus(list_.hwnd());
            return true;
        }
        if (input::IsTextEditingChord(chord)) return false;
    }

    // Teclas fijas.
    if (vk == VK_ESCAPE && !chord.ctrl && !chord.alt) {
        if (quickLook_) ToggleQuickLook();
        else if (!filter_.empty()) header_.ClearSearch();
        else if (list_.HasSelection()) list_.ClearSelection();
        else if (historyView_) ToggleHistory();
        return true;
    }
    if (vk == VK_RETURN && !chord.ctrl && !chord.alt && !chord.shift) {
        ToggleQuickLook();
        return true;
    }
    // Ctrl+1..9: impresora del acceso rápido (fijas, no reasignables).
    if (chord.ctrl && !chord.alt && !chord.shift && vk >= '1' && vk <= '9' && !header_.IsEditFocused()) {
        if (SelectQuickAccess(static_cast<int>(vk - '0'))) return true;
    }
    if (chord.shift && !chord.ctrl && !chord.alt && (vk == VK_UP || vk == VK_DOWN)) {
        list_.MoveFocus(vk == VK_UP ? -1 : 1, true);
        return true;
    }
    if (chord.shift && !chord.ctrl && !chord.alt && (vk == VK_HOME || vk == VK_END)) {
        list_.MoveFocus(vk == VK_HOME ? INT_MIN : INT_MAX, true);
        return true;
    }

    if (auto command = shortcuts_.Find(chord)) {
        Execute(*command);
        return true;
    }
    return false;
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == ShowInstanceMessage()) {
        ShowFromTray();
        return 0;
    }
    if (msg == ui::TrayIcon::TaskbarCreatedMessage()) {
        tray_.Readd();
        return 0;
    }

    switch (msg) {
        case WM_APP_SNAPSHOT: {
            std::unique_ptr<core::SnapshotPtr> data(reinterpret_cast<core::SnapshotPtr*>(lParam));
            OnSnapshot(std::move(*data));
            return 0;
        }
        case WM_APP_PREVIEW: {
            std::unique_ptr<std::wstring> key(reinterpret_cast<std::wstring*>(lParam));
            if (*key == currentPreviewKey_) UpdatePreview();
            return 0;
        }
        case WM_APP_FACTS: {
            // Agrupa los avisos que llegaron seguidos: basta con reconstruir una vez.
            MSG pending;
            while (PeekMessageW(&pending, hwnd_, WM_APP_FACTS, WM_APP_FACTS, PM_REMOVE)) {
            }
            if (!historyView_) RebuildRows();
            return 0;
        }
        case WM_APP_ACTION: {
            std::unique_ptr<ActionResult> result(reinterpret_cast<ActionResult*>(lParam));
            OnActionResult(*result);
            return 0;
        }
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
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = {Style::Get().Px(560), Style::Get().Px(380)};
            return 0;
        }
        case WM_DPICHANGED: {
            Style::Get().SetDpi(HIWORD(wParam));
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            OnStyleChanged();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (lParam && std::wstring(reinterpret_cast<const wchar_t*>(lParam)) == L"ImmersiveColorSet" &&
                settings_.theme == config::ThemeMode::System)
                ApplyTheme();
            return 0;
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
        case WM_ERASEBKGND: return 1;
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
        case WM_SETFOCUS:
            SetFocus(help_.IsOpen() ? help_.hwnd() : list_.hwnd());
            return 0;
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
        case WM_QUERYENDSESSION:
            reallyQuit_ = true;
            SaveWindowPlacement();
            SaveSettings();
            history_.Save();
            return TRUE;
        case WM_DESTROY:
            tray_.Remove();
            monitor_->Stop();
            PostQuitMessage(0);
            return 0;
    }
    return Window::HandleMessage(msg, wParam, lParam);
}

}  // namespace cola::app
