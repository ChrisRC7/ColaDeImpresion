#include "preview/PreviewBuilder.h"

#include <windows.h>
#include <winspool.h>

#include <algorithm>

#include "i18n/Strings.h"
#include "preview/EmfSpool.h"
#include "preview/ImageLoader.h"
#include "preview/PdlSettings.h"
#include "preview/SpoolFile.h"
#include "preview/TempFiles.h"
#include "preview/XpsPackage.h"
#include "preview/ZipReader.h"
#include "util/Log.h"
#include "util/Strings.h"

namespace cola::preview {

using i18n::S;
using i18n::Tr;

std::wstring PreviewRequest::Key() const {
    // El estado "recibiendo" forma parte de la clave: al terminar la recepción
    // se vuelve a generar (antes no se leen los datos).
    const unsigned spooling = (job.rawStatus & JOB_STATUS_SPOOLING) ? 1u : 0u;
    return util::Format(L"%ls|%ls|%u|%llu|%u|%u", server.c_str(), job.printer.c_str(), job.id,
                        static_cast<unsigned long long>(job.sizeBytes), job.totalPages, spooling);
}

namespace {

// Lista de vías probadas, acortada para que quepa en el panel.
std::wstring ShortDiagnostics(const std::wstring& diagnostics) {
    std::wstring out;
    for (const auto& line : util::Split(diagnostics, L'\n')) {
        if (line.empty()) continue;
        if (!out.empty()) out += L"\n";
        out += util::EllipsisMiddle(line, 90);
    }
    return out;
}

bool LooksLikePath(const std::wstring& text) {
    return (text.size() > 3 && text[1] == L':' && text[2] == L'\\') || text.rfind(L"\\\\", 0) == 0;
}

core::DataKind KindOf(DataFormat format) {
    switch (format) {
        case DataFormat::EmfSpool: return core::DataKind::Emf;
        case DataFormat::Xps: return core::DataKind::Xps;
        case DataFormat::Unknown: return core::DataKind::Unknown;
        default: return core::DataKind::Pdl;  // PCL, PS, PDF, ESC/Page, texto... ya generados para la impresora
    }
}

}  // namespace

void AnalyzeSpoolData(const SpoolData& spool, core::ContentFacts& facts) {
    facts.analyzed = true;
    facts.complete = !spool.truncated;
    facts.kind = KindOf(spool.format);
    facts.language = DataFormatName(spool.format);
    const std::uint8_t* data = spool.bytes.data();
    const size_t size = spool.bytes.size();
    switch (spool.format) {
        case DataFormat::EmfSpool: {
            size_t found = 0;
            std::vector<std::uint64_t> fingerprints;
            ExtractEmfPages(data, size, 0, &found, &fingerprints);
            facts.pageSource = core::ContentFacts::PageSource::EmfRecords;
            facts.pages = static_cast<std::uint32_t>(found);
            // DEVMODE efectivo dentro del spool (el que aplica el procesador de impresión).
            EmfSpoolDevMode spoolDevMode = ReadEmfSpoolDevMode(data, size);
            if (!spoolDevMode.firstPage.empty())
                facts.spoolDevMode = core::ParseDevMode(
                    reinterpret_cast<const DEVMODEW*>(spoolDevMode.firstPage.data()), spoolDevMode.firstPage.size());
            facts.spoolDevModeChanges = spoolDevMode.changesBetweenPages;
            if (facts.complete) {
                facts.repeatChecked = true;
                facts.repeatPeriod = core::RepeatPeriod(fingerprints);
            }
            break;
        }
        case DataFormat::Xps: {
            std::wstring error;
            if (!AnalyzeXpsPackage(data, size, facts, &error)) {
                // Paquete que el XPS OM no abre (incompleto, piezas...): al menos las páginas.
                util::LogError(L"No se pudo analizar el paquete XPS: %ls", error.c_str());
                facts.pageSource = core::ContentFacts::PageSource::XpsFixedPages;
                facts.pages = CountXpsPages(data, size);
            }
            if (facts.complete) {
                auto fingerprints = XpsPageFingerprints(data, size);
                if (!fingerprints.empty() && fingerprints.size() == facts.pages) {
                    facts.repeatChecked = true;
                    facts.repeatPeriod = core::RepeatPeriod(fingerprints);
                }
            }
            break;
        }
        case DataFormat::Pcl:
        case DataFormat::PclXl:
        case DataFormat::PostScript:
        case DataFormat::Pdf:
        case DataFormat::EscPage:
        case DataFormat::EscPr:
        case DataFormat::Unknown: ParsePdlSettings(data, size, facts.complete, facts); break;
        default: break;
    }
}

namespace {

void FromEmf(const SpoolData& spool, const PreviewOptions& opt, PreviewDocument& doc) {
    size_t found = 0;
    auto emfs = ExtractEmfPages(spool.bytes.data(), spool.bytes.size(), static_cast<size_t>(opt.maxPages), &found);
    for (auto& bytes : emfs) doc.pages.push_back(std::make_shared<EmfPage>(std::move(bytes)));
    doc.totalPages = std::max<std::uint32_t>(doc.totalPages, static_cast<std::uint32_t>(found));
}

void FromExternal(const SpoolData& spool, const PreviewOptions& opt, PreviewDocument& doc) {
    const wchar_t* formatName = DataFormatName(spool.format);
    if (RendererFor(spool.format, opt.renderers).empty()) {
        doc.message = util::Format(Tr(S::PreviewNeedsRenderer), formatName);
        return;
    }
    auto result = RenderExternal(spool.format, spool.bytes.data(), spool.bytes.size(), opt.renderers, opt.rasterDpi,
                                 1, opt.maxPages);
    doc.pages = std::move(result.pages);
    if (doc.pages.empty()) doc.message = util::Format(Tr(S::PreviewRenderFailed), result.error.c_str());
}

// XPS sin GhostXPS: intentamos la miniatura del Shell sobre una copia temporal.
void FromShellTemp(const SpoolData& spool, const wchar_t* extension, PreviewDocument& doc) {
    // Copia temporal en la carpeta privada del proceso; se borra enseguida.
    std::wstring path = NewTempPath(extension);
    if (path.empty()) return;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, spool.bytes.data(), static_cast<DWORD>(std::min<size_t>(spool.bytes.size(), 0x7fffffff)), &written,
              nullptr);
    CloseHandle(file);
    if (auto page = LoadShellThumbnail(path, 1024)) {
        doc.pages.push_back(page);
        doc.source = Tr(S::PreviewSourceThumbnail);
        doc.message.clear();
    }
    DeleteFileW(path.c_str());
}

void FromOriginalFile(const std::wstring& path, const PreviewOptions& opt, PreviewDocument& doc) {
    if (IsImageExtension(path)) {
        std::uint32_t frames = 0;
        doc.pages = LoadImageFile(path, static_cast<size_t>(opt.maxPages), 2400, &frames);
        if (!doc.pages.empty()) {
            doc.format = DataFormat::Image;
            doc.source = Tr(S::PreviewSourceOriginal);
            doc.totalPages = std::max(doc.totalPages, frames);
            return;
        }
    }
    if (auto page = LoadShellThumbnail(path, 1024)) {
        doc.pages.push_back(page);
        doc.source = Tr(S::PreviewSourceThumbnail);
    }
}

}  // namespace

