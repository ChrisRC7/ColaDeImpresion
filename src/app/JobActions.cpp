// Implementación de JobActionRunner (ver JobActions.h). Todo lo que hay dentro
// de las lambdas encoladas en worker_ corre en el hilo de acciones: puede
// llamar al spooler (remoto, lento) sin congelar la UI, y nunca toca
// controles de la ventana; el resultado sale solo por done_.
#include "app/JobActions.h"

#include <algorithm>

#include "core/JobStatus.h"
#include "core/Spooler.h"
#include "i18n/Strings.h"
#include "preview/EmfSpool.h"
#include "preview/ImageLoader.h"
#include "preview/PreviewBuilder.h"
#include "preview/Reprint.h"
#include "preview/SpoolFile.h"
#include "util/Log.h"
#include "util/Strings.h"

namespace cola::app {

using i18n::S;
using i18n::Tr;
namespace spooler = core::spooler;

std::wstring ActionErrorText(DWORD error) {
    if (error == ERROR_ACCESS_DENIED) return Tr(S::ActionDenied);
    return util::Format(Tr(S::ActionFailed), util::Win32ErrorMessage(error).c_str());
}

bool RedirectComplete(std::uint32_t pagesDone, std::uint32_t pagesExpected, bool hadError) {
    if (hadError) return false;
    // pagesExpected == 0: no se sabe cuántas debían salir; solo cuenta el error.
    return pagesExpected == 0 || pagesDone >= pagesExpected;
}

JobActionRunner::JobActionRunner(DoneCallback done) : done_(std::move(done)) {}

// Hilo UI: encola. Hilo de acciones: recorre los trabajos y aplica la acción
// a cada uno por separado (un fallo no detiene a los demás).
void JobActionRunner::Run(JobAction action, std::vector<core::JobInfo> jobs) {
    // La lista se mueve dentro de la lambda: el hilo de fondo trabaja con su
    // propia copia, aunque la UI cambie la selección mientras tanto.
    worker_.Post([this, action, jobs = std::move(jobs)] {
        ActionResult result;
        result.action = action;
        for (const auto& job : jobs) {
            // La copia local de CSR no se toca: cortarla deja hojas en blanco (D50).
            if (core::IsLocalCsrCopy(job.rawStatus)) {
                ++result.skipped;
                result.failedKeys.push_back(job.Key());
                util::LogInfo(L"Acción %d sobre %ls#%u omitida: es la copia local que esta PC envía al servidor",
                              static_cast<int>(action), job.printer.c_str(), job.id);
                continue;
            }
            core::OpResult op;
            switch (action) {
                // ResumeJob/RestartJob esperan a que termine una lectura de
                // datos en curso del mismo trabajo (D39), así nunca se lee
                // mientras se imprime.
                case JobAction::Pause: op = spooler::PauseJob(job.printer, job.id); break;
                case JobAction::Resume: op = spooler::ResumeJob(job.printer, job.id); break;
                case JobAction::Cancel: op = spooler::CancelJob(job.printer, job.id); break;
                case JobAction::Restart: op = spooler::RestartJob(job.printer, job.id); break;
                // Mover = cambiar JOB_INFO_1.Position; la posición 1 es la
                // primera, así que no se baja de ahí.
                case JobAction::MoveUp:
                    op = spooler::SetJobPosition(job.printer, job.id, job.position > 1 ? job.position - 1 : 1);
                    break;
                case JobAction::MoveDown: op = spooler::SetJobPosition(job.printer, job.id, job.position + 1); break;
                // Acciones de impresora / reenvío no se piden por Run: se ignoran.
                default: continue;
            }
            if (op.ok) {
                ++result.succeeded;
            } else {
                ++result.failed;
                result.failedKeys.push_back(job.Key());
                if (result.firstError == ERROR_SUCCESS) result.firstError = op.error;
                util::LogError(L"Acción %d sobre %ls#%u falló: %lu", static_cast<int>(action), job.printer.c_str(),
                               job.id, op.error);
            }
        }
        // Mensaje: los errores tienen prioridad; si todo salió bien pero se
        // omitieron copias locales, se avisa (la UI lo muestra en amarillo).
        // Si todo salió bien sin omisiones, message queda vacío.
        if (result.failed) result.message = ActionErrorText(result.firstError);
        else if (result.skipped) result.message = util::Format(Tr(S::ActionSkippedLocalCopy), result.skipped);
        done_(std::move(result));
    });
}

// Igual que Run pero sobre impresoras completas. Requiere el permiso
// "Administrar esta impresora" (más estricto que el de documentos, D18), por
// eso los errores son frecuentes y la UI los muestra solo en la barra de estado.
void JobActionRunner::RunOnPrinters(JobAction action, std::vector<std::wstring> printers) {
    worker_.Post([this, action, printers = std::move(printers)] {
        ActionResult result;
        result.action = action;
        for (const auto& printer : printers) {
            core::OpResult op;
            switch (action) {
                case JobAction::PausePrinter: op = spooler::PausePrinter(printer); break;
                case JobAction::ResumePrinter: op = spooler::ResumePrinter(printer); break;
                case JobAction::PurgePrinter: op = spooler::PurgePrinter(printer); break;
                // Acción que no es de impresora: se abandona sin llamar a
                // done_ (no debería ocurrir; la UI no la pide así).
                default: return;
            }
            if (op.ok) {
                ++result.succeeded;
            } else {
                ++result.failed;
                result.failedPrinters.push_back(printer);
                if (result.firstError == ERROR_SUCCESS) result.firstError = op.error;
                util::LogError(L"Acción %d sobre la impresora %ls falló: %lu", static_cast<int>(action), printer.c_str(),
                               op.error);
            }
        }
        // Pausar/reanudar con éxito informan cuántas impresoras cambiaron;
        // vaciar no tiene mensaje propio (el refresco de la lista lo muestra).
        if (result.failed) {
            result.message = ActionErrorText(result.firstError);
        } else if (action == JobAction::PausePrinter) {
            result.message = util::Format(Tr(S::PrintersPaused), result.succeeded);
        } else if (action == JobAction::ResumePrinter) {
            result.message = util::Format(Tr(S::PrintersResumed), result.succeeded);
        }
        done_(std::move(result));
    });
}

// Consulta de permisos de administración por impresora (CanAdminister intenta
// abrirla con acceso de administración). Se hace en el hilo de acciones
// porque OpenPrinter contra el servidor remoto puede tardar; la UI guarda el
// resultado en MainWindow::adminRights_ (que empieza optimista en true).
void JobActionRunner::CheckAdmin(std::vector<std::wstring> printers) {
    worker_.Post([this, printers = std::move(printers)] {
        ActionResult result;
        result.action = JobAction::CheckAdmin;
        for (const auto& p : printers) result.adminRights.emplace_back(p, spooler::CanAdminister(p));
        done_(std::move(result));
    });
}

// Reenvío de un trabajo a otra impresora (D12). No existe una API del spooler
// para mover un trabajo entre colas, así que se reimprime: se leen sus datos
// y se envían al destino como un trabajo nuevo, eligiendo el método según el
// formato y el driver.
void JobActionRunner::Redirect(RedirectRequest request) {
    worker_.Post([this, request = std::move(request)] {
        ActionResult result;
        result.action = JobAction::Redirect;
        // Copia del pedido: si hace falta confirmación RAW, la UI lo reenvía
        // tal cual con allowRaw = true.
        result.redirect = request;
        const auto& job = request.job;

        // Marca el resultado como fallido con ese error y mensaje.
        auto fail = [&](DWORD error, std::wstring message) {
            result.failed = 1;
            result.firstError = error;
            result.message = std::move(message);
        };

        // La copia local de CSR no se pausa ni se lee (D39, D50): se reenvía el
        // trabajo del servidor cuando aparezca.
        if (core::IsLocalCsrCopy(job.rawStatus)) {
            result.skipped = 1;
            result.message = Tr(S::RedirectLocalCopy);
            done_(std::move(result));
            return;
        }

        // 1. Pausar el original: que no se imprima dos veces y que el spooler no
        //    lo envíe a la impresora mientras se leen sus datos (la lectura le
        //    quitaría datos). Si ya estaba en pausa se deja así al terminar.
        //    El resultado de PauseJob no se revisa: si falla, la lectura
        //    (ReadJobData) comprueba igual que el trabajo esté en pausa (D39)
        //    y no lee, así que el reenvío termina en error sin daño.
        const bool wasPaused = (job.rawStatus & JOB_STATUS_PAUSED) != 0;
        if (!wasPaused) spooler::PauseJob(job.printer, job.id);
        // Devuelve el original a su estado previo (se usa en cada salida con error).
        auto restore = [&] {
            if (!wasPaused) spooler::ResumeJob(job.printer, job.id);
        };

        // 2. Leer los datos completos del trabajo.
        //    "held" es la copia del trabajo con la marca de pausa puesta: la
        //    instantánea que trajo la UI es de antes de pausarlo, y
        //    ReadJobSpool decide si puede leer (core::CanReadJobData) según
        //    ese estado. La comprobación real contra el servidor la repite
        //    ReadJobData antes y durante la lectura.
        core::JobInfo held = job;
        held.rawStatus |= JOB_STATUS_PAUSED;
        preview::SpoolSearch search{request.server, job.printer, request.spoolOverrideDir};
        // Límite de 4 GB: en la práctica, "todo". Un trabajo cortado
        // (truncated) o incompleto no se reenvía: se imprimiría a medias (D48).
        preview::SpoolData spool = preview::ReadJobSpool(search, held, 4ull * 1024 * 1024 * 1024);
        if (spool.error != 0 || spool.truncated || spool.bytes.empty()) {
            restore();
            fail(spool.error ? spool.error : ERROR_NO_DATA,
                 spool.error == ERROR_ACCESS_DENIED ? Tr(S::PreviewAccessDenied) : Tr(S::RedirectNoData));
            done_(std::move(result));
            return;
        }
        result.dataFormat = preview::DataFormatName(spool.format);

        // Elección del método de impresión:
        //  - sameDriver: el destino usa el mismo driver, así que entiende los
        //    mismos datos RAW tal cual (copia exacta).
        //  - canRender: se puede convertir a páginas GDI (EMF nativo, imagen
        //    con WIC o PDL con un renderizador externo disponible).
        const bool sameDriver = !job.driver.empty() && util::EqualsInsensitive(job.driver, request.targetDriver);
        const bool hasRenderer = !preview::RendererFor(spool.format, request.renderers).empty();
        const bool canRender = spool.format == preview::DataFormat::EmfSpool ||
                               spool.format == preview::DataFormat::Image || hasRenderer;

        if (!sameDriver && !canRender && !request.allowRaw) {
            // No hay forma de convertir: preguntar al usuario si enviar RAW igualmente.
            // Se restaura el original mientras tanto: la pregunta es modal
            // en la UI y el usuario podría no confirmar.
            restore();
            result.needsRawConfirmation = true;
            done_(std::move(result));
            return;
        }

        preview::ReprintResult printed;
        // Solo la rama de rasterizado por lotes los usa (F4): el intérprete
        // no pudo convertir el documento completo.
        bool renderFailed = false;
        std::wstring renderError;
        std::uint32_t renderedTotal = 0;
        // Nombre del trabajo nuevo en el destino (el del original si lo tiene).
        const std::wstring docName = job.document.empty() ? L"Documento" : job.document;
        if (spool.format == preview::DataFormat::EmfSpool) {
            // EMF: reproducción vectorial de cada página con GDI.
            // PrintPagesGdi pide páginas al proveedor hasta recibir una lista
            // vacía; "delivered" hace que entregue todas de una vez y luego
            // devuelva vacío para terminar.
            bool delivered = false;
            printed = preview::PrintPagesGdi(request.targetPrinter, docName, [&]() {
                std::vector<preview::PagePtr> pages;
                if (delivered) return pages;
                delivered = true;
                for (auto& bytes : preview::ExtractEmfPages(spool.bytes.data(), spool.bytes.size(), SIZE_MAX))
                    pages.push_back(std::make_shared<preview::EmfPage>(std::move(bytes)));
                return pages;
            });
        } else if (sameDriver || (request.allowRaw && !canRender)) {
            // Copia RAW: los datos del driver van tal cual. El XPS se envía
            // como XPS_PASS para que el spooler no lo vuelva a procesar.
            // SendRawData no sabe cuántas páginas hay: se informa TotalPages
            // del original (al menos 1) en el mensaje.
            const wchar_t* datatype = spool.format == preview::DataFormat::Xps ? L"XPS_PASS" : L"RAW";
            printed = preview::SendRawData(request.targetPrinter, docName, spool.bytes.data(), spool.bytes.size(), datatype);
            printed.pages = std::max(job.totalPages, 1u);
        } else if (spool.format == preview::DataFormat::Image) {
            // Imagen (JPEG/PNG/TIFF...): se decodifica con WIC y se imprime
            // como página bitmap, todo en una entrega (como en EMF).
            bool delivered = false;
            printed = preview::PrintPagesGdi(request.targetPrinter, docName, [&]() {
                std::vector<preview::PagePtr> pages;
                if (delivered) return pages;
                delivered = true;
                return preview::LoadImageMemory(spool.bytes.data(), spool.bytes.size(), 1000, 20000);
            });
        } else {
            // Rasteriza por lotes a 300 DPI con Ghostscript/GhostPCL.
            // Por lotes de 6 páginas para no tener en memoria todos los
            // bitmaps de un documento largo (a 300 DPI una carta son ~25 MB).
            // Un lote con menos páginas que kBatch indica que se llegó al
            // final; la siguiente llamada devuelve vacío y termina la
            // impresión. 300000 ms es el tiempo máximo de cada ejecución externa.
            //
            // F4: un lote corto también puede ser un fallo o un tiempo agotado
            // del intérprete a mitad del documento. Antes eso se tomaba por el
            // final, el reenvío se daba por bueno y se cancelaba el original
            // (el estudiante perdía el resto). Ahora, si un lote falla o el
            // documento termina con menos páginas de las que declaran sus
            // datos, se aborta el trabajo del destino y el original se restaura.
            constexpr int kBatch = 6;
            // Páginas que declaran los datos (0 = no se sabe): solo conteos
            // hechos sobre el contenido completo (DSC de PostScript, FixedPage
            // de XPS); TotalPages del spooler no sirve aquí (GUIA_IA §6).
            std::uint32_t expectedPages = 0;
            {
                core::ContentFacts facts;
                preview::AnalyzeSpoolData(spool, facts);
                const bool counted = facts.pageSource == core::ContentFacts::PageSource::PostScriptDsc ||
                                     facts.pageSource == core::ContentFacts::PageSource::XpsFixedPages;
                if (facts.complete && counted) expectedPages = facts.pages;
            }
            int next = 1;
            bool finished = false;
            std::uint32_t renderedPages = 0;
            printed = preview::PrintPagesGdi(
                request.targetPrinter, docName,
                [&]() {
                    std::vector<preview::PagePtr> pages;
                    if (finished || renderFailed) return pages;
                    auto rendered = preview::RenderExternal(spool.format, spool.bytes.data(), spool.bytes.size(),
                                                            request.renderers, 300, next, next + kBatch - 1, 300000);
                    renderedPages += static_cast<std::uint32_t>(rendered.pages.size());
                    if (static_cast<int>(rendered.pages.size()) < kBatch ||
                        (expectedPages != 0 && renderedPages >= expectedPages))
                        finished = true;
                    next += kBatch;
                    // Lote fallido, o fin del documento con páginas de menos:
                    // no se entrega nada más y PrintPagesGdi aborta.
                    if (!RedirectComplete(renderedPages, finished ? expectedPages : 0, rendered.failed)) {
                        renderFailed = true;
                        renderError = rendered.failed ? rendered.error : util::Format(L"%u < %u", renderedPages, expectedPages);
                        util::LogError(L"Reenvío de %ls#%u abortado: %u página(s) convertidas de %u esperadas (%ls)",
                                       job.printer.c_str(), job.id, renderedPages, expectedPages, renderError.c_str());
                        pages.clear();
                        return pages;
                    }
                    return rendered.pages;
                },
                [&]() { return renderFailed; });
            renderedTotal = renderedPages;
        }

        if (renderFailed) {
            // El documento no se pudo convertir completo: nada quedó en el
            // destino (AbortDoc) y el original vuelve a su estado anterior.
            restore();
            fail(printed.error ? printed.error : ERROR_CANCELLED,
                 util::Format(Tr(S::RedirectIncomplete), renderedTotal, renderError.c_str()));
        } else if (!printed.ok) {
            restore();  // deja el original como estaba
            fail(printed.error, ActionErrorText(printed.error));
        } else {
            // 3. Eliminar el original: ya está en la otra impresora.
            //    (Si la cancelación falla, el original queda en pausa: no se
            //    imprime dos veces y el operador puede borrarlo a mano. Se
            //    avisa en failedKeys para que el historial no lo dé por
            //    cancelado si después se libera, F3.)
            if (!spooler::CancelJob(job.printer, job.id).ok) result.failedKeys.push_back(job.Key());
            result.succeeded = 1;
            // PrinterInfo temporal solo para obtener el nombre visible del
            // destino (sin el prefijo \\SERVIDOR\).
            core::PrinterInfo target;
            target.name = request.targetPrinter;
            result.message = util::Format(Tr(S::RedirectDone), target.DisplayName().c_str(), printed.pages);
        }
        done_(std::move(result));
    });
}

}  // namespace cola::app
