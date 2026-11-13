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
//
// Ver D7 (formatos con intérprete opcional) y D27 (los archivos intermedios
// van a la carpeta temporal privada y se borran al terminar). El paquete de
// la app incluye los renderizadores en "renderers" (build.ps1 -Package).
// Los intérpretes corren como procesos hijos ocultos, con -dSAFER y
// prioridad baja, desde hilos de fondo (PreviewService / reimpresión): nunca
// desde el hilo de la UI, porque pueden tardar segundos.

#include <string>
#include <vector>

#include "preview/PreviewTypes.h"

namespace cola::preview {

// Rutas completas a los ejecutables de cada intérprete ("" = no disponible).
struct RendererPaths {
    std::wstring ghostscript;  // gswin64c.exe: PostScript y PDF
    std::wstring ghostpcl;     // gpcl6win64.exe: PCL 5 y PCL XL
    std::wstring ghostxps;     // gxpswin64.exe: XPS / OpenXPS
};

// Completa las rutas vacías con las que se encuentren instaladas.
// También reemplaza las configuradas que ya no existen en disco. Recorre
// carpetas y el PATH: llamarla en un hilo de fondo o al iniciar, no en cada
// vista previa.
RendererPaths DetectRenderers(const RendererPaths& configured);

// Ejecutable adecuado para el formato (vacío si no hay).
// EMF, ESC/Page, imágenes y texto no tienen intérprete externo.
std::wstring RendererFor(DataFormat format, const RendererPaths& paths);

// Resultado de RenderExternal.
struct ExternalRenderResult {
    std::vector<PagePtr> pages;  // páginas rasterizadas (vacío si falló)
    std::wstring error;          // "no-renderer", "timeout", "exit code N" o mensaje Win32
    // El intérprete no terminó bien (no se pudo ejecutar, se agotó el tiempo o
    // devolvió un código distinto de 0), AUNQUE haya dejado algunas páginas:
    // 'pages' puede estar incompleto. La vista previa aprovecha lo que haya;
    // el reenvío no puede (imprimiría el documento a medias, F4). En ese caso
    // 'error' también trae el motivo.
    bool failed = false;
    bool timedOut = false;  // se terminó a la fuerza por pasarse de timeoutMs
    int exitCode = 0;       // código de salida del intérprete (-1 si no se obtuvo)
};

// Renderiza 'data' (datos del trabajo) a páginas bitmap.
// firstPage/lastPage son 1-based e inclusivos.
// dpi: resolución del PNG intermedio. timeoutMs: tiempo máximo del proceso
// hijo (se termina a la fuerza si se pasa). Bloquea hasta que el intérprete
// termina: solo en hilos de fondo.
ExternalRenderResult RenderExternal(DataFormat format, const std::uint8_t* data, size_t size,
                                    const RendererPaths& paths, int dpi, int firstPage, int lastPage,
                                    unsigned timeoutMs = 60000);

}  // namespace cola::preview
