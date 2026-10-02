#include "core/Spooler.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <map>
#include <mutex>

#include "core/JobStatus.h"
#include "util/Log.h"
#include "util/Strings.h"

namespace cola::core {

bool PrinterHandle::Open(const std::wstring& name, bool admin) {
    Close();
    PRINTER_DEFAULTSW defaults{};
    if (name.empty() || (name.size() > 2 && name.rfind(L"\\\\", 0) == 0 && name.find(L'\\', 2) == std::wstring::npos)) {
        // Handle de servidor.
        defaults.DesiredAccess = admin ? SERVER_ACCESS_ADMINISTER : SERVER_ACCESS_ENUMERATE;
    } else {
        defaults.DesiredAccess = admin ? (PRINTER_ACCESS_ADMINISTER | PRINTER_ACCESS_USE) : PRINTER_ACCESS_USE;
    }
    HANDLE h = nullptr;
    LPWSTR target = name.empty() ? nullptr : const_cast<LPWSTR>(name.c_str());
    if (!OpenPrinterW(target, &h, &defaults)) return false;
    handle_ = h;
    return true;
}

bool PrinterHandle::OpenBest(const std::wstring& name, bool* gotAdmin) {
    if (Open(name, true)) {
        if (gotAdmin) *gotAdmin = true;
        return true;
    }
    if (gotAdmin) *gotAdmin = false;
    return Open(name, false);
}

void PrinterHandle::Close() {
    if (handle_) {
        ClosePrinter(handle_);
        handle_ = nullptr;
    }
}

namespace spooler {

namespace {

std::wstring Str(const wchar_t* s) { return s ? std::wstring(s) : std::wstring(); }

// Llama a una función del estilo "consulta tamaño + llena buffer" de winspool.
template <typename Fn>
bool FillBuffer(std::vector<BYTE>& buffer, Fn&& call) {
    DWORD needed = 0;
    if (call(nullptr, 0, &needed)) return true;
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        buffer.resize(needed);
        if (call(buffer.data(), needed, &needed)) return true;
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
    }
    return false;
}

void FillPrinterInfo(const PRINTER_INFO_2W& src, const std::wstring& server, const std::wstring& defaultName,
                     PrinterInfo& out) {
    out.name = Str(src.pPrinterName);
    // Algunos drivers devuelven el nombre sin el prefijo del servidor.
    if (!server.empty() && out.name.rfind(L"\\\\", 0) != 0) out.name = server + L"\\" + out.name;
    out.server = server;
    out.shareName = Str(src.pShareName);
    out.driver = Str(src.pDriverName);
    out.port = Str(src.pPortName);
    out.location = Str(src.pLocation);
    out.comment = Str(src.pComment);
    out.rawStatus = src.Status;
    out.problems = PrinterProblemsFromFlags(src.Status, src.Attributes);
    out.jobCount = src.cJobs;
    out.printProcessor = Str(src.pPrintProcessor);
    out.datatype = Str(src.pDatatype);
    out.defaults = ParseDevMode(src.pDevMode);
    out.isShared = (src.Attributes & PRINTER_ATTRIBUTE_SHARED) != 0;
    out.isDefault = !defaultName.empty() && _wcsicmp(out.name.c_str(), defaultName.c_str()) == 0;
}

OpResult ControlJob(const std::wstring& printer, std::uint32_t jobId, DWORD command) {
    PrinterHandle h;
    if (!h.OpenBest(printer)) return OpResult::FromLastError();
    if (!SetJobW(h.get(), jobId, 0, nullptr, command)) return OpResult::FromLastError();
    return OpResult::Success();
}

OpResult ControlPrinter(const std::wstring& printer, DWORD command) {
    PrinterHandle h;
    if (!h.Open(printer, true)) return OpResult::FromLastError();
    if (!SetPrinterW(h.get(), 0, nullptr, command)) return OpResult::FromLastError();
    return OpResult::Success();
}

// Modifica un trabajo usando el nivel 1 (posición/prioridad).
template <typename Mutator>
OpResult ModifyJobLevel1(const std::wstring& printer, std::uint32_t jobId, Mutator&& mutate) {
    PrinterHandle h;
    if (!h.OpenBest(printer)) return OpResult::FromLastError();
    std::vector<BYTE> buffer;
    bool ok = FillBuffer(buffer, [&](BYTE* data, DWORD size, DWORD* needed) {
        return GetJobW(h.get(), jobId, 1, data, size, needed) != FALSE;
    });
    if (!ok) return OpResult::FromLastError();
    auto* info = reinterpret_cast<JOB_INFO_1W*>(buffer.data());
    info->pStatus = nullptr;  // no sobrescribir el texto de estado del monitor de puerto
    info->Position = JOB_POSITION_UNSPECIFIED;
    mutate(*info);
    if (!SetJobW(h.get(), jobId, 1, reinterpret_cast<BYTE*>(info), 0)) return OpResult::FromLastError();
    return OpResult::Success();
}

}  // namespace

std::wstring NormalizeServerName(const std::wstring& server) {
    std::wstring s = server;
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/' || s.back() == L' ')) s.pop_back();
    while (!s.empty() && s.front() == L' ') s.erase(s.begin());
    if (s.empty() || s == L"." || _wcsicmp(s.c_str(), L"localhost") == 0) return L"";
    std::replace(s.begin(), s.end(), L'/', L'\\');
    while (!s.empty() && s.front() == L'\\') s.erase(s.begin());
    return L"\\\\" + s;
}

bool IsLocalServer(const std::wstring& serverIn) {
    const std::wstring server = NormalizeServerName(serverIn);
    if (server.empty()) return true;
    const std::wstring host = server.substr(2);  // sin "\\\\"
    const COMPUTER_NAME_FORMAT formats[] = {ComputerNameNetBIOS, ComputerNameDnsHostname,
                                            ComputerNameDnsFullyQualified, ComputerNamePhysicalNetBIOS};
    for (auto format : formats) {
        wchar_t name[256] = {};
        DWORD size = static_cast<DWORD>(std::size(name));
        if (!GetComputerNameExW(format, name, &size)) continue;
        if (_wcsicmp(host.c_str(), name) == 0) return true;
        // "SRV" contra "srv.dominio.local" (y al revés).
        const wchar_t* dot = wcschr(name, L'.');
        if (dot && static_cast<size_t>(dot - name) == host.size() && _wcsnicmp(host.c_str(), name, host.size()) == 0)
            return true;
    }
    return false;
}

std::wstring ReplacePrinterServer(const std::wstring& printerName, const std::wstring& newHost) {
    if (newHost.empty() || printerName.size() < 3 || printerName.rfind(L"\\\\", 0) != 0) return L"";
    size_t slash = printerName.find(L'\\', 2);
    if (slash == std::wstring::npos || slash + 1 >= printerName.size()) return L"";
    return L"\\\\" + newHost + printerName.substr(slash);
}

namespace {

// Nombre DNS completo de un equipo ("vtec-laimiprint" -> "vtec-laimiprint.estudiantes.tec.cr").
std::wstring CanonicalHostName(const std::wstring& host) {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return L"";
    ADDRINFOW hints{};
    hints.ai_flags = AI_CANONNAME;
    hints.ai_family = AF_UNSPEC;
    PADDRINFOW result = nullptr;
    std::wstring canonical;
    if (GetAddrInfoW(host.c_str(), nullptr, &hints, &result) == 0 && result && result->ai_canonname)
        canonical = result->ai_canonname;
    if (result) FreeAddrInfoW(result);
    WSACleanup();
    return canonical;
}

}  // namespace

std::wstring CacheBypassName(const std::wstring& printerName) {
    if (printerName.size() < 3 || printerName.rfind(L"\\\\", 0) != 0) return L"";
    const size_t slash = printerName.find(L'\\', 2);
    if (slash == std::wstring::npos) return L"";
    const std::wstring host = printerName.substr(2, slash - 2);
    if (IsLocalServer(L"\\\\" + host)) return L"";  // colas de este equipo: no hay caché de cliente

    // La resolución DNS se hace una vez por servidor.
    static std::mutex mutex;
    static std::map<std::wstring, std::wstring> resolved;
    std::wstring canonical;
    {
        std::lock_guard lock(mutex);
        auto it = resolved.find(util::ToLower(host));
        if (it != resolved.end()) canonical = it->second;
    }
    if (canonical.empty()) {
        canonical = CanonicalHostName(host);
        std::lock_guard lock(mutex);
        resolved[util::ToLower(host)] = canonical;
    }
    // Si la conexión ya usa el nombre completo, no hay otro nombre que evite la caché.
    if (canonical.empty() || util::EqualsInsensitive(canonical, host)) return L"";
    return ReplacePrinterServer(printerName, canonical);
}

std::vector<PrinterInfo> EnumeratePrinters(const std::wstring& serverIn, DWORD* error) {
    const std::wstring server = NormalizeServerName(serverIn);
    const std::wstring defaultName = server.empty() ? GetDefaultPrinterName() : L"";
    std::vector<PrinterInfo> printers;

    // Agrega las impresoras de una enumeración, sin repetir las que ya están.
    // 'prefix' != "" filtra las conexiones que no son de este servidor.
    auto append = [&](DWORD flags, const std::wstring& target, const std::wstring& prefix, DWORD* errorOut) {
        std::vector<BYTE> buffer;
        DWORD count = 0;
        LPWSTR name = target.empty() ? nullptr : const_cast<LPWSTR>(target.c_str());
        bool ok = FillBuffer(buffer, [&](BYTE* data, DWORD size, DWORD* needed) {
            return EnumPrintersW(flags, name, 2, data, size, needed, &count) != FALSE;
        });
        if (!ok) {
            DWORD err = GetLastError();
            if (errorOut) *errorOut = err;
            util::LogError(L"EnumPrinters(flags=%lu, '%ls') falló: %lu", flags, target.c_str(), err);
            return;
        }
        if (errorOut) *errorOut = ERROR_SUCCESS;
        auto* infos = reinterpret_cast<PRINTER_INFO_2W*>(buffer.data());
        for (DWORD i = 0; i < count; ++i) {
            PrinterInfo p;
            FillPrinterInfo(infos[i], server, defaultName, p);
            if (!prefix.empty() && _wcsnicmp(p.name.c_str(), prefix.c_str(), prefix.size()) != 0) continue;
            bool duplicate = std::any_of(printers.begin(), printers.end(), [&](const PrinterInfo& existing) {
                return _wcsicmp(existing.name.c_str(), p.name.c_str()) == 0;
            });
            if (!duplicate) printers.push_back(std::move(p));
        }
    };

    DWORD err = ERROR_SUCCESS;
    if (server.empty()) {
        append(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, L"", L"", &err);
    } else {
        // Compartidas del servidor (lo normal en un servidor de impresión).
        append(PRINTER_ENUM_NAME, server, L"", &err);
        // Colas locales del propio equipo cuando la app corre en el servidor:
        // incluye las que no están compartidas (sus trabajos no salían en la lista).
        if (IsLocalServer(server)) append(PRINTER_ENUM_LOCAL, L"", L"", nullptr);
        // Conexiones de este equipo a ese servidor (por si alguna no se enumera por nombre).
        append(PRINTER_ENUM_CONNECTIONS, L"", server + L"\\", nullptr);
    }
    if (error) *error = printers.empty() ? err : ERROR_SUCCESS;

    std::sort(printers.begin(), printers.end(), [](const PrinterInfo& a, const PrinterInfo& b) {
        return _wcsicmp(a.DisplayName().c_str(), b.DisplayName().c_str()) < 0;
    });
    return printers;
}

bool GetDriverForms(const std::wstring& printer, std::vector<std::pair<short, std::wstring>>& bins,
                    std::vector<DriverPaper>& papers) {
    bins.clear();
    papers.clear();
    const wchar_t* device = printer.c_str();
    const int binCount = DeviceCapabilitiesW(device, nullptr, DC_BINS, nullptr, nullptr);
    if (binCount > 0) {
        std::vector<WORD> codes(static_cast<size_t>(binCount));
        std::vector<wchar_t> names(static_cast<size_t>(binCount) * 24 + 1, L'\0');
        if (DeviceCapabilitiesW(device, nullptr, DC_BINS, reinterpret_cast<LPWSTR>(codes.data()), nullptr) == binCount &&
            DeviceCapabilitiesW(device, nullptr, DC_BINNAMES, names.data(), nullptr) == binCount) {
            for (int i = 0; i < binCount; ++i) {
                const wchar_t* name = &names[static_cast<size_t>(i) * 24];
                bins.emplace_back(static_cast<short>(codes[static_cast<size_t>(i)]), std::wstring(name, wcsnlen(name, 24)));
            }
        }
    }
    const int paperCount = DeviceCapabilitiesW(device, nullptr, DC_PAPERS, nullptr, nullptr);
    if (paperCount > 0) {
        std::vector<WORD> codes(static_cast<size_t>(paperCount));
        std::vector<wchar_t> names(static_cast<size_t>(paperCount) * 64 + 1, L'\0');
        std::vector<POINT> sizes(static_cast<size_t>(paperCount));
        if (DeviceCapabilitiesW(device, nullptr, DC_PAPERS, reinterpret_cast<LPWSTR>(codes.data()), nullptr) == paperCount &&
            DeviceCapabilitiesW(device, nullptr, DC_PAPERNAMES, names.data(), nullptr) == paperCount &&
            DeviceCapabilitiesW(device, nullptr, DC_PAPERSIZE, reinterpret_cast<LPWSTR>(sizes.data()), nullptr) ==
                paperCount) {
            for (int i = 0; i < paperCount; ++i) {
                const wchar_t* name = &names[static_cast<size_t>(i) * 64];
                DriverPaper p;
                p.code = static_cast<short>(codes[static_cast<size_t>(i)]);
                p.name.assign(name, wcsnlen(name, 64));
                p.width = static_cast<short>(sizes[static_cast<size_t>(i)].x);
                p.length = static_cast<short>(sizes[static_cast<size_t>(i)].y);
                papers.push_back(std::move(p));
            }
        }
    }
    return !bins.empty() || !papers.empty();
}

bool QueryPrinter(HANDLE printer, const std::wstring& server, PrinterInfo& out) {
    std::vector<BYTE> buffer;
    bool ok = FillBuffer(buffer, [&](BYTE* data, DWORD size, DWORD* needed) {
        return GetPrinterW(printer, 2, data, size, needed) != FALSE;
    });
    if (!ok) return false;
    bool wasDefault = out.isDefault;
    FillPrinterInfo(*reinterpret_cast<PRINTER_INFO_2W*>(buffer.data()), server, L"", out);
    out.isDefault = wasDefault;
    return true;
}

std::vector<JobInfo> EnumerateJobs(HANDLE printer, const std::wstring& printerName, std::uint32_t problems,
                                   DWORD* error) {
    std::vector<JobInfo> jobs;
    std::vector<BYTE> buffer;
    DWORD count = 0;
    bool ok = FillBuffer(buffer, [&](BYTE* data, DWORD size, DWORD* needed) {
        return EnumJobsW(printer, 0, 0xFFFF, 2, data, size, needed, &count) != FALSE;
    });
    if (!ok) {
        if (error) *error = GetLastError();
        return jobs;
    }
    if (error) *error = ERROR_SUCCESS;

    auto* infos = reinterpret_cast<JOB_INFO_2W*>(buffer.data());
    jobs.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        const JOB_INFO_2W& src = infos[i];
        JobInfo j;
        j.id = src.JobId;
        j.printer = printerName;
        j.document = Str(src.pDocument);
        j.user = Str(src.pUserName);
        j.machine = Str(src.pMachineName);
        j.datatype = Str(src.pDatatype);
        j.driver = Str(src.pDriverName);
        j.printProcessor = Str(src.pPrintProcessor);
        j.parameters = Str(src.pParameters);
        j.statusText = Str(src.pStatus);
        j.rawStatus = src.Status;
        j.priority = src.Priority;
        j.position = src.Position;
        j.totalPages = src.TotalPages;
        j.pagesPrinted = src.PagesPrinted;
        j.sizeBytes = src.Size;
        j.submitted = src.Submitted;
        // El DEVMODE del trabajo (no el de la impresora): lo que se pidió al imprimir.
        j.settings = ParseDevMode(src.pDevMode);
        j.state = JobStateFromFlags(src.Status, problems);
        jobs.push_back(std::move(j));
    }
    return jobs;
}

