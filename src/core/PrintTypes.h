#pragma once
// Tipos de datos del dominio: impresoras, trabajos e instantáneas de la cola.
// No dependen de la UI; los produce core/Spooler y los consume el resto.
//
// Son copias "planas" (std::wstring, enteros, SYSTEMTIME) de lo que devuelve
// winspool en PRINTER_INFO_2 / JOB_INFO_2: así se pueden pasar entre hilos
// (QueueMonitor -> MainWindow) sin depender de los búferes de la API, que se
// liberan al volver de EnumPrinters/EnumJobs. Toda la información "cruda" del
// spooler se conserva (rawStatus, datatype, printProcessor...) además de la
// interpretación simplificada (JobState, PrinterProblem), porque varias
// decisiones (D39, D47, D50) dependen de flags concretos como
// JOB_STATUS_RENDERING_LOCALLY.

#include <windows.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/PrintSettings.h"

namespace cola::core {

// Estado simplificado de un trabajo (derivado de los flags JOB_STATUS_*).
// Un trabajo puede tener varios flags a la vez (p. ej. PAUSED | SPOOLING);
// core::JobStateFromFlags (JobStatus.cpp) elige uno solo con un orden de prioridad.
// Es lo que muestra la columna Estado y lo que usan el JobTracker y el
// PrintReleaseTracker para sus heurísticas; las decisiones delicadas (leer o no
// los datos, D39) miran además rawStatus directamente.
enum class JobState {
    Queued,      // en espera
    Spooling,    // la aplicación todavía está enviando datos
    Printing,    // imprimiendo
    Paused,      // pausado por un usuario/operador
    Error,       // error, sin papel, atasco, intervención requerida...
    Offline,     // impresora fuera de línea
    Deleting,    // cancelándose
    Printed,     // terminado (si el spooler conserva los documentos)
    Restarting,  // reiniciándose
    Unknown,     // ningún flag reconocido (se muestra como "en cola" o sin estado)
};

// Problemas de hardware de la impresora (derivados de PRINTER_STATUS_*).
// Es una máscara de bits: una impresora puede estar a la vez sin papel y en
// pausa, por ejemplo. core::PrinterProblemsFromFlags la calcula a partir de
// PRINTER_INFO_2.Status y la UI la traduce a texto/ícono.
enum PrinterProblem : std::uint32_t {
    ProblemNone = 0,                 // sin problemas: operativa
    ProblemOffline = 1u << 0,        // fuera de línea (PRINTER_STATUS_OFFLINE)
    ProblemPaperJam = 1u << 1,       // atasco de papel
    ProblemPaperOut = 1u << 2,       // sin papel
    ProblemTonerLow = 1u << 3,       // tóner/tinta bajo (solo aviso)
    ProblemNoToner = 1u << 4,        // sin tóner/tinta
    ProblemDoorOpen = 1u << 5,       // puerta/tapa abierta
    ProblemUserIntervention = 1u << 6,  // requiere intervención del usuario
    ProblemError = 1u << 7,          // error genérico de la impresora
    ProblemPaused = 1u << 8,  // la impresora (cola completa) está en pausa
    // No disponible: PRINTER_STATUS_NOT_AVAILABLE/SERVER_UNKNOWN, o el monitor no
    // pudo abrir/consultar la cola (servidor caído, sin permisos, RPC).
    // El JobTracker conserva los trabajos de una impresora así hasta que
    // vuelva (D52) y PrintRelease no decide nada sobre ella.
    ProblemNotAvailable = 1u << 9,
    ProblemOutputBinFull = 1u << 10,  // bandeja de salida llena
    ProblemPaperProblem = 1u << 11,   // problema de papel (tamaño/tipo incorrecto)
    ProblemOutOfMemory = 1u << 12,    // la impresora se quedó sin memoria
};

// Papel que ofrece el controlador (DeviceCapabilities DC_PAPERS / DC_PAPERNAMES / DC_PAPERSIZE).
// Se usa para nombrar los papeles propios del controlador (códigos >= 256, que
// no significan nada fuera de ese controlador) por sus medidas, para que
// coincidan con lo que dicen los datos del trabajo (D34).
struct DriverPaper {
    short code = 0;       // DMPAPER_* o código propio del controlador (>= 256)
    std::wstring name;    // "12 x 18 pulg. (305 x 457 mm)"
    short width = 0;      // décimas de mm
    short length = 0;     // décimas de mm (alto del papel en vertical)
};

// Una impresora (cola) tal como se vio en una instantánea. Sale de
// PRINTER_INFO_2 (EnumPrinters/GetPrinter) más datos del controlador que el
// monitor pide una sola vez por cola (bandejas y papeles, D34).
struct PrinterInfo {
    std::wstring name;      // nombre completo usable con OpenPrinter ("\\\\SRV\\HP-Lab1" o "HP-Lab1")
    std::wstring server;    // "" = local
    std::wstring shareName;  // nombre con que se comparte en el servidor (pShareName)
    std::wstring driver;     // nombre del controlador (pDriverName)
    std::wstring port;       // puerto (pPortName): IP, USB001, FILE:...
    std::wstring location;   // ubicación configurada por el administrador (pLocation)
    std::wstring comment;    // comentario de la cola (pComment)
    std::uint32_t rawStatus = 0;  // PRINTER_STATUS_*
    std::uint32_t problems = ProblemNone;  // máscara PrinterProblem derivada de rawStatus
    // Trabajos que el spooler dice tener en la cola (PRINTER_INFO_2.cJobs). Se
    // compara con los enumerados para detectar trabajos que no se ven (D21) y
    // para saber si la lista de una impresora está completa (D35).
    std::uint32_t jobCount = 0;
    std::wstring printProcessor;  // procesador de impresión de la cola ("winprint"...)
    std::wstring datatype;        // tipo de datos predeterminado de la cola
    // DEVMODE predeterminado de la cola (PRINTER_INFO_2.pDevMode): solo para
    // compararlo con el del trabajo, nunca para sustituirlo.
    PrintSettings defaults;
    // Nombres que da el controlador a sus bandejas y papeles (en el idioma de
    // Windows): "Auto selección", "Bandeja de papel 1"... Vacío si no se pudieron leer.
    std::vector<std::pair<short, std::wstring>> bins;
    std::vector<DriverPaper> papers;  // papeles del controlador (vacío si no se pudieron leer)
    bool isDefault = false;  // es la impresora predeterminada de este usuario/equipo
    bool isShared = false;   // la cola está compartida (PRINTER_ATTRIBUTE_SHARED)

