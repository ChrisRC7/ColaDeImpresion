// Primitivas de reimpresión: páginas por GDI y envío RAW. Ver Reprint.h.
#include "preview/Reprint.h"

#include <windows.h>
#include <winspool.h>

#include <algorithm>

#include "core/Spooler.h"

namespace cola::preview {

namespace {

// DEVMODE por defecto de la impresora (para poder cambiar la orientación por página).
// Patrón de dos llamadas de DocumentProperties: la primera (fMode = 0)
// devuelve el tamaño del DEVMODE completo (incluida la parte privada del
// driver) y la segunda (DM_OUT_BUFFER) lo copia. Vacío si algo falla; en ese
// caso se imprime con el DEVMODE predeterminado y sin cambiar orientación.
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
                            const std::function<std::vector<PagePtr>()>& nextPages,
                            const std::function<bool()>& aborted) {
    ReprintResult result;
    // DEVMODE propio (modificable) para poder cambiar la orientación con
    // ResetDC entre páginas.
    std::vector<BYTE> devModeBuffer = DefaultDevMode(printer);
    auto* devMode = devModeBuffer.empty() ? nullptr : reinterpret_cast<DEVMODEW*>(devModeBuffer.data());

    // DC de impresora: todo lo que se dibuje entre StartDoc y EndDoc se
    // convierte en un trabajo nuevo en la cola destino.
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
    bool cancelled = false;  // el proveedor avisó que el documento no está completo
    DWORD pageError = ERROR_SUCCESS;
    // Pide lotes hasta que el proveedor devuelva uno vacío; cada lote se
    // suelta al terminar (memoria acotada aunque el documento sea largo).
    for (;;) {
        std::vector<PagePtr> batch = nextPages();
        if (aborted && aborted()) {
            cancelled = true;
            break;
        }
        if (batch.empty()) break;
        for (const auto& page : batch) {
            SIZE size = page->SizeHimetric();
            // Orientación según el aspecto de la página.
            // ResetDC entre páginas (antes de StartPage) es la forma
            // documentada de cambiar la orientación dentro de un documento.
            if (devMode && (devMode->dmFields & DM_ORIENTATION)) {
                short wanted = size.cx > size.cy ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
                if (devMode->dmOrientation != wanted) {
                    devMode->dmOrientation = wanted;
                    ResetDCW(dc, devMode);
                }
            }
            if (StartPage(dc) <= 0) {
                pageError = GetLastError();
                failed = true;
                break;
            }
            // Mapea la página física completa (no solo el área imprimible) para conservar la escala.
            // El origen del DC es la esquina del área imprimible; la hoja
            // física empieza PHYSICALOFFSET píxeles antes (coordenadas
            // negativas). Si el driver no informa el tamaño físico se usa el
            // área imprimible (HORZRES/VERTRES).
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
                pageError = GetLastError();
                failed = true;
                break;
            }
            ++result.pages;
        }
        if (failed) break;
    }

    // Sin páginas, con un fallo o con el documento incompleto: se cancela
    // para no dejar en la cola un trabajo vacío o a medias.
    if (failed || cancelled || result.pages == 0) {
        // Nunca ERROR_SUCCESS: quien llama distingue el éxito por 'ok', pero
        // el código se usa para el mensaje.
        if (cancelled) result.error = ERROR_CANCELLED;
        else if (failed) result.error = pageError != ERROR_SUCCESS ? pageError : ERROR_GEN_FAILURE;
        else result.error = ERROR_NO_DATA;
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
    // DOC_INFO_1: nombre, archivo de salida (nulo = la impresora) y datatype.
    DOC_INFO_1W doc{};
    doc.pDocName = const_cast<LPWSTR>(docName.c_str());
    doc.pDatatype = const_cast<LPWSTR>(datatype);
    if (!StartDocPrinterW(h.get(), 1, reinterpret_cast<BYTE*>(&doc))) {
        result.error = GetLastError();
        return result;
    }
    // Escritura en bloques de 1 MB; WritePrinter puede aceptar menos de lo
    // pedido, por eso se avanza con lo realmente escrito.
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
    // Envío completo: se cierra el documento. Si falló a mitad (M4) se
    // descarta con AbortPrinter: EndDocPrinter dejaría lo ya escrito como un
    // trabajo incompleto que la impresora intentaría imprimir.
    if (ok) {
        if (!EndDocPrinter(h.get())) {
            ok = false;
            result.error = GetLastError();
        }
    } else {
        AbortPrinter(h.get());
    }
    result.ok = ok;
    return result;
}

}  // namespace cola::preview
