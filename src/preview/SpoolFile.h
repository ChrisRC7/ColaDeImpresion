#pragma once
// Acceso al archivo de spool (.SPL) de un trabajo y detección de su formato.
//
// El spooler guarda los datos de cada trabajo en <carpeta de spool>\NNNNN.SPL
// (NNNNN = id del trabajo con 5 dígitos). En un servidor remoto la carpeta se
// alcanza por el recurso administrativo \\SERVIDOR\C$\... (requiere ser
// administrador del servidor) o por una carpeta alternativa configurada.

#include <cstdint>
#include <string>
#include <vector>

#include "core/PrintTypes.h"
#include "preview/PreviewTypes.h"

namespace cola::preview {

// Dónde buscar el spool de un trabajo.
struct SpoolSearch {
    std::wstring server;       // "" = este equipo
    std::wstring printer;      // cola del trabajo (puede tener carpeta propia)
    std::wstring overrideDir;  // carpeta alternativa configurada por el usuario
};

// Cómo se obtuvieron los datos del trabajo.
enum class SpoolOrigin { None, SpoolerApi, File };

struct SpoolData {
    SpoolOrigin origin = SpoolOrigin::None;
    unsigned long apiError = 0;      // error de la API del spooler si no se pudo usar
    bool stillSpooling = false;      // no se leyó: el trabajo todavía se está recibiendo
    bool notHeld = false;            // no se leyó: no está en pausa (leerlo dañaría la impresión)
    bool incomplete = false;         // se leyó solo una parte: otro programa lo leía a la vez (se descartó)
    std::wstring path;               // archivo leído (vacío si vino por la API)
    std::vector<std::uint8_t> bytes;
    std::uint64_t fileSize = 0;      // tamaño real (bytes puede estar recortado)
    bool truncated = false;
    DataFormat format = DataFormat::Unknown;
    unsigned long error = 0;         // error Win32 si no se pudo leer
    // Carpetas probadas y por qué falló cada una (para explicarlo en la UI y el log).
    std::vector<std::wstring> searched;
    std::wstring diagnostics;
};

// Carpetas donde buscar el spool, en orden de preferencia: alternativa
// configurada, carpeta propia de la cola, carpeta del servidor y su variante
// por ADMIN$. Si el servidor es este mismo equipo se usan rutas locales (no
// hace falta pasar por \\\\SRV\\C$).
std::vector<std::wstring> SpoolDirectories(const SpoolSearch& search);

// Rutas directas "NNNNN.SPL" (id del trabajo con 5 dígitos) en cada carpeta.
std::vector<std::wstring> SpoolFileCandidates(const SpoolSearch& search, std::uint32_t jobId);

// Convierte una ruta local del servidor ("C:\\X") a UNC ("\\\\SRV\\C$\\X").
std::wstring ToAdminShare(const std::wstring& server, const std::wstring& localPath);

// true si el contenido de un .SHD (archivo sombra) corresponde al trabajo.
// requireDocument = false acepta la coincidencia solo por id (segunda pasada:
// algunos servidores guardan el nombre del documento de otra forma).
bool ShadowMatchesJob(const std::uint8_t* data, size_t size, std::uint32_t jobId, const std::wstring& document,
                      bool requireDocument = true);

// Con "spool file pooling" los archivos se llaman FPnnnnn.SPL: busca el del
// trabajo revisando los .SHD de la carpeta. Devuelve "" si no lo encuentra.
// Primero exige que coincidan id y documento; si nada coincide, reintenta solo
// con el id. 'error' recibe el motivo más informativo (acceso denegado, etc.).
std::wstring FindPooledSpoolFile(const std::wstring& dir, std::uint32_t jobId, const std::wstring& document,
                                 unsigned long* error);

// Lee el spool del trabajo (como máximo maxBytes) y detecta el formato.
// Prueba primero NNNNN.SPL y luego los archivos FPnnnnn del pooling.
SpoolData ReadSpoolFile(const SpoolSearch& search, std::uint32_t jobId, const std::wstring& document,
                        std::uint64_t maxBytes);

// Lee los datos de un trabajo (como máximo maxBytes) y detecta el formato:
//   1. carpeta alternativa configurada (spoolDirOverrides), si el operador la definió;
//   2. API del spooler (OpenPrinter "Impresora,Job N" + ReadPrinter): usa los
//      permisos delegados sobre la cola ("Administrar documentos"), sin ser
//      administrador del servidor, sin \\SRV\C$ y sin chocar con los bloqueos
//      que el spooler mantiene sobre los .SHD/.SPL;
//   3. archivo de spool en la carpeta del servidor (respaldo).
// Un trabajo que todavía se está recibiendo no se lee (ReadPrinter esperaría),
// y tampoco uno que no esté en pausa (core::CanReadJobData): la lectura le
// quitaría datos al procesador de impresión. Una lectura incompleta
// (ReadLooksIncomplete) se reintenta unas veces con espera; si no se logra
// completa se descarta (incomplete = true) en vez de analizar bloques sueltos.
// allowFile = false: solo la API del spooler (para análisis en segundo plano,
// donde buscar en la carpeta de spool solo sumaría esperas y errores).
SpoolData ReadJobSpool(const SpoolSearch& search, const core::JobInfo& job, std::uint64_t maxBytes,
                       bool allowFile = true);

// Una lectura por la API llegó incompleta: no se cortó por tamaño (truncated)
// y trae menos bytes que JOB_INFO_2.Size. Pasa cuando otro programa (otra
// copia de la app en otra PC) lee el mismo trabajo a la vez: las lecturas
// comparten el avance y cada una se queda con bloques sueltos.
bool ReadLooksIncomplete(std::uint64_t jobSize, std::uint64_t bytesRead, bool truncated);

// Lee un archivo cualquiera (hasta maxBytes) compartiendo lectura/escritura.
bool ReadFileShared(const std::wstring& path, std::uint64_t maxBytes, std::vector<std::uint8_t>& out,
                    std::uint64_t* fileSize, unsigned long* error);

// Detecta el lenguaje de los datos (EMF spool, XPS, PDF, PostScript, PCL, PCL XL...).
// Entiende el envoltorio PJL (@PJL ENTER LANGUAGE=...).
DataFormat DetectFormat(const std::uint8_t* data, size_t size);

// Lenguaje declarado en un encabezado EJL de Epson ("ESC 01 @EJL ... LA=ESC/PAGE").
// Unknown si los datos no empiezan con EJL.
DataFormat EjlLanguage(const std::uint8_t* data, size_t size);

// Posición donde empiezan los datos reales tras un encabezado PJL/UEL (0 si no hay).
size_t SkipPjlHeader(const std::uint8_t* data, size_t size);

}  // namespace cola::preview
