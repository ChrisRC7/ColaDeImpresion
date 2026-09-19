#include "preview/Reprint.h"

#include <windows.h>
#include <winspool.h>

#include <algorithm>

#include "core/Spooler.h"

namespace cola::preview {

namespace {

// DEVMODE por defecto de la impresora (para poder cambiar la orientación por página).
std::vector<BYTE> DefaultDevMode(const std::wstring& printer) {
    core::PrinterHandle h;
    if (!h.Open(printer, false)) return {};
    LONG size = DocumentPropertiesW(nullptr, h.get(), const_cast<LPWSTR>(printer.c_str()), nullptr, nullptr, 0);
    if (size <= 0) return {};
    std::vector<BYTE> buffer(static_cast<size_t>(size));
    auto* dm = reinterpret_cast<DEVMODEW*>(buffer.data());
    if (DocumentPropertiesW(nullptr, h.get(), const_cast<LPWSTR>(printer.c_str()), dm, nullptr, DM_OUT_BUFFER) != IDOK)
        return {};
    return buffer;
}

}  // namespace

ReprintResult PrintPagesGdi(const std::wstring& printer, const std::wstring& docName,
                            const std::function<std::vector<PagePtr>()>& nextPages) {
    ReprintResult result;
    std::vector<BYTE> devModeBuffer = DefaultDevMode(printer);
    auto* devMode = devModeBuffer.empty() ? nullptr : reinterpret_cast<DEVMODEW*>(devModeBuffer.data());

    HDC dc = CreateDCW(L"WINSPOOL", printer.c_str(), nullptr, devMode);
    if (!dc) {
        result.error = GetLastError();
        return result;
    }
    DOCINFOW info{};
    info.cbSize = sizeof(info);
    info.lpszDocName = docName.c_str();
    if (StartDocW(dc, &info) <= 0) {
        result.error = GetLastError();
        DeleteDC(dc);
        return result;
    }

    bool failed = false;
    for (;;) {
        std::vector<PagePtr> batch = nextPages();
        if (batch.empty()) break;
        for (const auto& page : batch) {
            SIZE size = page->SizeHimetric();
            // Orientación según el aspecto de la página.
            if (devMode && (devMode->dmFields & DM_ORIENTATION)) {
                short wanted = size.cx > size.cy ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
                if (devMode->dmOrientation != wanted) {
                    devMode->dmOrientation = wanted;
                    ResetDCW(dc, devMode);
                }
            }
            if (StartPage(dc) <= 0) {
                failed = true;
                break;
            }
            // Mapea la página física completa (no solo el área imprimible) para conservar la escala.
            int offX = GetDeviceCaps(dc, PHYSICALOFFSETX);
            int offY = GetDeviceCaps(dc, PHYSICALOFFSETY);
            int physW = GetDeviceCaps(dc, PHYSICALWIDTH);
            int physH = GetDeviceCaps(dc, PHYSICALHEIGHT);
            if (physW <= 0 || physH <= 0) {
                physW = GetDeviceCaps(dc, HORZRES);
                physH = GetDeviceCaps(dc, VERTRES);
                offX = offY = 0;
            }
            RECT paper{-offX, -offY, physW - offX, physH - offY};
            page->Render(dc, FitPage(size, paper));
            if (EndPage(dc) <= 0) {
                failed = true;
                break;
            }
            ++result.pages;
        }
        if (failed) break;
    }

    if (failed || result.pages == 0) {
        result.error = failed ? GetLastError() : ERROR_NO_DATA;
        AbortDoc(dc);
    } else {
        EndDoc(dc);
        result.ok = true;
    }
    DeleteDC(dc);
    return result;
}

ReprintResult SendRawData(const std::wstring& printer, const std::wstring& docName, const std::uint8_t* data,
                          size_t size, const wchar_t* datatype) {
    ReprintResult result;
    core::PrinterHandle h;
    if (!h.Open(printer, false)) {
        result.error = GetLastError();
        return result;
    }
    DOC_INFO_1W doc{};
    doc.pDocName = const_cast<LPWSTR>(docName.c_str());
    doc.pDatatype = const_cast<LPWSTR>(datatype);
    if (!StartDocPrinterW(h.get(), 1, reinterpret_cast<BYTE*>(&doc))) {
        result.error = GetLastError();
        return result;
    }
    size_t offset = 0;
    bool ok = true;
    while (offset < size) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(size - offset, 1024 * 1024));
        DWORD written = 0;
        if (!WritePrinter(h.get(), const_cast<std::uint8_t*>(data + offset), chunk, &written) || written == 0) {
            ok = false;
            result.error = GetLastError();
            break;
        }
        offset += written;
    }
    EndDocPrinter(h.get());
    result.ok = ok;
    return result;
}

}  // namespace cola::preview
