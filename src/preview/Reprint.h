#pragma once
// Reimpresión de un trabajo en otra impresora (función "reenviar").
//
// Estrategias, de mejor a peor calidad:
//   - EMF: se reproducen las páginas vectoriales en el DC de la impresora destino.
//   - Mismo driver/lenguaje: se copian los bytes RAW tal cual (idéntico al original).
//   - Otro driver: se rasteriza con Ghostscript/GhostPCL a 300 DPI y se imprime como imagen.
// (D12. La elección de estrategia la hace app/JobActions; este módulo solo
// ofrece las dos primitivas: imprimir páginas por GDI y enviar bytes RAW.)
//
// Hilos: se llama desde el hilo de acciones (JobActionRunner), nunca desde
// la UI: abrir la impresora, generar el trabajo y escribirlo puede tardar.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "preview/PreviewTypes.h"

namespace cola::preview {

// Resultado de una reimpresión.
struct ReprintResult {
    bool ok = false;          // el trabajo nuevo se entregó completo al spooler
    unsigned long error = 0;  // Win32
    std::uint32_t pages = 0;  // páginas impresas por GDI (0 en SendRawData)
};

// Imprime páginas vía GDI. 'nextPages' se llama repetidamente para obtener
// lotes de páginas (permite rasterizar por partes sin agotar memoria);
// devuelve un vector vacío cuando no hay más.
// printer: cola destino (puede ser "\\SRV\Cola"). docName: nombre del
// documento en la cola destino. Cada página se escala a la hoja física
// conservando su proporción, y la orientación se ajusta por página. Si
// falla una página se cancela el documento entero (AbortDoc).
ReprintResult PrintPagesGdi(const std::wstring& printer, const std::wstring& docName,
                            const std::function<std::vector<PagePtr>()>& nextPages);

// Envía bytes al spooler de la impresora destino con el datatype indicado.
// (StartDocPrinter + WritePrinter en bloques de 1 MB + EndDocPrinter). Con
// "RAW" los bytes llegan sin cambios a la impresora: solo sirve si entiende
// el mismo lenguaje que el driver original. Otro datatype (p. ej.
// "NT EMF 1.008") deja que el procesador de impresión los interprete.
ReprintResult SendRawData(const std::wstring& printer, const std::wstring& docName, const std::uint8_t* data,
                          size_t size, const wchar_t* datatype = L"RAW");

}  // namespace cola::preview
