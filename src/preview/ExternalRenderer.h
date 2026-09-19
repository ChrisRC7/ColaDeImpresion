#pragma once
// Renderizado de lenguajes de impresora con la familia Ghostscript (opcional).
//
// Con impresoras compartidas Windows usa por defecto "Client Side Rendering":
// el equipo del estudiante genera los datos finales (PCL / PostScript / PDF) y
// el servidor solo recibe RAW. Windows no trae intérpretes para esos lenguajes,
// así que si están instalados se usan:
//   - Ghostscript  (gswin64c.exe)   -> PostScript y PDF
//   - GhostPCL     (gpcl6win64.exe) -> PCL 5 / PCL XL (PCL 6)
//   - GhostXPS     (gxpswin64.exe)  -> XPS
// Se detectan en "C:\\Program Files\\gs\\...", en la carpeta "renderers" junto
// al .exe, en el PATH, o se configuran en config.json ("renderers").

#include <string>
#include <vector>

#include "preview/PreviewTypes.h"

namespace cola::preview {

struct RendererPaths {
    std::wstring ghostscript;
    std::wstring ghostpcl;
    std::wstring ghostxps;
};

// Completa las rutas vacías con las que se encuentren instaladas.
RendererPaths DetectRenderers(const RendererPaths& configured);

// Ejecutable adecuado para el formato (vacío si no hay).
std::wstring RendererFor(DataFormat format, const RendererPaths& paths);

struct ExternalRenderResult {
    std::vector<PagePtr> pages;
    std::wstring error;
};

// Renderiza 'data' (datos del trabajo) a páginas bitmap.
// firstPage/lastPage son 1-based e inclusivos.
ExternalRenderResult RenderExternal(DataFormat format, const std::uint8_t* data, size_t size,
                                    const RendererPaths& paths, int dpi, int firstPage, int lastPage,
                                    unsigned timeoutMs = 60000);

}  // namespace cola::preview
