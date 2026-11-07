#pragma once
// Acceso al archivo de spool (.SPL) de un trabajo y detección de su formato.
//
// El spooler guarda los datos de cada trabajo en <carpeta de spool>\NNNNN.SPL
// (NNNNN = id del trabajo con 5 dígitos). En un servidor remoto la carpeta se
// alcanza por el recurso administrativo \\SERVIDOR\C$\... (requiere ser
// administrador del servidor) o por una carpeta alternativa configurada.
//
// Desde D22 la vía principal NO es el archivo sino la API del spooler
// (OpenPrinter("Cola,Job N") + ReadPrinter, ver ReadJobSpool): el archivo
// queda como respaldo. Este módulo también decide el formato real de los
// datos (DetectFormat) mirando el contenido y no pDatatype, porque un driver
// v4 declara "RAW" y manda XPS, y un trabajo Epson empieza con ESC y antes se
// confundía con PCL (D26).
//
// Hilos: todas las funciones de lectura hacen E/S de red o RPC y se llaman
// desde hilos de fondo (PreviewService, JobFactsService, reimpresión); nunca
// desde el hilo de la UI. Las funciones puras (DetectFormat, EjlLanguage,
// SkipPjlHeader, ShadowMatchesJob, ToAdminShare, ReadLooksIncomplete) son
// seguras desde cualquier hilo y tienen pruebas en tests/.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/PrintTypes.h"
#include "preview/PdlSettings.h"
#include "preview/PreviewTypes.h"