OpResult PauseJob(const std::wstring& printer, std::uint32_t jobId) {
    return ControlJob(printer, jobId, JOB_CONTROL_PAUSE);
}

OpResult ResumeJob(const std::wstring& printer, std::uint32_t jobId) {
    return ControlJob(printer, jobId, JOB_CONTROL_RESUME);
}

OpResult CancelJob(const std::wstring& printer, std::uint32_t jobId) {
    // JOB_CONTROL_DELETE elimina el trabajo; es lo que hace la cola nativa de Windows.
    return ControlJob(printer, jobId, JOB_CONTROL_DELETE);
}

OpResult RestartJob(const std::wstring& printer, std::uint32_t jobId) {
    return ControlJob(printer, jobId, JOB_CONTROL_RESTART);
}

OpResult SetJobPosition(const std::wstring& printer, std::uint32_t jobId, std::uint32_t position) {
    if (position == 0) position = 1;
    return ModifyJobLevel1(printer, jobId, [&](JOB_INFO_1W& info) { info.Position = position; });
}

OpResult SetJobPriority(const std::wstring& printer, std::uint32_t jobId, std::uint32_t priority) {
    priority = std::clamp<std::uint32_t>(priority, MIN_PRIORITY, MAX_PRIORITY);
    return ModifyJobLevel1(printer, jobId, [&](JOB_INFO_1W& info) { info.Priority = priority; });
}

