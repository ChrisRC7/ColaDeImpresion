#pragma once
// Carga de imágenes con WIC (PNG, JPG, BMP, GIF, TIFF multipágina...) y
// miniaturas del Shell de Windows (para PDF/DOCX/XLSX si hay un proveedor de
// miniaturas instalado, p. ej. Office o un lector de PDF).
// Requieren COM inicializado en el hilo que llama.
//
// Se usa para: trabajos que son una imagen suelta (DataFormat::Image), los
// PNG que generan Ghostscript/GhostPCL/GhostXPS (ExternalRenderer) y las
// miniaturas de XPS cuando no hay GhostXPS. Siempre desde hilos de fondo
// (PreviewService), que inicializan COM. El resultado son BitmapPage en BGRA
// opaco (la transparencia se compone sobre blanco, como en el papel).

#include <cstdint>
#include <string>
#include <vector>

#include "preview/PreviewTypes.h"

namespace cola::preview {

// Carga todas las páginas/fotogramas de una imagen (hasta maxPages).
// maxDimension: reduce las imágenes enormes para limitar memoria.
// (el lado mayor queda en maxDimension píxeles, conservando la proporción).
// totalFrames (opcional): recibe cuántos fotogramas tiene la imagen en total,
// aunque se devuelvan menos. Devuelve vacío si WIC no la puede abrir.
std::vector<PagePtr> LoadImageFile(const std::wstring& path, size_t maxPages, int maxDimension,
                                   std::uint32_t* totalFrames = nullptr);
// Igual que LoadImageFile pero desde un búfer en memoria (p. ej. los datos
// del trabajo o una imagen sacada de un ZIP). No copia 'data' más allá de
// la llamada.
std::vector<PagePtr> LoadImageMemory(const std::uint8_t* data, size_t size, size_t maxPages, int maxDimension,
                                     std::uint32_t* totalFrames = nullptr);

// Miniatura del Shell (null si no hay proveedor de miniaturas para el tipo).
// path: archivo en disco (el proveedor necesita una ruta). size: lado del
// cuadrado pedido, en píxeles (el Shell puede devolver algo mayor).
PagePtr LoadShellThumbnail(const std::wstring& path, int size);

// Extensiones que WIC sabe decodificar directamente.
// Compara la extensión de 'path' sin distinguir mayúsculas. WEBP y HEIC
// dependen de que el códec esté instalado en Windows.
bool IsImageExtension(const std::wstring& path);

}  // namespace cola::preview
