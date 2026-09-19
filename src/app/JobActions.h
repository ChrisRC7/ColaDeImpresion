#pragma once
// Ejecuta las acciones sobre trabajos e impresoras en un hilo de fondo (las
// llamadas al spooler remoto pueden tardar) y devuelve el resultado mediante
// un callback que la ventana reenvía al hilo de la UI.

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/PrintTypes.h"
#include "preview/ExternalRenderer.h"
#include "util/WorkQueue.h"

namespace cola::app {

enum class JobAction {
    Pause,
    Resume,
    Cancel,
    Restart,
    MoveUp,
    MoveDown,
    Redirect,
    PausePrinter,
    ResumePrinter,
    PurgePrinter,
    CheckAdmin,
};

struct RedirectRequest {
    std::wstring server;
    core::JobInfo job;
    std::wstring targetPrinter;
    std::wstring targetDriver;
    std::wstring spoolOverrideDir;
    preview::RendererPaths renderers;
    bool allowRaw = false;  // el usuario confirmó enviar RAW a un driver distinto
};

struct ActionResult {
    JobAction action = JobAction::Pause;
    unsigned succeeded = 0;
    unsigned failed = 0;
    DWORD firstError = ERROR_SUCCESS;
    std::wstring message;  // mensaje listo para mostrar (vacío si no hace falta)

    // Reenvío: necesita confirmación para enviar RAW a otro driver.
    bool needsRawConfirmation = false;
    std::wstring dataFormat;
    RedirectRequest redirect;

    // CheckAdmin: impresora -> tiene permisos de administración.
    std::vector<std::pair<std::wstring, bool>> adminRights;

    bool ok() const { return failed == 0 && !needsRawConfirmation; }
};

class JobActionRunner {
public:
    using DoneCallback = std::function<void(ActionResult)>;  // se llama en el hilo de fondo

    explicit JobActionRunner(DoneCallback done);

    void Run(JobAction action, std::vector<core::JobInfo> jobs);
    // Pausar / reanudar / vaciar una o varias impresoras.
    void RunOnPrinters(JobAction action, std::vector<std::wstring> printers);
    void Redirect(RedirectRequest request);
    void CheckAdmin(std::vector<std::wstring> printers);

private:
    DoneCallback done_;
    util::WorkQueue worker_{true};
};

// Texto de error amigable para un código Win32 de una acción.
std::wstring ActionErrorText(DWORD error);

}  // namespace cola::app
