#pragma once
// Carga de imágenes con WIC (PNG, JPG, BMP, GIF, TIFF multipágina...) y
// miniaturas del Shell de Windows (para PDF/DOCX/XLSX si hay un proveedor de
// miniaturas instalado, p. ej. Office o un lector de PDF).
// Requieren COM inicializado en el hilo que llama.

#include <cstdint>
#include <string>
#include <vector>

#include "preview/PreviewTypes.h"

namespace cola::preview {

// Carga todas las páginas/fotogramas de una imagen (hasta maxPages).
// maxDimension: reduce las imágenes enormes para limitar memoria.
std::vector<PagePtr> LoadImageFile(const std::wstring& path, size_t maxPages, int maxDimension,
                                   std::uint32_t* totalFrames = nullptr);
std::vector<PagePtr> LoadImageMemory(const std::uint8_t* data, size_t size, size_t maxPages, int maxDimension,
                                     std::uint32_t* totalFrames = nullptr);

// Miniatura del Shell (null si no hay proveedor de miniaturas para el tipo).
PagePtr LoadShellThumbnail(const std::wstring& path, int size);

// Extensiones que WIC sabe decodificar directamente.
bool IsImageExtension(const std::wstring& path);

}  // namespace cola::preview