DocumentPtr BuildPreview(const PreviewRequest& request) {
    auto doc = std::make_shared<PreviewDocument>();
    const auto& opt = request.options;
    doc->totalPages = request.job.totalPages;

    SpoolSearch search{request.server, request.job.printer, opt.spoolOverrideDir};
    SpoolData spool = ReadJobSpool(search, request.job, opt.maxSpoolBytes);
    if (spool.stillSpooling) {
        doc->message = Tr(S::PreviewStillSpooling);
    } else if (spool.error == 0 && spool.bytes.empty()) {
        // Los datos existen pero todavía están vacíos: el trabajo sigue llegando.
        doc->message = Tr(S::PreviewSpoolEmpty);
    } else if (spool.error == 0) {
        doc->format = spool.format;
        doc->dataBytes = spool.fileSize;
        doc->dataOrigin = spool.origin == SpoolOrigin::SpoolerApi ? L"api" : spool.path;
        doc->source = util::Format(spool.origin == SpoolOrigin::SpoolerApi ? Tr(S::PreviewSourceApi)
                                                                            : Tr(S::PreviewSourceSpool),
                                   DataFormatName(spool.format));
        AnalyzeSpoolData(spool, doc->facts);
        switch (spool.format) {
            case DataFormat::EmfSpool: FromEmf(spool, opt, *doc); break;
            case DataFormat::Xps:
                doc->totalPages = std::max(doc->totalPages, doc->facts.pages);
                FromExternal(spool, opt, *doc);
                if (doc->pages.empty()) FromShellTemp(spool, L".xps", *doc);
                break;
            case DataFormat::Pdf:
                FromExternal(spool, opt, *doc);
                if (doc->pages.empty()) FromShellTemp(spool, L".pdf", *doc);
                break;
            case DataFormat::PostScript:
            case DataFormat::Pcl:
            case DataFormat::PclXl: FromExternal(spool, opt, *doc); break;
            case DataFormat::Image: {
                std::uint32_t frames = 0;
                doc->pages = LoadImageMemory(spool.bytes.data(), spool.bytes.size(),
                                             static_cast<size_t>(opt.maxPages), 2400, &frames);
                doc->totalPages = std::max(doc->totalPages, frames);
                break;
            }
            case DataFormat::EscPage:
            case DataFormat::EscPr:
                // Lenguajes propios de Epson: no hay intérprete; no se intenta "adivinar".
                doc->message = util::Format(Tr(S::PreviewNotAvailableFormat), DataFormatName(spool.format));
                break;
            case DataFormat::Unknown: doc->message = Tr(S::PreviewNotAvailableUnknown); break;
            default:
                doc->message = util::Format(Tr(S::PreviewUnsupported), DataFormatName(spool.format));
                break;
        }
        if (spool.truncated) doc->truncated = true;
    } else {
        doc->message = spool.error == ERROR_ACCESS_DENIED ? std::wstring(Tr(S::PreviewAccessDenied))
                                                          : util::Format(Tr(S::PreviewSpoolMissing), request.job.id);
        // Se indica qué se intentó y por qué falló cada vía.
        if (!spool.diagnostics.empty())
            doc->message += L"\n\n" + std::wstring(Tr(S::PreviewSearchedIn)) + L"\n" +
                            ShortDiagnostics(spool.diagnostics);
    }

    // Respaldo: el archivo original (solo si el documento es una ruta accesible desde aquí).
    if (doc->pages.empty() && LooksLikePath(request.job.document) &&
        GetFileAttributesW(request.job.document.c_str()) != INVALID_FILE_ATTRIBUTES) {
        FromOriginalFile(request.job.document, opt, *doc);
        if (!doc->pages.empty()) doc->message.clear();
    }

    doc->totalPages = std::max<std::uint32_t>(doc->totalPages, static_cast<std::uint32_t>(doc->pages.size()));
    if (doc->pages.size() < doc->totalPages && !doc->pages.empty() &&
        doc->pages.size() >= static_cast<size_t>(opt.maxPages))
        doc->truncated = true;
    return doc;
}

}  // namespace cola::preview