OpResult PausePrinter(const std::wstring& printer) { return ControlPrinter(printer, PRINTER_CONTROL_PAUSE); }
OpResult ResumePrinter(const std::wstring& printer) { return ControlPrinter(printer, PRINTER_CONTROL_RESUME); }
OpResult PurgePrinter(const std::wstring& printer) { return ControlPrinter(printer, PRINTER_CONTROL_PURGE); }

bool CanAdminister(const std::wstring& printer) {
    PrinterHandle h;
    return h.Open(printer, true);
}

OpResult ReadJobData(const std::wstring& printer, std::uint32_t jobId, std::uint64_t maxBytes,
                     std::vector<std::uint8_t>& out, bool* truncated) {
    out.clear();
    if (truncated) *truncated = false;
    // Dos lecturas simultáneas del mismo trabajo hacen que la segunda reciba 0
    // bytes (comprobado): todas las lecturas de la app pasan de a una.
    static std::mutex readMutex;
    std::lock_guard readLock(readMutex);
    // "Impresora,Job N" abre el trabajo (no la impresora) para leer sus datos.
    std::wstring name = printer + L",Job " + std::to_wstring(jobId);
    PRINTER_DEFAULTSW defaults{};
    defaults.DesiredAccess = JOB_READ;
    HANDLE h = nullptr;
    if (!OpenPrinterW(name.data(), &h, &defaults)) {
        DWORD err = GetLastError();
        // Sin derechos pedidos explícitamente algunos servidores dan el acceso que corresponde.
        if (err != ERROR_ACCESS_DENIED || !OpenPrinterW(name.data(), &h, nullptr)) return OpResult::Fail(err);
    }
    PrinterHandle handle(h);
    std::vector<std::uint8_t> chunk(256 * 1024);
    DWORD error = ERROR_SUCCESS;
    for (;;) {
        if (out.size() >= maxBytes) {
            // Comprueba si quedaba algo más sin leerlo entero.
            BYTE probe = 0;
            DWORD got = 0;
            if (truncated && ReadPrinter(handle.get(), &probe, 1, &got) && got > 0) *truncated = true;
            break;
        }
        DWORD want = static_cast<DWORD>(std::min<std::uint64_t>(chunk.size(), maxBytes - out.size()));
        DWORD got = 0;
        if (!ReadPrinter(handle.get(), chunk.data(), want, &got)) {
            error = GetLastError();
            break;
        }
        if (got == 0) break;  // fin de los datos
        out.insert(out.end(), chunk.begin(), chunk.begin() + got);
    }
    if (error != ERROR_SUCCESS) {
        if (out.empty()) return OpResult::Fail(error);
        if (truncated) *truncated = true;  // se cortó a mitad: lo leído sirve, pero está incompleto
    }
    return OpResult::Success();
}

