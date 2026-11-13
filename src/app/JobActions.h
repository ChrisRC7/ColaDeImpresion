#pragma once
// Ejecuta las acciones sobre trabajos e impresoras en un hilo de fondo (las
// llamadas al spooler remoto pueden tardar) y devuelve el resultado mediante
// un callback que la ventana reenvía al hilo de la UI.
//
// Flujo: MainWindow (hilo UI) llama a Run / RunOnPrinters / Redirect /
// CheckAdmin, que solo encolan una tarea en worker_ y vuelven enseguida. La
// tarea corre en el hilo de acciones (un único hilo, así las acciones se
// ejecutan en el orden en que se pidieron, p. ej. F11 = reanudar trabajos y
// después la impresora, D35) y al terminar llama a done_ con un ActionResult.
// MainWindow, en ese callback, lo envía a la UI con PostOwned
// (WM_APP_ACTION_RESULT) y lo procesa en OnActionResult.
//
// Regla D50: ninguna acción toca la copia local de Client Side Rendering
// (core::IsLocalCsrCopy); se cuenta como "omitida" (ActionResult::skipped).

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/PrintTypes.h"
#include "preview/ExternalRenderer.h"
#include "util/WorkQueue.h"

namespace cola::app {

// Tipo de acción que se pide al hilo de acciones. Viaja de vuelta en
// ActionResult::action para que OnActionResult sepa qué mensaje mostrar y
// qué refrescar.
enum class JobAction {
    Pause,          // pausar trabajos (SetJob JOB_CONTROL_PAUSE)
    Resume,         // reanudar trabajos (también lo usa F11, D35)
    Cancel,         // cancelar/eliminar trabajos
    Restart,        // reiniciar trabajos desde el principio
    MoveUp,         // subir una posición en la cola real del spooler
    MoveDown,       // bajar una posición en la cola real del spooler
    Redirect,       // reenviar un trabajo a otra impresora (D12)
    PausePrinter,   // pausar impresoras completas (SetPrinter, D18)
    ResumePrinter,  // reanudar impresoras completas
    PurgePrinter,   // vaciar la cola de una impresora (cancela todos sus trabajos)
    CheckAdmin,     // averiguar si se tienen permisos de administración por impresora
};

// Datos necesarios para reenviar un trabajo a otra impresora. Se arma en la
// UI (MainWindow) y se copia al hilo de acciones. También vuelve en
// ActionResult::redirect para poder repetir el pedido con allowRaw = true
// si el usuario confirma enviar RAW.
struct RedirectRequest {
    std::wstring server;            // servidor conectado ("" = este equipo); para buscar el spool
    core::JobInfo job;              // trabajo original (se pausa, se lee y se cancela al terminar)
    std::wstring targetPrinter;     // nombre completo de la impresora destino
    std::wstring targetDriver;      // driver del destino: si coincide con el del original se copia RAW
    std::wstring spoolOverrideDir;  // carpeta de spool alternativa configurada (spoolDirOverrides), o ""
    preview::RendererPaths renderers;  // rutas de Ghostscript/GhostPCL/GhostXPS para rasterizar
    bool allowRaw = false;  // el usuario confirmó enviar RAW a un driver distinto
};

// Resultado de una acción, armado en el hilo de acciones y procesado en la UI
// (MainWindow::OnActionResult). Se mueve por valor; MainWindow lo envuelve en
// un puntero propio para pasarlo por PostMessage.
struct ActionResult {
    JobAction action = JobAction::Pause;  // qué acción se ejecutó
    unsigned succeeded = 0;               // trabajos/impresoras en que funcionó
    unsigned failed = 0;                  // trabajos/impresoras en que falló
    unsigned skipped = 0;  // copias locales de CSR que no se tocaron (core::IsLocalCsrCopy)
    DWORD firstError = ERROR_SUCCESS;  // primer código Win32 de error (para el mensaje)
    std::wstring message;  // mensaje listo para mostrar (vacío si no hace falta)
    // Run: claves (JobInfo::Key) de los trabajos en los que la acción NO se
    // aplicó (falló o se omitió por ser copia local de CSR). MainWindow las
    // usa para quitar la marca "cancelado por el operador" (F3).
    std::vector<std::wstring> failedKeys;
    // RunOnPrinters: impresoras en las que la acción falló (mismo motivo, al
    // vaciar una cola).
    std::vector<std::wstring> failedPrinters;

