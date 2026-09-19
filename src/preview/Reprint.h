#pragma once
// Reimpresión de un trabajo en otra impresora (función "reenviar").
//
// Estrategias, de mejor a peor calidad:
//   - EMF: se reproducen las páginas vectoriales en el DC de la impresora destino.
//   - Mismo driver/lenguaje: se copian los bytes RAW tal cual (idéntico al original).
//   - Otro driver: se rasteriza con Ghostscript/GhostPCL a 300 DPI y se imprime como imagen.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "preview/PreviewTypes.h"

namespace cola::preview {

struct ReprintResult {
    bool ok = false;
    unsigned long error = 0;  // Win32
    std::uint32_t pages = 0;
};

// Imprime páginas vía GDI. 'nextPages' se llama repetidamente para obtener
// lotes de páginas (permite rasterizar por partes sin agotar memoria);
// devuelve un vector vacío cuando no hay más.
ReprintResult PrintPagesGdi(const std::wstring& printer, const std::wstring& docName,
                            const std::function<std::vector<PagePtr>()>& nextPages);

// Envía bytes al spooler de la impresora destino con el datatype indicado.
ReprintResult SendRawData(const std::wstring& printer, const std::wstring& docName, const std::uint8_t* data,
                          size_t size, const wchar_t* datatype = L"RAW");

}  // namespace cola::preview