std::wstring GetSpoolDirectory(const std::wstring& serverIn) {
    const std::wstring server = NormalizeServerName(serverIn);
    PrinterHandle h;
    if (!h.Open(server, false)) return L"";
    DWORD type = 0;
    DWORD needed = 0;
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD rc = GetPrinterDataW(h.get(), const_cast<LPWSTR>(SPLREG_DEFAULT_SPOOL_DIRECTORY), &type,
                               reinterpret_cast<BYTE*>(buffer), sizeof(buffer) - sizeof(wchar_t), &needed);
    if (rc != ERROR_SUCCESS) {
        util::LogError(L"No se pudo leer la carpeta de spool de %ls: %lu", server.c_str(), rc);
        return L"";
    }
    return buffer;
}

std::wstring GetPrinterSpoolDirectory(const std::wstring& printer) {
    if (printer.empty()) return L"";
    PrinterHandle h;
    if (!h.Open(printer, false)) return L"";
    DWORD type = 0;
    DWORD needed = 0;
    wchar_t buffer[MAX_PATH * 2] = {};
    // Cada cola puede tener su propia carpeta ("SpoolDirectory" en el registro
    // de la impresora); si no está definida, el spooler usa la del servidor.
    DWORD rc = GetPrinterDataW(h.get(), const_cast<LPWSTR>(L"SpoolDirectory"), &type,
                               reinterpret_cast<BYTE*>(buffer), sizeof(buffer) - sizeof(wchar_t), &needed);
    if (rc != ERROR_SUCCESS || type != REG_SZ) return L"";
    return buffer;
}

std::wstring GetDefaultPrinterName() {
    DWORD size = 0;
    GetDefaultPrinterW(nullptr, &size);
    if (size == 0) return L"";
    std::wstring name(size, L'\0');
    if (!GetDefaultPrinterW(name.data(), &size)) return L"";
    name.resize(size > 0 ? size - 1 : 0);
    return name;
}

}  // namespace spooler
}  // namespace cola::core
