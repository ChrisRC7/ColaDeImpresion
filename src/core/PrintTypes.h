#pragma once
// Tipos de datos del dominio: impresoras, trabajos e instantáneas de la cola.
// No dependen de la UI; los produce core/Spooler y los consume el resto.

#include <windows.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/PrintSettings.h"

namespace cola::core {

// Estado simplificado de un trabajo (derivado de los flags JOB_STATUS_*).
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
    Unknown,
};

// Problemas de hardware de la impresora (derivados de PRINTER_STATUS_*).
enum PrinterProblem : std::uint32_t {
    ProblemNone = 0,
    ProblemOffline = 1u << 0,
    ProblemPaperJam = 1u << 1,
    ProblemPaperOut = 1u << 2,
    ProblemTonerLow = 1u << 3,
    ProblemNoToner = 1u << 4,
    ProblemDoorOpen = 1u << 5,
    ProblemUserIntervention = 1u << 6,
    ProblemError = 1u << 7,
    ProblemPaused = 1u << 8,  // la impresora (cola completa) está en pausa
    ProblemNotAvailable = 1u << 9,
    ProblemOutputBinFull = 1u << 10,
    ProblemPaperProblem = 1u << 11,
    ProblemOutOfMemory = 1u << 12,
};

struct PrinterInfo {
    std::wstring name;      // nombre completo usable con OpenPrinter ("\\\\SRV\\HP-Lab1" o "HP-Lab1")
    std::wstring server;    // "" = local
    std::wstring shareName;
    std::wstring driver;
    std::wstring port;
    std::wstring location;
    std::wstring comment;
    std::uint32_t rawStatus = 0;  // PRINTER_STATUS_*
    std::uint32_t problems = ProblemNone;
    std::uint32_t jobCount = 0;
    std::wstring printProcessor;  // procesador de impresión de la cola ("winprint"...)
    std::wstring datatype;        // tipo de datos predeterminado de la cola
    // DEVMODE predeterminado de la cola (PRINTER_INFO_2.pDevMode): solo para
    // compararlo con el del trabajo, nunca para sustituirlo.
    PrintSettings defaults;
    bool isDefault = false;
    bool isShared = false;

    // Nombre corto para mostrar (sin el prefijo del servidor).
    std::wstring DisplayName() const;
};

struct JobInfo {
    std::uint32_t id = 0;
    std::wstring printer;  // PrinterInfo::name de la impresora a la que pertenece
    std::wstring document;
    std::wstring user;
    std::wstring machine;
    std::wstring datatype;    // "RAW", "NT EMF 1.008", "XPS_PASS"...
    std::wstring driver;
    std::wstring printProcessor;  // pPrintProcessor ("winprint", "MS_XPS_PROC"...)
    std::wstring parameters;      // pParameters
    std::wstring statusText;  // texto de estado proporcionado por el monitor de puerto (pStatus)
    std::uint32_t rawStatus = 0;  // JOB_STATUS_*
    std::uint32_t priority = 1;   // 1..99
    std::uint32_t position = 0;   // posición en la cola (1 = siguiente)
    std::uint32_t totalPages = 0;
    std::uint32_t pagesPrinted = 0;
    std::uint64_t sizeBytes = 0;
    SYSTEMTIME submitted{};  // UTC
    // Configuración pedida al imprimir: DEVMODE DEL TRABAJO (JOB_INFO_2.pDevMode).
    PrintSettings settings;
    JobState state = JobState::Unknown;
    bool heldByApp = false;  // pausado automáticamente por la retención de la app

    // Clave única estable dentro de un servidor (impresora + id).
    std::wstring Key() const;
};

// Estado completo de un servidor en un instante. Inmutable una vez publicado:
// el monitor crea una nueva instantánea en cada refresco y la UI solo la lee.
struct QueueSnapshot {
    std::wstring server;
    std::vector<PrinterInfo> printers;
    std::vector<JobInfo> jobs;  // todos los trabajos de todas las impresoras
    std::wstring error;         // error global (p. ej. servidor inaccesible)
    std::uint64_t sequence = 0;
    bool notificationsActive = false;  // true si FindFirstPrinterChangeNotification funciona
    bool autoHold = false;             // retención automática activa
    std::uint32_t holdFailures = 0;    // trabajos nuevos que no se pudieron pausar (acumulado)
    unsigned long holdError = 0;       // último error Win32 al intentar retener
    // Trabajos que el spooler cuenta en sus colas pero no se pudieron enumerar
    // (la cola de Windows los muestra y la app no): se avisa en la barra de estado.
    std::uint32_t missingJobs = 0;
    std::uint32_t unreachablePrinters = 0;  // impresoras que no se pudieron abrir

    const PrinterInfo* FindPrinter(const std::wstring& name) const;
};

using SnapshotPtr = std::shared_ptr<const QueueSnapshot>;

// JOB_STATUS_RENDERING_LOCALLY (winspool.h de Windows 7+; falta en MinGW):
// trabajo que esta PC todavía procesa (Client Side Rendering).
inline constexpr std::uint32_t kJobStatusRenderingLocally = 0x00004000;

// Concilia la lista consultada al servidor (la verdadera) con la de la caché
// del cliente: de la caché solo se agregan los trabajos que esta PC todavía
// procesa localmente y el servidor aún no muestra; los demás que solo estén en
// la caché son restos viejos y se descartan. Ordenado por posición.
std::vector<JobInfo> ReconcileJobLists(std::vector<JobInfo> server, const std::vector<JobInfo>& cached);

}  // namespace cola::core
