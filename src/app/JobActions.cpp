#include "app/JobActions.h"

#include <algorithm>

#include "core/JobStatus.h"
#include "core/Spooler.h"
#include "i18n/Strings.h"
#include "preview/EmfSpool.h"
#include "preview/ImageLoader.h"
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

JobActionRunner::JobActionRunner(DoneCallback done) : done_(std::move(done)) {}

void JobActionRunner::Run(JobAction action, std::vector<core::JobInfo> jobs) {
    worker_.Post([this, action, jobs = std::move(jobs)] {
        ActionResult result;
        result.action = action;
        for (const auto& job : jobs) {
            // La copia local de CSR no se toca: cortarla deja hojas en blanco (D50).
            if (core::IsLocalCsrCopy(job.rawStatus)) {
                ++result.skipped;
                util::LogInfo(L"Acción %d sobre %ls#%u omitida: es la copia local que esta PC envía al servidor",
                              static_cast<int>(action), job.printer.c_str(), job.id);
                continue;
            }
            core::OpResult op;
            switch (action) {
                case JobAction::Pause: op = spooler::PauseJob(job.printer, job.id); break;
                case JobAction::Resume: op = spooler::ResumeJob(job.printer, job.id); break;
                case JobAction::Cancel: op = spooler::CancelJob(job.printer, job.id); break;
                case JobAction::Restart: op = spooler::RestartJob(job.printer, job.id); break;
                case JobAction::MoveUp:
                    op = spooler::SetJobPosition(job.printer, job.id, job.position > 1 ? job.position - 1 : 1);
                    break;
                case JobAction::MoveDown: op = spooler::SetJobPosition(job.printer, job.id, job.position + 1); break;
                default: continue;
            }
            if (op.ok) {
                ++result.succeeded;
            } else {
                ++result.failed;
                if (result.firstError == ERROR_SUCCESS) result.firstError = op.error;
                util::LogError(L"Acción %d sobre %ls#%u falló: %lu", static_cast<int>(action), job.printer.c_str(),
                               job.id, op.error);
            }
        }
        if (result.failed) result.message = ActionErrorText(result.firstError);
        else if (result.skipped) result.message = util::Format(Tr(S::ActionSkippedLocalCopy), result.skipped);
        done_(std::move(result));
    });
}

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
                default: return;
            }
            if (op.ok) {
                ++result.succeeded;
            } else {
                ++result.failed;
                if (result.firstError == ERROR_SUCCESS) result.firstError = op.error;
                util::LogError(L"Acción %d sobre la impresora %ls falló: %lu", static_cast<int>(action), printer.c_str(),
                               op.error);
            }
        }
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

void JobActionRunner::CheckAdmin(std::vector<std::wstring> printers) {
    worker_.Post([this, printers = std::move(printers)] {
        ActionResult result;
        result.action = JobAction::CheckAdmin;
        for (const auto& p : printers) result.adminRights.emplace_back(p, spooler::CanAdminister(p));
        done_(std::move(result));
    });
}

void JobActionRunner::Redirect(RedirectRequest request) {
    worker_.Post([this, request = std::move(request)] {
        ActionResult result;
        result.action = JobAction::Redirect;
        result.redirect = request;
        const auto& job = request.job;

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
        const bool wasPaused = (job.rawStatus & JOB_STATUS_PAUSED) != 0;
        if (!wasPaused) spooler::PauseJob(job.printer, job.id);
        auto restore = [&] {
            if (!wasPaused) spooler::ResumeJob(job.printer, job.id);
        };

        // 2. Leer los datos completos del trabajo.
        core::JobInfo held = job;
        held.rawStatus |= JOB_STATUS_PAUSED;
        preview::SpoolSearch search{request.server, job.printer, request.spoolOverrideDir};
        preview::SpoolData spool = preview::ReadJobSpool(search, held, 4ull * 1024 * 1024 * 1024);
        if (spool.error != 0 || spool.truncated || spool.bytes.empty()) {
            restore();
            fail(spool.error ? spool.error : ERROR_NO_DATA,
                 spool.error == ERROR_ACCESS_DENIED ? Tr(S::PreviewAccessDenied) : Tr(S::RedirectNoData));
            done_(std::move(result));
            return;
        }
        result.dataFormat = preview::DataFormatName(spool.format);

        const bool sameDriver = !job.driver.empty() && util::EqualsInsensitive(job.driver, request.targetDriver);
        const bool hasRenderer = !preview::RendererFor(spool.format, request.renderers).empty();
        const bool canRender = spool.format == preview::DataFormat::EmfSpool ||
                               spool.format == preview::DataFormat::Image || hasRenderer;

        if (!sameDriver && !canRender && !request.allowRaw) {
            // No hay forma de convertir: preguntar al usuario si enviar RAW igualmente.
            restore();
            result.needsRawConfirmation = true;
            done_(std::move(result));
            return;
        }

        preview::ReprintResult printed;
        const std::wstring docName = job.document.empty() ? L"Documento" : job.document;
        if (spool.format == preview::DataFormat::EmfSpool) {
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
            const wchar_t* datatype = spool.format == preview::DataFormat::Xps ? L"XPS_PASS" : L"RAW";
            printed = preview::SendRawData(request.targetPrinter, docName, spool.bytes.data(), spool.bytes.size(), datatype);
            printed.pages = std::max(job.totalPages, 1u);
        } else if (spool.format == preview::DataFormat::Image) {
            bool delivered = false;
            printed = preview::PrintPagesGdi(request.targetPrinter, docName, [&]() {
                std::vector<preview::PagePtr> pages;
                if (delivered) return pages;
                delivered = true;
                return preview::LoadImageMemory(spool.bytes.data(), spool.bytes.size(), 1000, 20000);
            });
        } else {
            // Rasteriza por lotes a 300 DPI con Ghostscript/GhostPCL.
            constexpr int kBatch = 6;
            int next = 1;
            bool finished = false;
            printed = preview::PrintPagesGdi(request.targetPrinter, docName, [&]() {
                std::vector<preview::PagePtr> pages;
                if (finished) return pages;
                auto rendered = preview::RenderExternal(spool.format, spool.bytes.data(), spool.bytes.size(),
                                                        request.renderers, 300, next, next + kBatch - 1, 300000);
                if (static_cast<int>(rendered.pages.size()) < kBatch) finished = true;
                next += kBatch;
                return rendered.pages;
            });
        }

        if (!printed.ok) {
            restore();  // deja el original como estaba
            fail(printed.error, ActionErrorText(printed.error));
        } else {
            // 3. Eliminar el original: ya está en la otra impresora.
            spooler::CancelJob(job.printer, job.id);
            result.succeeded = 1;
            core::PrinterInfo target;
            target.name = request.targetPrinter;
            result.message = util::Format(Tr(S::RedirectDone), target.DisplayName().c_str(), printed.pages);
        }
        done_(std::move(result));
    });
}

}  // namespace cola::app
