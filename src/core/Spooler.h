#pragma once
// Envoltorio delgado sobre la API del spooler de Windows (winspool).
// Todas las funciones son síncronas y pueden tardar con servidores remotos:
// llamarlas SIEMPRE desde un hilo de fondo (QueueMonitor / WorkQueue), nunca
// desde el hilo de la UI.
//
// Es la única puerta de la app hacia winspool (D3: la app controla el spooler,
// no lo reemplaza). Agrupa:
//  - PrinterHandle: RAII para OpenPrinter/ClosePrinter con el acceso adecuado.
//  - Enumeración de impresoras y trabajos (EnumPrinters / EnumJobs nivel 2),
//    copiados a los tipos planos de PrintTypes.h.
//  - Acciones (SetJob / SetPrinter): pausar, reanudar, cancelar, reiniciar,
//    mover y cambiar prioridad.
//  - Lectura de los datos de un trabajo por la API (ReadJobData, D22) con las
//    protecciones de D32/D39 para no competir con el procesador de impresión.
//  - El nombre DNS completo para saltarse la caché de Client Side Rendering
//    (CacheBypassName, D28/D51).
//
// Permisos: ver el estado de la cola basta con PRINTER_ACCESS_USE; actuar sobre
// trabajos de otros usuarios exige "Administrar documentos" y pausar o purgar
// la impresora exige "Administrar esta impresora" (D15, D18). Los errores se
// devuelven como código Win32 (OpResult / DWORD*) para que la UI los explique.
//
// Ojo con las macros de winspool.h (SetPrinter, GetJob...): por eso las
// funciones propias tienen otros nombres (PausePrinter, ControlJob...).

#include <windows.h>
#include <winspool.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/PrintTypes.h"

namespace cola::core {

// Resultado de una operación sobre el spooler.
struct OpResult {
    bool ok = true;               // la operación se completó
    DWORD error = ERROR_SUCCESS;  // código Win32 si falló (p. ej. ERROR_ACCESS_DENIED)

    // Éxito.
    static OpResult Success() { return {}; }
    // Fallo con un código; si llega 0 (GetLastError sin valor) se usa
    // ERROR_GEN_FAILURE para que nunca haya un "fallo sin error".
    static OpResult Fail(DWORD err) { return {false, err ? err : ERROR_GEN_FAILURE}; }
    // Fallo con el último error del hilo: llamar justo después de la API que falló.
    static OpResult FromLastError() { return Fail(GetLastError()); }
};

// RAII para HANDLE de impresora.
// Cierra con ClosePrinter al destruirse. No copiable (un handle no se puede
// cerrar dos veces), sí movible. Sirve para handles de impresora, de servidor
// y de trabajo ("Cola,Job N").
class PrinterHandle {
public:
    PrinterHandle() = default;
    // Toma posesión de un handle ya abierto con OpenPrinterW.
    explicit PrinterHandle(HANDLE h) : handle_(h) {}
    ~PrinterHandle() { Close(); }
    PrinterHandle(const PrinterHandle&) = delete;
    PrinterHandle& operator=(const PrinterHandle&) = delete;
    PrinterHandle(PrinterHandle&& o) noexcept : handle_(o.handle_) { o.handle_ = nullptr; }
    PrinterHandle& operator=(PrinterHandle&& o) noexcept {
        if (this != &o) {
            Close();
            handle_ = o.handle_;
            o.handle_ = nullptr;
        }
        return *this;
    }

    // name vacío = servidor local. "\\\\SRV" = servidor remoto. Otro = impresora.
    // admin = pide PRINTER_ACCESS_ADMINISTER (necesario para trabajos de otros usuarios).
    // Cierra antes el handle que tuviera. Devuelve false si OpenPrinterW falla
    // (el motivo queda en GetLastError, p. ej. ERROR_ACCESS_DENIED).
    bool Open(const std::wstring& name, bool admin);
    // Intenta abrir con permisos de administración y, si no, de uso normal.
    // gotAdmin (opcional) indica con cuál se logró. Con acceso de uso todavía
    // se pueden controlar los trabajos propios y los de otros si se tiene
    // "Administrar documentos" (el spooler lo comprueba en cada SetJob).
    bool OpenBest(const std::wstring& name, bool* gotAdmin = nullptr);
    // Cierra el handle si está abierto (se puede llamar varias veces).
    void Close();

