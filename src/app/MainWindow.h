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
#include "preview/PreviewService.h"
#include "ui/HeaderBar.h"
#include "ui/HelpOverlay.h"
#include "ui/JobListView.h"
#include "ui/PreviewPane.h"
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
    void CancelSelection(const std::vector<core::JobInfo>& jobs);
    void MoveFocused(int direction);
    void StartRedirect(POINT screen);
    void RunPrinterAction(JobAction action);
    void OnActionResult(const ActionResult& result);
    void ToggleQuickLook();
    void ToggleHistory();
    void ToggleTheme();
    void SetFilter(const std::wstring& filter);
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
    std::unique_ptr<JobActionRunner> actions_;
    core::JobTracker tracker_;

    // Controles
    ui::HeaderBar header_;
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
    std::wstring currentPreviewKey_;
    std::map<std::wstring, bool> adminRights_;           // impresora -> puede administrar
    std::map<std::wstring, std::uint32_t> lastProblems_;  // para notificar problemas nuevos
    std::wstring transientText_;
    ui::Tone transientTone_ = ui::Tone::Neutral;
    RECT splitter_{};
    bool draggingSplitter_ = false;
    std::vector<std::wstring> menuPrinters_;  // impresoras listadas en el último menú
    std::vector<std::wstring> menuServers_;
    core::JobInfo redirectJob_;
};

}  // namespace cola::app
