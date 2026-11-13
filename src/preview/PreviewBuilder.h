#pragma once
// Construye la vista previa de un trabajo probando fuentes en orden:
//   1. Archivo de spool del servidor (EMF nativo; XPS/PDF/PS/PCL vía renderizador externo)
//   2. Archivo original, si el nombre del documento es una ruta de unidad
//      local y el trabajo se envió desde este equipo (CanUseOriginalFile)
//   3. Miniatura del Shell de Windows
// Operación bloqueante: se ejecuta en el hilo de PreviewService.
//
// Detalle de (1): los datos se leen con ReadJobSpool (API del spooler y, de
// respaldo, el archivo; D22), se analizan (AnalyzeSpoolData: páginas,
// DEVMODE, opciones declaradas; D23-D25) y se dibujan según el formato:
//   EMF        -> páginas EmfPage (vectorial, nativo, D7)
//   XPS / PDF  -> Ghostscript/GhostXPS; si no hay, miniatura del Shell
//   PS/PCL/XL  -> Ghostscript/GhostPCL
//   Imagen     -> WIC
//   ESC/Page   -> sin vista previa, con el motivo (D26); la configuración sí
//   Otro       -> sin vista previa, con el motivo
// Si no salió ninguna página, (2) y (3) son respaldos.

#include <cstdint>
#include <string>

#include "core/PrintTypes.h"
#include "preview/ExternalRenderer.h"
#include "preview/PreviewTypes.h"
#include "preview/SpoolFile.h"

namespace cola::preview {

// Parámetros de construcción (vienen de la configuración del operador).
struct PreviewOptions {
    std::wstring spoolOverrideDir;  // carpeta de spool alternativa para este servidor
    RendererPaths renderers;        // ya detectados
    int rasterDpi = 110;            // DPI para rasterizar (imágenes externas)
    int maxPages = 30;              // páginas máximas a preparar
    std::uint64_t maxSpoolBytes = 512ull * 1024 * 1024;  // tope de bytes a leer del trabajo (512 MB)
};

// Pedido de vista previa de un trabajo: se copia entero al hilo de fondo
// (no comparte memoria con la UI).
struct PreviewRequest {
    std::wstring server;     // servidor de la cola ("" = este equipo)
    core::JobInfo job;       // instantánea del trabajo (id, cola, estado, tamaño...)
    PreviewOptions options;  // límites y renderizadores

    // Clave de caché: cambia si el trabajo crece (sigue recibiendo datos) o cambia de páginas.
    // Formato: "servidor|cola|id|bytes|páginas|recibiendo|legible".
    std::wstring Key() const;
};

// Construye la vista previa completa del trabajo. Nunca devuelve nullptr:
// si no hay páginas, el documento trae 'message' con el motivo. Bloquea
// (lecturas de red, reintentos, intérpretes externos de hasta 60 s): solo
// en el hilo de PreviewService, con COM inicializado (WIC y Shell).
DocumentPtr BuildPreview(const PreviewRequest& request);

// true si se puede usar como respaldo de la vista previa el archivo cuyo
// nombre trae el trabajo (F5). El nombre del documento lo elige quien imprime,
// así que solo vale si es una ruta de unidad local ("X:\...", nunca UNC) y el
// trabajo se envió desde este mismo equipo. Función pura: no toca el disco.
//  document:    JobInfo::document.
//  jobMachine:  equipo de origen (JOB_INFO_2.pMachineName, con o sin "\\").
//  thisMachine: nombre de este equipo (con o sin "\\"); "" = desconocido -> false.
bool CanUseOriginalFile(const std::wstring& document, const std::wstring& jobMachine,
                        const std::wstring& thisMachine);

// Lo que se puede averiguar de los datos de un trabajo sin renderizarlos:
// formato, páginas, repeticiones, DEVMODE del spool EMF y opciones declaradas
// (PJL, EJL, DSC, PrintTicket). Lo usan la vista previa y JobFactsService.
// No dibuja nada; para XPS usa el XPS Object Model (COM inicializado en el
// hilo que llama). facts.analyzed queda en true siempre.
void AnalyzeSpoolData(const SpoolData& spool, core::ContentFacts& facts);

}  // namespace cola::preview