    // Nombre corto para mostrar (sin el prefijo del servidor).
    // Ejemplo: "\\\\SRV\\HP-Lab1" -> "HP-Lab1". Si el nombre no empieza por
    // dos barras se devuelve tal cual (impresora local).
    std::wstring DisplayName() const;
};

// Un trabajo de impresión tal como se vio en una instantánea (JOB_INFO_2 de
// EnumJobs, más la marca de retención de la app). Es inmutable dentro de la
// instantánea; el seguimiento entre instantáneas lo hace core::JobTracker.
struct JobInfo {
    std::uint32_t id = 0;  // JobId del spooler (único dentro de la impresora/servidor)
    std::wstring printer;  // PrinterInfo::name de la impresora a la que pertenece
    std::wstring document;  // nombre del documento (pDocument), el que puso la aplicación
    std::wstring user;      // usuario que lo envió (pUserName), sin dominio por lo general
    std::wstring machine;   // equipo de origen (pMachineName), normalmente "\\\\PC"
    // "RAW", "NT EMF 1.008", "XPS_PASS"... Ojo: no dice el formato real (un
    // driver v4 declara RAW y envía OpenXPS); el formato se detecta por contenido.
    std::wstring datatype;    // "RAW", "NT EMF 1.008", "XPS_PASS"...
    std::wstring driver;      // controlador con que se generó (pDriverName)
    std::wstring printProcessor;  // pPrintProcessor ("winprint", "MS_XPS_PROC"...)
    std::wstring parameters;      // pParameters
    std::wstring statusText;  // texto de estado proporcionado por el monitor de puerto (pStatus)
    std::uint32_t rawStatus = 0;  // JOB_STATUS_*
    std::uint32_t priority = 1;   // 1..99
    std::uint32_t position = 0;   // posición en la cola (1 = siguiente)
    // Páginas que cuenta el spooler (JOB_INFO_2.TotalPages). No es fiable: baja
    // mientras se imprime y en ESC/Page incluye el dorso en blanco (D46).
    std::uint32_t totalPages = 0;
    std::uint32_t pagesPrinted = 0;  // páginas ya impresas (PagesPrinted)
    // Tamaño de los datos en bytes (JOB_INFO_2.Size). También baja al imprimirse
    // (llega a 0 con "Microsoft Print to PDF"): no sirve para reconocer el trabajo.
    std::uint64_t sizeBytes = 0;
    SYSTEMTIME submitted{};  // UTC
    // Configuración pedida al imprimir: DEVMODE DEL TRABAJO (JOB_INFO_2.pDevMode).
    // En EMF puede no ser el que se aplica (Word cambia el DEVMODE tras
    // StartDoc, D23): para lo efectivo usar core::EffectiveDevMode.
    PrintSettings settings;
    JobState state = JobState::Unknown;  // estado simplificado (JobStateFromFlags de rawStatus)
    bool heldByApp = false;  // pausado automáticamente por la retención de la app

