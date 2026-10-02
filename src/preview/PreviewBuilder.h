#pragma once
// Construye la vista previa de un trabajo probando fuentes en orden:
//   1. Archivo de spool del servidor (EMF nativo; XPS/PDF/PS/PCL vía renderizador externo)
//   2. Archivo original, si el nombre del documento es una ruta accesible
//   3. Miniatura del Shell de Windows
// Operación bloqueante: se ejecuta en el hilo de PreviewService.

#include <cstdint>
#include <string>

#include "core/PrintTypes.h"
#include "preview/ExternalRenderer.h"
#include "preview/PreviewTypes.h"
#include "preview/SpoolFile.h"

namespace cola::preview {

struct PreviewOptions {
    std::wstring spoolOverrideDir;  // carpeta de spool alternativa para este servidor
    RendererPaths renderers;        // ya detectados
    int rasterDpi = 110;            // DPI para rasterizar (imágenes externas)
    int maxPages = 30;              // páginas máximas a preparar
    std::uint64_t maxSpoolBytes = 512ull * 1024 * 1024;
};

struct PreviewRequest {
    std::wstring server;
    core::JobInfo job;
    PreviewOptions options;

    // Clave de caché: cambia si el trabajo crece (sigue recibiendo datos) o cambia de páginas.
    std::wstring Key() const;
};

DocumentPtr BuildPreview(const PreviewRequest& request);

// Lo que se puede averiguar de los datos de un trabajo sin renderizarlos:
// formato, páginas, repeticiones, DEVMODE del spool EMF y opciones declaradas
// (PJL, EJL, DSC, PrintTicket). Lo usan la vista previa y JobFactsService.
void AnalyzeSpoolData(const SpoolData& spool, core::ContentFacts& facts);

}  // namespace cola::preview