    // Reenvío: necesita confirmación para enviar RAW a otro driver.
    bool needsRawConfirmation = false;
    std::wstring dataFormat;   // nombre del formato de los datos (para la pregunta al usuario)
    RedirectRequest redirect;  // copia del pedido original (para reintentarlo con allowRaw)

    // CheckAdmin: impresora -> tiene permisos de administración.
    std::vector<std::pair<std::wstring, bool>> adminRights;

    // true si no hubo fallos ni queda una confirmación pendiente. Las
    // omisiones (skipped) no cuentan como fallo.
    bool ok() const { return failed == 0 && !needsRawConfirmation; }
};

// Ejecutor de acciones en un hilo de fondo propio (util::WorkQueue). Todos los
// métodos públicos se llaman desde el hilo de la UI, copian sus argumentos y
// vuelven de inmediato; el trabajo real y la llamada a done_ ocurren en el
// hilo de acciones. Al destruirse, la WorkQueue termina la tarea en curso y
// une el hilo (por eso MainWindow lo destruye antes que la ventana).
class JobActionRunner {
public:
    using DoneCallback = std::function<void(ActionResult)>;  // se llama en el hilo de fondo

    // done: callback que recibe cada resultado (en el hilo de acciones).
    explicit JobActionRunner(DoneCallback done);

    // Pausa/reanuda/cancela/reinicia/mueve cada trabajo de la lista (acciones
    // Pause..MoveDown). Omite las copias locales de CSR (D50). Produce un único
    // ActionResult con los totales y el texto del primer error.
    void Run(JobAction action, std::vector<core::JobInfo> jobs);
    // Pausar / reanudar / vaciar una o varias impresoras.
    // action: PausePrinter, ResumePrinter o PurgePrinter; printers: nombres
    // completos (\\SERVIDOR\Cola). Un único ActionResult con los totales.
    void RunOnPrinters(JobAction action, std::vector<std::wstring> printers);
    // Reenvía un trabajo a otra impresora (D12): pausa el original, lee sus
    // datos, los imprime en el destino (EMF por GDI, mismo driver en RAW,
    // imagen por GDI o rasterizado con Ghostscript) y cancela el original si
    // todo salió bien; si falla, lo deja como estaba. Si no hay forma de
    // convertir y no se permitió RAW, devuelve needsRawConfirmation.
    void Redirect(RedirectRequest request);
    // Consulta, para cada impresora, si el usuario tiene permiso de
    // administrarla (spooler::CanAdminister). Resultado en adminRights.
    void CheckAdmin(std::vector<std::wstring> printers);

private:
    DoneCallback done_;  // destino de los resultados (lo fija MainWindow)
    // Hilo de acciones. El "true" es initCom: el hilo inicializa COM, que
    // necesitan el reenvío (WIC para imágenes, XPS) y el Shell.
    util::WorkQueue worker_{true};
};

// Decide si un reenvío rasterizado por lotes puede darse por completo (F4);
// solo entonces se cancela el original. Función pura (se prueba en tests/).
//  pagesDone:     páginas convertidas hasta ahora.
//  pagesExpected: páginas que declaran los datos del trabajo; 0 = no se sabe
//                 (o todavía no se llegó al final: no se compara).
//  hadError:      el intérprete falló o se le agotó el tiempo en algún lote.
// false si hubo error, o si se esperaban más páginas de las que salieron.
bool RedirectComplete(std::uint32_t pagesDone, std::uint32_t pagesExpected, bool hadError);

// Texto de error amigable para un código Win32 de una acción.
// ERROR_ACCESS_DENIED tiene un texto propio (falta de permisos, D15); el
// resto se formatea con el mensaje del sistema. Se puede llamar desde
// cualquier hilo (solo lee la tabla de textos).
std::wstring ActionErrorText(DWORD error);

}  // namespace cola::app