    // Clave única estable dentro de un servidor (impresora + id).
    // Formato "<impresora>#<id>". No incluye tamaño ni estado a propósito: esos
    // valores cambian mientras el trabajo vive (ver GUIA_IA §6).
    std::wstring Key() const;
};

// Estado completo de un servidor en un instante. Inmutable una vez publicado:
// el monitor crea una nueva instantánea en cada refresco y la UI solo la lee.
// Se comparte como SnapshotPtr (shared_ptr a const) para que varios
// consumidores (tabla, tracker, vista previa) la usen sin copiarla.
struct QueueSnapshot {
    std::wstring server;  // servidor consultado ("\\\\SRV") o "" para este equipo
    std::vector<PrinterInfo> printers;
    std::vector<JobInfo> jobs;  // todos los trabajos de todas las impresoras
    std::wstring error;         // error global (p. ej. servidor inaccesible)
    // Número creciente de la instantánea dentro de un monitor: permite a la UI
    // descartar resultados de una instantánea anterior.
    std::uint64_t sequence = 0;
    bool notificationsActive = false;  // true si FindFirstPrinterChangeNotification funciona
    bool autoHold = false;             // retención automática activa
    std::uint32_t holdFailures = 0;    // trabajos nuevos que no se pudieron pausar (acumulado)
    unsigned long holdError = 0;       // último error Win32 al intentar retener
    // Trabajos que el spooler cuenta en sus colas pero no se pudieron enumerar
    // (la cola de Windows los muestra y la app no): se avisa en la barra de estado.
    std::uint32_t missingJobs = 0;
    std::uint32_t unreachablePrinters = 0;  // impresoras que no se pudieron abrir

    // Busca una impresora por su nombre completo (PrinterInfo::name, comparación
    // exacta). Devuelve un puntero dentro de esta instantánea (válido mientras
    // viva) o nullptr si no está. Búsqueda lineal: hay pocas impresoras.
    const PrinterInfo* FindPrinter(const std::wstring& name) const;
};

// Puntero compartido a una instantánea inmutable: el monitor la crea y la
// publica; la UI y los servicios la conservan mientras la necesiten.
using SnapshotPtr = std::shared_ptr<const QueueSnapshot>;

// JOB_STATUS_RENDERING_LOCALLY (winspool.h de Windows 7+; falta en MinGW):
// trabajo que esta PC todavía procesa (Client Side Rendering).
inline constexpr std::uint32_t kJobStatusRenderingLocally = 0x00004000;

// Concilia la lista consultada al servidor (la verdadera) con la de la caché
// del cliente: de la caché solo se agregan los trabajos que esta PC todavía
// procesa localmente y el servidor aún no muestra; los demás que solo estén en
// la caché son restos viejos y se descartan. Ordenado por posición.
// Contexto (D28): por el nombre de la conexión Windows responde desde la caché
// de Client Side Rendering y puede no mostrar trabajos de otras PCs; por eso
// manda la lista pedida con el nombre DNS completo. La copia local
// (kJobStatusRenderingLocally) solo existe en la caché y se muestra mientras
// esta PC la envía, para que el operador vea que algo está saliendo.
// Parámetros: server (por valor, se devuelve modificado) = lista del servidor;
// cached = lista de la caché del cliente. Los trabajos se identifican por id.
std::vector<JobInfo> ReconcileJobLists(std::vector<JobInfo> server, const std::vector<JobInfo>& cached);

// Qué lista de trabajos usar en un refresco en que NO se obtuvo la del
// servidor (la consulta directa falló o su handle espera para reabrirse), F6.
enum class JobListFallback {
    // Nunca hubo lista del servidor para esta impresora: la caché del cliente,
    // como siempre (colas locales o DNS que todavía no responde).
    Cache,
    // Ya se tenía una lista buena del servidor: se conserva la del refresco
    // anterior y NO se ejecuta la retención en este refresco. La caché puede
    // no tener todos los trabajos (D28) y usarla hacía que el historial los
    // diera por terminados y que la retención los volviera a pausar al volver.
    KeepPrevious,
    // La consulta directa lleva varios fallos seguidos: se muestra la caché,
    // pero con la impresora marcada ProblemNotAvailable (JobTracker y
    // PrintReleaseTracker saben esperar en ese estado, D52) y la retención en
    // modo "lista parcial" (no olvida los trabajos que no ve).
    CacheDegraded,
};

// Fallos seguidos de la consulta directa a partir de los cuales se deja de
// mostrar la lista anterior (unos 15 s con el reintento cada 5 s).
inline constexpr int kDirectFailuresToDegrade = 3;

// Decisión pura de JobListFallback.
//  hadServerList:       alguna vez se obtuvo la lista del servidor para esta impresora.
//  consecutiveFailures: intentos fallidos seguidos de la consulta directa.
JobListFallback ChooseJobListFallback(bool hadServerList, int consecutiveFailures);

}  // namespace cola::core
