#pragma once
// Envoltorio delgado sobre la API del spooler de Windows (winspool).
// Todas las funciones son síncronas y pueden tardar con servidores remotos:
// llamarlas SIEMPRE desde un hilo de fondo (QueueMonitor / WorkQueue), nunca
// desde el hilo de la UI.

#include <windows.h>
#include <winspool.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/PrintTypes.h"

namespace cola::core {

// Resultado de una operación sobre el spooler.
struct OpResult {
    bool ok = true;
    DWORD error = ERROR_SUCCESS;

    static OpResult Success() { return {}; }
    static OpResult Fail(DWORD err) { return {false, err ? err : ERROR_GEN_FAILURE}; }
    static OpResult FromLastError() { return Fail(GetLastError()); }
};

// RAII para HANDLE de impresora.
class PrinterHandle {
public:
    PrinterHandle() = default;
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
    bool Open(const std::wstring& name, bool admin);
    // Intenta abrir con permisos de administración y, si no, de uso normal.
    bool OpenBest(const std::wstring& name, bool* gotAdmin = nullptr);
    void Close();

    HANDLE get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HANDLE handle_ = nullptr;
};

namespace spooler {

// Normaliza el nombre del servidor: "srv" -> "\\\\srv", "" -> "" (local).
std::wstring NormalizeServerName(const std::wstring& server);

// true si el nombre apunta a este mismo equipo ("" , "localhost", el nombre
// NetBIOS o el DNS del equipo). Se usa para no pasar por \\\\SRV\\C$ cuando la
// app corre en el propio servidor de impresión.
bool IsLocalServer(const std::wstring& server);

// Impresoras del servidor (vacío = locales + conexiones del equipo).
// Con un servidor remoto se combinan tres fuentes para no perder colas:
// las compartidas del servidor, las conexiones de este equipo a ese servidor y,
// si el servidor es este mismo equipo, también sus colas locales no compartidas.
std::vector<PrinterInfo> EnumeratePrinters(const std::wstring& server, DWORD* error = nullptr);

// Datos actualizados de una impresora a partir de un handle abierto.
bool QueryPrinter(HANDLE printer, const std::wstring& server, PrinterInfo& out);

// Trabajos de una impresora (handle ya abierto).
std::vector<JobInfo> EnumerateJobs(HANDLE printer, const std::wstring& printerName, std::uint32_t printerProblems,
                                   DWORD* error = nullptr);

// Acciones sobre trabajos.
OpResult PauseJob(const std::wstring& printer, std::uint32_t jobId);
OpResult ResumeJob(const std::wstring& printer, std::uint32_t jobId);
OpResult CancelJob(const std::wstring& printer, std::uint32_t jobId);
OpResult RestartJob(const std::wstring& printer, std::uint32_t jobId);
OpResult SetJobPosition(const std::wstring& printer, std::uint32_t jobId, std::uint32_t position);
OpResult SetJobPriority(const std::wstring& printer, std::uint32_t jobId, std::uint32_t priority);

// Acciones sobre la impresora completa (requieren permisos de administración).
OpResult PausePrinter(const std::wstring& printer);
OpResult ResumePrinter(const std::wstring& printer);
OpResult PurgePrinter(const std::wstring& printer);

// true si el usuario actual puede administrar la impresora (trabajos de otros).
bool CanAdminister(const std::wstring& printer);

// Carpeta de spool configurada en el servidor (p. ej. "C:\\Windows\\System32\\spool\\PRINTERS").
std::wstring GetSpoolDirectory(const std::wstring& server);

// Carpeta de spool propia de una cola de impresión (valor "SpoolDirectory" de
// la impresora). Devuelve "" si la cola usa la carpeta general del servidor.
std::wstring GetPrinterSpoolDirectory(const std::wstring& printer);

std::wstring GetDefaultPrinterName();

}  // namespace spooler
}  // namespace cola::core