namespace cola::preview {

// Dónde buscar el spool de un trabajo.
struct SpoolSearch {
    std::wstring server;       // "" = este equipo; "\\SRV" o "SRV" para un servidor remoto
    std::wstring printer;      // cola del trabajo (puede tener carpeta propia)
    std::wstring overrideDir;  // carpeta alternativa configurada por el usuario
                               // (spoolDirOverrides en config.json); "" = ninguna
};

// Cómo se obtuvieron los datos del trabajo.
//   None       : no se obtuvieron (ver error / stillSpooling / notHeld / incomplete).
//   SpoolerApi : OpenPrinter("Cola,Job N") + ReadPrinter (vía principal, D22).
//   File       : archivo .SPL de la carpeta de spool (alternativa o respaldo).
enum class SpoolOrigin { None, SpoolerApi, File };

// Resultado de leer los datos de un trabajo: los bytes (posiblemente
// recortados a maxBytes), el formato detectado y, si falló, por qué. Se pasa
// por valor entre hilos; no tiene punteros a memoria ajena.
struct SpoolData {
    SpoolOrigin origin = SpoolOrigin::None;  // de dónde salieron los bytes
    unsigned long apiError = 0;      // error de la API del spooler si no se pudo usar
    bool stillSpooling = false;      // no se leyó: el trabajo todavía se está recibiendo
    bool notHeld = false;            // no se leyó: no está en pausa (leerlo dañaría la impresión)
    bool incomplete = false;         // se leyó solo una parte: otro programa lo leía a la vez (se descartó)
    std::wstring path;               // archivo leído (vacío si vino por la API)
    std::vector<std::uint8_t> bytes; // datos leídos (como máximo maxBytes)
    std::uint64_t fileSize = 0;      // tamaño real (bytes puede estar recortado)
    bool truncated = false;          // true si bytes es solo el principio (se cortó en maxBytes)
    // ESC/Page cortado por tamaño: páginas contadas recorriendo el resto de los
    // datos sin guardarlo (solo si se llegó al final y se leyó todo, D53).
    std::optional<EscPageCount> escPageAllData;
    DataFormat format = DataFormat::Unknown;  // lenguaje detectado por el contenido (DetectFormat)
    unsigned long error = 0;         // error Win32 si no se pudo leer (0 = éxito; ERROR_BUSY
                                     // si no se leyó a propósito: spooling o no pausado)
    // Carpetas probadas y por qué falló cada una (para explicarlo en la UI y el log).
    std::vector<std::wstring> searched;  // carpetas de spool probadas, en orden (D21)
    std::wstring diagnostics;            // una línea "carpeta — mensaje de error" por intento
};

// Carpetas donde buscar el spool, en orden de preferencia: alternativa
// configurada, carpeta propia de la cola, carpeta del servidor y su variante
// por ADMIN$. Si el servidor es este mismo equipo se usan rutas locales (no
// hace falta pasar por \\\\SRV\\C$).
std::vector<std::wstring> SpoolDirectories(const SpoolSearch& search);

// Rutas directas "NNNNN.SPL" (id del trabajo con 5 dígitos) en cada carpeta.
// No comprueba que existan; sirve para mostrar/registrar dónde se buscaría.
std::vector<std::wstring> SpoolFileCandidates(const SpoolSearch& search, std::uint32_t jobId);

// Convierte una ruta local del servidor ("C:\\X") a UNC ("\\\\SRV\\C$\\X").
// server debe venir ya normalizado ("\\SRV"); si está vacío, o la ruta ya es
// UNC, o no empieza con letra de unidad, la devuelve sin cambios.
std::wstring ToAdminShare(const std::wstring& server, const std::wstring& localPath);

// true si el contenido de un .SHD (archivo sombra) corresponde al trabajo.
// data/size: bytes del .SHD. jobId: id buscado. document: nombre del
// documento (se compara su principio, en UTF-16).
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
// En cada carpeta de SpoolDirectories, en orden; la primera que funciona gana.
// Si ninguna sirve, error queda con el motivo más informativo y diagnostics
// con una línea por carpeta. Hilo de fondo (E/S por SMB).
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
// Si un trabajo ESC/Page pasa de maxBytes, el resto se recorre sin guardarlo
// para contar sus páginas (escPageAllData).
// allowFile = false: solo la API del spooler (para análisis en segundo plano,
// donde buscar en la carpeta de spool solo sumaría esperas y errores).
// Puede bloquear varios segundos (reintentos con Sleep): solo en hilos de fondo.
SpoolData ReadJobSpool(const SpoolSearch& search, const core::JobInfo& job, std::uint64_t maxBytes,
                       bool allowFile = true);

// Una lectura por la API llegó incompleta: no se cortó por tamaño (truncated)
// y trae menos bytes que JOB_INFO_2.Size. Pasa cuando otro programa (otra
// copia de la app en otra PC) lee el mismo trabajo a la vez: las lecturas
// comparten el avance y cada una se queda con bloques sueltos.
// jobSize: JOB_INFO_2.Size; bytesRead: lo recibido; truncated: si se cortó
// a propósito en maxBytes (entonces nunca se considera incompleta). D48.
bool ReadLooksIncomplete(std::uint64_t jobSize, std::uint64_t bytesRead, bool truncated);

// Lee un archivo cualquiera (hasta maxBytes) compartiendo lectura/escritura.
// Se abre con FILE_SHARE_READ | WRITE | DELETE para no chocar con el spooler,
// que mantiene el .SPL/.SHD abierto. out recibe los bytes leídos; fileSize
// (opcional) el tamaño real del archivo; error (opcional) el código Win32.
// Devuelve false solo si no se pudo abrir; una lectura corta deja out más
// pequeño pero devuelve true.
bool ReadFileShared(const std::wstring& path, std::uint64_t maxBytes, std::vector<std::uint8_t>& out,
                    std::uint64_t* fileSize, unsigned long* error);

// Detecta el lenguaje de los datos (EMF spool, XPS, PDF, PostScript, PCL, PCL XL...).
// Entiende el envoltorio PJL (@PJL ENTER LANGUAGE=...).
// Solo mira el principio de los datos (firmas mágicas y encabezados), por lo
// que sirve con datos recortados. Función pura.
DataFormat DetectFormat(const std::uint8_t* data, size_t size);

// Lenguaje declarado en un encabezado EJL de Epson ("ESC 01 @EJL ... LA=ESC/PAGE").
// Unknown si los datos no empiezan con EJL. Un EJL sin "LA=" o con un
// lenguaje no reconocido se toma como ESC/Page (EJL es exclusivo de Epson).
DataFormat EjlLanguage(const std::uint8_t* data, size_t size);

// Posición donde empiezan los datos reales tras un encabezado PJL/UEL (0 si no hay).
// Salta cualquier combinación de UEL (ESC%-12345X), líneas "@PJL ...\n" y
// saltos de línea sueltos. Devuelve size si una línea PJL no termina.
size_t SkipPjlHeader(const std::uint8_t* data, size_t size);

}  // namespace cola::preview
