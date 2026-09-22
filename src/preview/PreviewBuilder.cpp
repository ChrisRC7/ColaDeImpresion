#include "preview/PreviewBuilder.h"

#include <windows.h>

#include <algorithm>

#include "i18n/Strings.h"
#include "preview/EmfSpool.h"
#include "preview/ImageLoader.h"
#include "preview/SpoolFile.h"
#include "preview/ZipReader.h"
#include "util/Strings.h"

namespace cola::preview {

using i18n::S;
using i18n::Tr;

std::wstring PreviewRequest::Key() const {
    return util::Format(L"%ls|%ls|%u|%llu|%u", server.c_str(), job.printer.c_str(), job.id,
                        static_cast<unsigned long long>(job.sizeBytes), job.totalPages);
}

namespace {

// Lista de carpetas probadas, acortada para que quepa en el panel.
std::wstring ShortDiagnostics(const std::wstring& diagnostics) {
    std::wstring out;
    for (const auto& line : util::Split(diagnostics, L'\n')) {
        if (line.empty()) continue;
        if (!out.empty()) out += L"\n";
        out += util::EllipsisMiddle(line, 70);
    }
    return out;
}

bool LooksLikePath(const std::wstring& text) {
    return (text.size() > 3 && text[1] == L':' && text[2] == L'\\') || text.rfind(L"\\\\", 0) == 0;
}

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
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    std::wstring path = util::Format(L"%lsColaDeImpresion_thumb_%lu%ls", temp, GetTickCount(), extension);
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
    SpoolData spool = ReadSpoolFile(search, request.job.id, request.job.document, opt.maxSpoolBytes);
    if (spool.error == 0 && spool.bytes.empty()) {
        // El archivo existe pero todavía está vacío: el trabajo sigue llegando.
        doc->message = Tr(S::PreviewSpoolEmpty);
    } else if (spool.error == 0) {
        doc->format = spool.format;
        doc->dataBytes = spool.fileSize;
        doc->source = util::Format(Tr(S::PreviewSourceSpool), DataFormatName(spool.format));
        switch (spool.format) {
            case DataFormat::EmfSpool: FromEmf(spool, opt, *doc); break;
            case DataFormat::Xps:
                doc->totalPages = std::max(doc->totalPages, CountXpsPages(spool.bytes.data(), spool.bytes.size()));
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
            default:
                doc->message = util::Format(Tr(S::PreviewUnsupported), DataFormatName(spool.format));
                break;
        }
        if (spool.truncated) doc->truncated = true;
    } else if (spool.error == ERROR_ACCESS_DENIED) {
        doc->message = Tr(S::PreviewAccessDenied);
        if (!spool.diagnostics.empty())
            doc->message += L"\n\n" + std::wstring(Tr(S::PreviewSearchedIn)) + L"\n" +
                            ShortDiagnostics(spool.diagnostics);
    } else {
        // Se indica dónde se buscó: en un servidor real casi siempre es un
        // problema de carpeta o de permisos, no un trabajo que ya terminó.
        doc->message = util::Format(Tr(S::PreviewSpoolMissing), request.job.id);
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
