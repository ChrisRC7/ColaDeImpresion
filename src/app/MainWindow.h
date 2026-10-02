#pragma once
// Ventana principal: une los módulos (monitor de cola, vistas previas,
// acciones, historial, configuración) con los controles de la interfaz.
// Toda la lógica de "qué pasa cuando..." vive aquí; los controles solo dibujan
// y avisan por callbacks, y los módulos core/preview no conocen la UI.

#include <windows.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/JobActions.h"
#include "config/History.h"
#include "config/Settings.h"
#include "core/JobTracker.h"
#include "core/QueueMonitor.h"
#include "input/Shortcuts.h"
#include "preview/JobFacts.h"
#include "preview/PreviewService.h"
#include "ui/HeaderBar.h"
#include "ui/HelpOverlay.h"
#include "ui/JobListView.h"
#include "ui/PreviewPane.h"
#include "ui/PrinterBar.h"
#include "ui/StatusBar.h"
#include "ui/TrayIcon.h"
#include "ui/Window.h"

namespace cola::app {

struct StartupOptions {
    bool startInTray = false;
    std::wstring server;  // --server
    bool hasServer = false;
};

class MainWindow : public ui::Window {
public:
    static constexpr wchar_t kClassName[] = L"ColaDeImpresionMainWindow";
    static UINT ShowInstanceMessage();  // mensaje registrado para traer la ventana al frente

    MainWindow();
    ~MainWindow() override;

    bool Create(const StartupOptions& options);

    // Procesa atajos antes de que el mensaje llegue al control con foco.
    bool PreTranslate(MSG& msg);

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    // --- Configuración / estado general ---
    void ApplyTheme();
    void ApplyLanguage();
    void OnStyleChanged();
    void SaveSettings();
    void SaveWindowPlacement();
    void RestoreWindowPlacement(bool show);

    // --- Servidor / impresoras ---
    void ConnectTo(const std::wstring& server);
    std::wstring ServerDisplayName() const;
    void SelectPrinter(const std::wstring& printer);
    void CyclePrinter(int delta);
    // Acceso rápido: barra superior con las impresoras fijadas por el operador.
    void UpdatePrinterBar();
    void TogglePinnedPrinter(const std::wstring& printer);
    void ShowQuickAccessMenu(POINT screen);
    void ShowPrinterChipMenu(const std::wstring& printer, POINT screen);
    // Ctrl+1..9: elige la n-ésima impresora del acceso rápido.
    bool SelectQuickAccess(int index);
    std::wstring EffectivePrinter() const;  // impresora para acciones de impresora

    // --- Datos ---
    void OnSnapshot(core::SnapshotPtr snapshot);
    void ProcessEvents(const std::vector<core::JobEvent>& events);
    void CheckPrinterProblems();
    void RebuildRows();
    void UpdateHeader();
    void UpdateStatus();
    void UpdateTitle();
    const core::JobInfo* FindJob(const std::wstring& key) const;
    std::vector<core::JobInfo> SelectedJobs() const;
    const core::JobInfo* FocusedJob() const;

    // --- Vista previa ---
    void UpdatePreview();
    preview::PreviewRequest MakePreviewRequest(const core::JobInfo& job) const;
    std::vector<std::wstring> JobDetails(const core::JobInfo& job, const preview::PreviewDocument* doc) const;

    // --- Comandos ---
    void Execute(input::Command command);
    void RunOnSelection(JobAction action);
    // Pausa o reanuda los trabajos seleccionados (y opcionalmente la impresora).
    void TogglePauseSelection(bool alsoPrinter);
    void CancelSelection(const std::vector<core::JobInfo>& jobs);
    void MoveFocused(int direction);
    void StartRedirect(POINT screen);
    void RunPrinterAction(JobAction action);
    // Impresoras afectadas por pausar/reanudar: la elegida, o todas en la vista "Todas".
    std::vector<std::wstring> TargetPrinters() const;
    void TogglePrinterPause();
    // printers vacío = las impresoras de TargetPrinters().
    void SetPrintersPaused(bool pause, std::vector<std::wstring> printers = {});
    void OnActionResult(const ActionResult& result);
    void ToggleQuickLook();
    void ToggleHistory();
    void ToggleTheme();
    void SetFilter(const std::wstring& filter);
    // Orden de la cola (equipo, hora, usuario...). delta rota entre criterios.
    void CycleSort(int delta);
    void SetSort(const std::wstring& sortId, bool descending, bool announce = true);
    // Clic en el encabezado de una columna de la lista.
    void OnColumnClicked(int sortId);
    // Carpeta de spool alternativa configurada para el servidor activo.
    std::wstring SpoolOverrideDir() const;
    void ShowTransient(const std::wstring& text, ui::Tone tone);

    // --- Menús ---
    void ShowPrinterMenu(POINT screen);
    void ShowMainMenu(POINT screen);
    void ShowJobContextMenu(POINT screen);
    void ShowTrayMenu();
    void HandleMenuCommand(UINT id);

    // --- Ventana ---
    void Layout();
    void ShowFromTray();
    void HideToTray();
    void Quit();
    void RestartAsAdmin();
    void Notify(const std::wstring& title, const std::wstring& text, bool error);

    // Módulos
    config::Settings settings_;
    config::History history_;
    input::ShortcutMap shortcuts_;
    preview::RendererPaths renderers_;
    std::unique_ptr<core::QueueMonitor> monitor_;
    std::unique_ptr<preview::PreviewService> previews_;
    // Copias y páginas reales de todos los trabajos (para la columna "Páginas").
    std::unique_ptr<preview::JobFactsService> facts_;
    std::unique_ptr<JobActionRunner> actions_;
    core::JobTracker tracker_;

    // Controles
    ui::HeaderBar header_;
    ui::PrinterBar printerBar_;
    ui::JobListView list_;
    ui::PreviewPane preview_;
    ui::StatusBar status_;
    ui::HelpOverlay help_;
    ui::TrayIcon tray_;
    HICON icon_ = nullptr;
    HICON iconSmall_ = nullptr;

    // Estado
    core::SnapshotPtr snapshot_;
    std::wstring filter_;
    bool historyView_ = false;
    bool quickLook_ = false;
    bool reallyQuit_ = false;
    bool connecting_ = true;
    bool startPausePending_ = false;  // pausar impresoras al recibir la primera instantánea
    bool startPauseInFlight_ = false; // su resultado se informa sin diálogos
    std::wstring currentPreviewKey_;
    std::map<std::wstring, bool> adminRights_;           // impresora -> puede administrar
    std::map<std::wstring, std::uint32_t> lastProblems_;  // para notificar problemas nuevos
    std::wstring transientText_;
    ui::Tone transientTone_ = ui::Tone::Neutral;
    RECT splitter_{};
    bool draggingSplitter_ = false;
    std::vector<std::wstring> menuPrinters_;  // impresoras listadas en el último menú
    std::wstring chipPrinter_;                // impresora del menú contextual de la barra
    std::vector<std::wstring> menuServers_;
    core::JobInfo redirectJob_;
};

}  // namespace cola::app