    // Handle crudo para pasar a las funciones de winspool (sigue siendo de este objeto).
    HANDLE get() const { return handle_; }
    // true si hay un handle abierto.
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HANDLE handle_ = nullptr;  // nullptr = cerrado
};

namespace spooler {

// Normaliza el nombre del servidor: "srv" -> "\\\\srv", "" -> "" (local).
// También acepta lo que escribe un operador: quita espacios y barras finales,
// convierte "/" en barras invertidas y trata "." y "localhost" como locales.
std::wstring NormalizeServerName(const std::wstring& server);

// true si el nombre apunta a este mismo equipo ("" , "localhost", el nombre
// NetBIOS o el DNS del equipo). Se usa para no pasar por \\\\SRV\\C$ cuando la
// app corre en el propio servidor de impresión.
bool IsLocalServer(const std::wstring& server);

// Nombre para consultar una impresora de servidor SIN la caché del cliente.
//
// Con una conexión a una impresora compartida ("\\\\vtec-laimiprint\\Cola"),
// Windows responde EnumJobs/GetPrinter desde la caché local de Client Side
// Rendering (medido: 0,6 ms, sin ida y vuelta al servidor), y esa caché a veces
// no recibe los trabajos enviados desde otras PCs hasta el siguiente evento.
// PRINTER_OPTION_NO_CACHE no lo evita. Abrir la misma cola con el nombre DNS
// completo del servidor ("\\\\vtec-laimiprint.dominio\\Cola"), que no es la
// conexión guardada, sí consulta al servidor (17 ms). Devuelve "" si no hay un
// nombre alternativo (servidor local o la conexión ya usa el nombre completo).
// La resolución DNS se hace una vez por servidor y se recuerda (también el
// fallo, durante 60 s: D51). Thread-safe (mutex interno). Las acciones y la
// retención siguen usando el nombre de la conexión (D28).
std::wstring CacheBypassName(const std::wstring& printerName);

// "\\\\srv\\Cola" + "srv.dominio" -> "\\\\srv.dominio\\Cola" ("" si el nombre no es de servidor).
// También "" si newHost está vacío o el nombre no tiene parte de impresora.
std::wstring ReplacePrinterServer(const std::wstring& printerName, const std::wstring& newHost);

// Impresoras del servidor (vacío = locales + conexiones del equipo).
// Con un servidor remoto se combinan tres fuentes para no perder colas:
// las compartidas del servidor, las conexiones de este equipo a ese servidor y,
// si el servidor es este mismo equipo, también sus colas locales no compartidas.
// Resultado ordenado por nombre corto (sin distinguir mayúsculas) y sin
// duplicados. error (opcional) = código de la enumeración principal, solo si
// no se obtuvo ninguna impresora; ERROR_SUCCESS en cualquier otro caso.
std::vector<PrinterInfo> EnumeratePrinters(const std::wstring& server, DWORD* error = nullptr);

// Bandejas y papeles que declara el controlador de la cola (DeviceCapabilities).
// Necesita el controlador instalado en este equipo (lo está en las conexiones a
// impresoras compartidas). Puede tardar la primera vez: llamar en segundo plano.
// bins = pares (código DMBIN_*, nombre); papers = papeles con medidas. Ambas
// listas se vacían al empezar. Devuelve true si se obtuvo al menos una de las dos
// (D34: el monitor lo pide una vez por cola).
bool GetDriverForms(const std::wstring& printer, std::vector<std::pair<short, std::wstring>>& bins,
                    std::vector<DriverPaper>& papers);

// Datos actualizados de una impresora a partir de un handle abierto.
// GetPrinter nivel 2 sobre 'printer'; 'server' es el servidor normalizado (para
// completar el nombre si el controlador lo devuelve sin prefijo). Conserva
// out.isDefault (GetPrinter no lo informa). false si GetPrinter falla.
bool QueryPrinter(HANDLE printer, const std::wstring& server, PrinterInfo& out);

// Trabajos de una impresora (handle ya abierto).
// EnumJobs nivel 2 de todos los trabajos (0..0xFFFF). printerName se copia en
// JobInfo::printer; printerProblems se usa para calcular JobInfo::state. Si
// falla devuelve vacío y deja el código en *error (si se pasó).
std::vector<JobInfo> EnumerateJobs(HANDLE printer, const std::wstring& printerName, std::uint32_t printerProblems,
                                   DWORD* error = nullptr);

// Acciones sobre trabajos.
// Cada una abre su propio handle (OpenBest) con el nombre de la conexión y
// llama a SetJob. Requieren ser dueño del trabajo o tener "Administrar
// documentos". Ninguna comprueba IsLocalCsrCopy: eso lo hace JobActionRunner
// (D50) y la retención (D39) antes de llamar.
// Pausa (JOB_CONTROL_PAUSE).
OpResult PauseJob(const std::wstring& printer, std::uint32_t jobId);
// Reanuda (JOB_CONTROL_RESUME); espera a que termine/se corte una lectura
// ReadJobData del mismo trabajo (D39).
OpResult ResumeJob(const std::wstring& printer, std::uint32_t jobId);
// Cancela y elimina el trabajo (JOB_CONTROL_DELETE).
OpResult CancelJob(const std::wstring& printer, std::uint32_t jobId);
// Reinicia la impresión desde el principio (JOB_CONTROL_RESTART); igual que
// ResumeJob, espera a una lectura en curso.
OpResult RestartJob(const std::wstring& printer, std::uint32_t jobId);
// Mueve el trabajo a 'position' (1 = el siguiente; 0 se trata como 1). Usa
// SetJob nivel 1; reordenar suele exigir permisos de administración.
OpResult SetJobPosition(const std::wstring& printer, std::uint32_t jobId, std::uint32_t position);
// Cambia la prioridad (se limita a MIN_PRIORITY..MAX_PRIORITY = 1..99).
OpResult SetJobPriority(const std::wstring& printer, std::uint32_t jobId, std::uint32_t priority);

// Acciones sobre la impresora completa (requieren permisos de administración).
// Abren la impresora con PRINTER_ACCESS_ADMINISTER y llaman a SetPrinter con
// el comando de control; sin "Administrar esta impresora" fallan con
// ERROR_ACCESS_DENIED (D18: por eso la retención de trabajos es la defensa
// principal y la pausa de impresora es complementaria).
OpResult PausePrinter(const std::wstring& printer);
OpResult ResumePrinter(const std::wstring& printer);
// Elimina TODOS los trabajos de la cola (PRINTER_CONTROL_PURGE).
OpResult PurgePrinter(const std::wstring& printer);

// true si el usuario actual puede administrar la impresora (trabajos de otros).
// Lo prueba abriendo con acceso de administración y cerrando enseguida.
bool CanAdminister(const std::wstring& printer);

// Lee los datos de un trabajo con la API documentada del spooler:
// OpenPrinter("Impresora,Job N") + ReadPrinter. No toca la carpeta de spool,
// no necesita ser administrador del servidor ni acceso a \\SRV\C$, y no choca
// con los bloqueos que el spooler mantiene sobre los .SHD/.SPL: el spooler
// comprueba JOB_ACCESS_READ, que tienen el dueño del trabajo y quien tiene
// "Administrar documentos" en la cola. Funciona igual con servidores remotos
// (RPC). Lee como máximo maxBytes; *truncated indica si quedaron datos.
// IMPORTANTE: con un trabajo que todavía se está recibiendo (JOB_STATUS_SPOOLING)
// ReadPrinter espera a que lleguen más datos: no llamarla en ese caso. Las
// lecturas se serializan dentro del proceso: si dos coinciden en el tiempo, la
// segunda recibe 0 bytes. Si otro programa lee el mismo trabajo a la vez puede
// pasar lo mismo (ver el reintento de preview::ReadJobSpool), y el procesador
// de impresión es "otro programa": solo se lee si el estado real del trabajo
// (consultado al servidor antes y durante la lectura) cumple CanReadJobData;
// si no, o si lo reanudan a mitad, devuelve ERROR_BUSY. ResumeJob/RestartJob
// esperan a que termine o se corte una lectura del mismo trabajo.
// beyond: si se da, lo que pasa de maxBytes no se descarta sino que se le
// entrega por bloques sin guardarlo (con la misma comprobación de pausa antes
// de cada bloque); devolver false deja de leer. Si lo reanudan mientras tanto
// se deja de leer pero 'out' sigue valiendo (truncated = true).
// Devuelve: Success con 'out' lleno (posiblemente truncado), ERROR_BUSY si no
// era seguro leer o lo reanudaron a mitad (out vacío), o el error de
// OpenPrinter/ReadPrinter. Decisiones: D22, D32, D39, D48, D53.
// DataSink: recibe cada bloque que pasa de maxBytes; true = seguir leyendo.
using DataSink = std::function<bool(const std::uint8_t* data, size_t size)>;
OpResult ReadJobData(const std::wstring& printer, std::uint32_t jobId, std::uint64_t maxBytes,
                     std::vector<std::uint8_t>& out, bool* truncated, const DataSink& beyond = {});

// Carpeta de spool configurada en el servidor (p. ej. "C:\\Windows\\System32\\spool\\PRINTERS").
// Es una ruta local DEL SERVIDOR: preview/ la convierte en \\SRV\C$\... o
// \\SRV\ADMIN$\... para el respaldo por archivo (D6). "" si no se pudo leer
// (GetPrinterData con SPLREG_DEFAULT_SPOOL_DIRECTORY sobre el handle del servidor).
std::wstring GetSpoolDirectory(const std::wstring& server);

// Carpeta de spool propia de una cola de impresión (valor "SpoolDirectory" de
// la impresora). Devuelve "" si la cola usa la carpeta general del servidor.
std::wstring GetPrinterSpoolDirectory(const std::wstring& printer);

// Impresora predeterminada del usuario actual en este equipo (GetDefaultPrinter),
// o "" si no hay. Se usa para marcar PrinterInfo::isDefault en la vista local.
std::wstring GetDefaultPrinterName();

}  // namespace spooler
}  // namespace cola::core
