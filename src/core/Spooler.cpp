// Implementación del envoltorio de winspool (ver Spooler.h). Todo aquí es
// síncrono y puede bloquear segundos contra un servidor remoto (RPC): solo se
// llama desde hilos de fondo. Lo único con estado compartido entre hilos es la
// "puerta de lecturas" (JobReadGate) y la caché de nombres DNS de
// CacheBypassName, ambas protegidas con mutex.
#include "core/Spooler.h"

// winsock solo para GetAddrInfoW (nombre DNS completo del servidor, D28).
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <condition_variable>
#include <cwchar>
#include <iterator>
#include <map>
#include <mutex>
#include <set>

#include "core/JobStatus.h"
#include "util/Log.h"
#include "util/Strings.h"

namespace cola::core {

bool PrinterHandle::Open(const std::wstring& name, bool admin) {
    Close();
    PRINTER_DEFAULTSW defaults{};
    // Un nombre vacío (equipo local) o "\\\\SRV" sin otra barra es un handle de
    // SERVIDOR: los derechos son SERVER_ACCESS_*, no PRINTER_ACCESS_*. Pedir
    // derechos de impresora sobre un servidor hace fallar OpenPrinter.
    if (name.empty() || (name.size() > 2 && name.rfind(L"\\\\", 0) == 0 && name.find(L'\\', 2) == std::wstring::npos)) {
        // Handle de servidor.
        defaults.DesiredAccess = admin ? SERVER_ACCESS_ADMINISTER : SERVER_ACCESS_ENUMERATE;
    } else {
        // Impresora: ADMINISTER se pide junto con USE para poder también
        // consultar y enumerar con el mismo handle.
        defaults.DesiredAccess = admin ? (PRINTER_ACCESS_ADMINISTER | PRINTER_ACCESS_USE) : PRINTER_ACCESS_USE;
    }
    HANDLE h = nullptr;
    // OpenPrinterW recibe nullptr para el servidor local; el const_cast es
    // necesario porque la firma de la API no es const (no modifica la cadena).
    LPWSTR target = name.empty() ? nullptr : const_cast<LPWSTR>(name.c_str());
    if (!OpenPrinterW(target, &h, &defaults)) return false;
    handle_ = h;
    return true;
}

bool PrinterHandle::OpenBest(const std::wstring& name, bool* gotAdmin) {
    // Primero con administración (sin "Administrar esta impresora" falla con
    // ERROR_ACCESS_DENIED) y, si no, con uso normal.
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

// Copia una cadena de las estructuras de winspool, que pueden traer punteros
// nulos en campos opcionales (pComment, pLocation, pStatus...).
std::wstring Str(const wchar_t* s) { return s ? std::wstring(s) : std::wstring(); }

// Llama a una función del estilo "consulta tamaño + llena buffer" de winspool.
// call(data, size, &needed) debe devolver true si la API tuvo éxito. Primero se
// llama sin búfer para conocer el tamaño; luego se reintenta hasta 3 veces
// porque entre una llamada y otra la cola puede crecer (llega un trabajo) y
// el tamaño necesario cambiar. Devuelve false con GetLastError intacto.
// Nota: si la primera llamada tiene éxito sin búfer (nada que devolver),
// 'buffer' queda vacío.
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

// Copia un PRINTER_INFO_2 a PrinterInfo. server = servidor normalizado ("" =
// local); defaultName = impresora predeterminada para marcar isDefault (""
// para no marcar ninguna). No toca bins/papers (los llena el monitor aparte).
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
    // DEVMODE predeterminado de la cola: solo para comparar con el del trabajo.
    out.defaults = ParseDevMode(src.pDevMode);
    out.isShared = (src.Attributes & PRINTER_ATTRIBUTE_SHARED) != 0;
    out.isDefault = !defaultName.empty() && _wcsicmp(out.name.c_str(), defaultName.c_str()) == 0;
}

// Envía un comando JOB_CONTROL_* a un trabajo: SetJob con nivel 0 (sin
// estructura) solo ejecuta el comando. El handle se abre en cada llamada para
// no mantener conexiones abiertas con el servidor entre acciones.
OpResult ControlJob(const std::wstring& printer, std::uint32_t jobId, DWORD command) {
    PrinterHandle h;
    if (!h.OpenBest(printer)) return OpResult::FromLastError();
    if (!SetJobW(h.get(), jobId, 0, nullptr, command)) return OpResult::FromLastError();
    return OpResult::Success();
}

// Envía un comando PRINTER_CONTROL_* a la impresora (SetPrinter nivel 0).
// Exige abrirla con administración: con acceso de uso SetPrinter fallaría igual.
OpResult ControlPrinter(const std::wstring& printer, DWORD command) {
    PrinterHandle h;
    if (!h.Open(printer, true)) return OpResult::FromLastError();
    if (!SetPrinterW(h.get(), 0, nullptr, command)) return OpResult::FromLastError();
    return OpResult::Success();
}

// Coordina las lecturas de datos (ReadJobData) con las reanudaciones de la app:
// un trabajo no se reanuda mientras se lee (la lectura se corta) y no se empieza
// a leer uno que se está reanudando. Así el procesador de impresión nunca
// comparte el trabajo con una lectura de la app.
// (D39.) Como ReadJobData está serializada, basta con un único "reading".
// 'releasing' es multiset porque dos hilos podrían reanudar el mismo trabajo a
// la vez (p. ej. F11 y un clic): cada uno quita su propia entrada.
struct JobReadGate {
    std::mutex mutex;                 // protege reading y releasing
    std::condition_variable idle;     // se notifica al terminar una lectura
    std::wstring reading;                // trabajo que se lee ahora ("" = ninguno)
    std::multiset<std::wstring> releasing;  // trabajos que la app va a reanudar
};

// Única instancia por proceso (static local: se construye de forma segura entre
// hilos la primera vez que se usa).
JobReadGate& ReadGate() {
    static JobReadGate gate;
    return gate;
}

// Clave del trabajo en la puerta: impresora en minúsculas (los nombres de
// impresora no distinguen mayúsculas) + "#" + id.
std::wstring ReadGateKey(const std::wstring& printer, std::uint32_t jobId) {
    return util::ToLower(printer) + L"#" + std::to_wstring(jobId);
}

// true si la app está por reanudar/reiniciar ese trabajo: la lectura en curso
// debe cortarse para que ResumeJob/RestartJob puedan continuar.
bool ReleaseRequested(const std::wstring& key) {
    auto& gate = ReadGate();
    std::lock_guard lock(gate.mutex);
    return gate.releasing.count(key) > 0;
}

// Reanuda o reinicia un trabajo esperando a que termine (o se corte) una
// lectura en curso del mismo trabajo.
OpResult ReleaseJob(const std::wstring& printer, std::uint32_t jobId, DWORD command) {
    auto& gate = ReadGate();
    const std::wstring key = ReadGateKey(printer, jobId);
    {
        // Se anuncia la reanudación (así la lectura en curso se corta en su
        // próximo bloque y no empieza ninguna nueva) y se espera a que la
        // lectura de ESTE trabajo termine.
        std::unique_lock lock(gate.mutex);
        gate.releasing.insert(key);
        gate.idle.wait(lock, [&] { return gate.reading != key; });
    }
    // El SetJob va fuera del mutex: puede tardar (RPC) y no debe bloquear a
    // otras lecturas o reanudaciones de otros trabajos.
    OpResult result = ControlJob(printer, jobId, command);
    std::lock_guard lock(gate.mutex);
    // Se quita una sola entrada (find + erase), no todas las de esa clave.
    gate.releasing.erase(gate.releasing.find(key));
    return result;
}

// Estado actual del trabajo consultado al servidor (no a la caché del cliente,
// que puede estar atrasada). false si no se pudo consultar.
// 'printer' debería estar abierto con el nombre DNS completo (CacheBypassName)
// para que GetJob vaya al servidor. Se usa nivel 1 porque basta con Status y
// es más liviano que el 2 (sin DEVMODE).
bool LiveJobStatus(HANDLE printer, std::uint32_t jobId, DWORD& status) {
    std::vector<BYTE> buffer;
    bool ok = FillBuffer(buffer, [&](BYTE* data, DWORD size, DWORD* needed) {
        return GetJobW(printer, jobId, 1, data, size, needed) != FALSE;
    });
    if (!ok || buffer.empty()) return false;
    status = reinterpret_cast<const JOB_INFO_1W*>(buffer.data())->Status;
    return true;
}

// Modifica un trabajo usando el nivel 1 (posición/prioridad).
// Patrón leer-modificar-escribir: GetJob nivel 1 trae la estructura actual,
// 'mutate' cambia solo el campo deseado y SetJob nivel 1 la devuelve. Se
// neutralizan antes los campos que no se quieren tocar (ver abajo).
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
    // "Sin cambio de posición" salvo que mutate la fije (al cambiar la
    // prioridad no se quiere mover el trabajo).
    info->Position = JOB_POSITION_UNSPECIFIED;
    mutate(*info);
    // Command = 0 en SetJob: solo se aplican los cambios de la estructura.
    if (!SetJobW(h.get(), jobId, 1, reinterpret_cast<BYTE*>(info), 0)) return OpResult::FromLastError();
    return OpResult::Success();
}

}  // namespace

std::wstring NormalizeServerName(const std::wstring& server) {
    std::wstring s = server;
    // 1) Recorta espacios y barras sobrantes al final y espacios al inicio.
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/' || s.back() == L' ')) s.pop_back();
    while (!s.empty() && s.front() == L' ') s.erase(s.begin());
    // 2) Formas de decir "este equipo".
    if (s.empty() || s == L"." || _wcsicmp(s.c_str(), L"localhost") == 0) return L"";
    // 3) Barras normales a invertidas, quita las iniciales y antepone exactamente dos.
    std::replace(s.begin(), s.end(), L'/', L'\\');
    while (!s.empty() && s.front() == L'\\') s.erase(s.begin());
    return L"\\\\" + s;
}

bool IsLocalServer(const std::wstring& serverIn) {
    const std::wstring server = NormalizeServerName(serverIn);
    if (server.empty()) return true;
    const std::wstring host = server.substr(2);  // sin "\\\\"
    // Se compara contra todos los nombres que puede tener el equipo (NetBIOS,
    // DNS corto, DNS completo y NetBIOS físico en clústeres), sin mayúsculas.
    // No resuelve IPs: "\\\\10.0.0.5" apuntando a este equipo no se reconoce.
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
    // Debe ser "\\\\host\\cola" con algo después de la barra; se conserva
    // desde esa barra (incluida) y se cambia solo el host.
    if (newHost.empty() || printerName.size() < 3 || printerName.rfind(L"\\\\", 0) != 0) return L"";
    size_t slash = printerName.find(L'\\', 2);
    if (slash == std::wstring::npos || slash + 1 >= printerName.size()) return L"";
    return L"\\\\" + newHost + printerName.substr(slash);
}

namespace {

// Nombre DNS completo de un equipo ("vtec-laimiprint" -> "vtec-laimiprint.estudiantes.tec.cr").
// "" si no se pudo resolver (sin red, DNS sin responder, nombre inexistente).
// WSAStartup/WSACleanup se emparejan en cada llamada (Winsock lleva un contador
// interno, así que es seguro aunque otro código lo tenga inicializado).
std::wstring CanonicalHostName(const std::wstring& host) {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return L"";
    // AI_CANONNAME pide el nombre canónico (el FQDN que da el DNS); la familia
    // da igual porque no se usan las direcciones.
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
    // Solo aplica a "\\\\host\\cola": una impresora local no tiene caché de cliente.
    if (printerName.size() < 3 || printerName.rfind(L"\\\\", 0) != 0) return L"";
    const size_t slash = printerName.find(L'\\', 2);
    if (slash == std::wstring::npos) return L"";
    const std::wstring host = printerName.substr(2, slash - 2);
    if (IsLocalServer(L"\\\\" + host)) return L"";  // colas de este equipo: no hay caché de cliente

    // La resolución DNS se hace una vez por servidor. Si falla (red o DNS que
    // todavía no responden, p. ej. al iniciar con Windows) se reintenta pasado
    // un minuto, sin repetir la consulta en cada llamada mientras tanto.
    struct Resolution {
        std::wstring canonical;  // FQDN obtenido ("" = la última consulta falló)
        ULONGLONG retryAt = 0;   // GetTickCount64 a partir del cual se reintenta un fallo
    };
    // Caché compartida por todos los hilos (monitor, lecturas, acciones).
    static std::mutex mutex;
    static std::map<std::wstring, Resolution> resolved;  // host en minúsculas -> resolución
    const std::wstring key = util::ToLower(host);
    std::wstring canonical;
    bool known = false;
    {
        // Un éxito vale para siempre; un fallo solo hasta retryAt.
        std::lock_guard lock(mutex);
        auto it = resolved.find(key);
        if (it != resolved.end() && (!it->second.canonical.empty() || GetTickCount64() < it->second.retryAt)) {
            canonical = it->second.canonical;
            known = true;
        }
    }
    if (!known) {
        // La consulta DNS va fuera del mutex (puede tardar segundos). Si dos
        // hilos coinciden, ambos consultan y el último en guardar gana: inocuo.
        canonical = CanonicalHostName(host);
        std::lock_guard lock(mutex);
        resolved[key] = {canonical, GetTickCount64() + 60000};
        if (canonical.empty()) util::LogError(L"No se pudo resolver el nombre completo de %ls; se reintentará", host.c_str());
    }
    // Si la conexión ya usa el nombre completo, no hay otro nombre que evite la caché.
    if (canonical.empty() || util::EqualsInsensitive(canonical, host)) return L"";
    return ReplacePrinterServer(printerName, canonical);
}

std::vector<PrinterInfo> EnumeratePrinters(const std::wstring& serverIn, DWORD* error) {
    const std::wstring server = NormalizeServerName(serverIn);
    // La predeterminada solo tiene sentido en la vista de este equipo.
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
        // El búfer contiene 'count' PRINTER_INFO_2 seguidos y, detrás, las
        // cadenas y DEVMODE a los que apuntan: se copia todo antes de liberarlo.
        auto* infos = reinterpret_cast<PRINTER_INFO_2W*>(buffer.data());
        for (DWORD i = 0; i < count; ++i) {
            PrinterInfo p;
            FillPrinterInfo(infos[i], server, defaultName, p);
            // Comparación por prefijo sin mayúsculas ("\\\\SRV\\" contra "\\\\srv\\Cola").
            if (!prefix.empty() && _wcsnicmp(p.name.c_str(), prefix.c_str(), prefix.size()) != 0) continue;
            bool duplicate = std::any_of(printers.begin(), printers.end(), [&](const PrinterInfo& existing) {
                return _wcsicmp(existing.name.c_str(), p.name.c_str()) == 0;
            });
            if (!duplicate) printers.push_back(std::move(p));
        }
    };

    DWORD err = ERROR_SUCCESS;
    if (server.empty()) {
        // Vista "Este equipo": colas locales + conexiones a impresoras de
        // servidores (por eso muestra trabajos reales de estudiantes, GUIA_IA §6).
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
    // Solo es un error si no se obtuvo nada: las fuentes secundarias pueden
    // fallar sin que importe.
    if (error) *error = printers.empty() ? err : ERROR_SUCCESS;

    // Orden alfabético por el nombre que ve el operador.
    std::sort(printers.begin(), printers.end(), [](const PrinterInfo& a, const PrinterInfo& b) {
        return _wcsicmp(a.DisplayName().c_str(), b.DisplayName().c_str()) < 0;
    });
    return printers;
}

bool GetDriverForms(const std::wstring& printer, std::vector<std::pair<short, std::wstring>>& bins,
                    std::vector<DriverPaper>& papers) {
    bins.clear();
    papers.clear();
    // DeviceCapabilities carga el controlador en este proceso (por eso necesita
    // el controlador instalado localmente y puede tardar). Patrón: primero se
    // pide la cantidad (salida nula) y luego los arreglos; las tres listas de
    // cada grupo deben tener la misma cantidad o se descartan.
    const wchar_t* device = printer.c_str();
    // Bandejas: DC_BINS = códigos WORD, DC_BINNAMES = nombres de 24 caracteres
    // fijos cada uno (no siempre terminados en nulo).
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
    // Papeles: DC_PAPERS = códigos WORD, DC_PAPERNAMES = nombres de 64
    // caracteres fijos, DC_PAPERSIZE = POINT con ancho/alto en décimas de mm.
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
    // FillPrinterInfo sin nombre predeterminado pondría isDefault = false: se
    // conserva el valor que vino de la enumeración.
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
    // Desde el primer trabajo (0) hasta 0xFFFF trabajos, nivel 2 (JOB_INFO_2:
    // incluye DEVMODE, equipo, tamaño y páginas).
    bool ok = FillBuffer(buffer, [&](BYTE* data, DWORD size, DWORD* needed) {
        return EnumJobsW(printer, 0, 0xFFFF, 2, data, size, needed, &count) != FALSE;
    });
    if (!ok) {
        if (error) *error = GetLastError();
        return jobs;
    }
    if (error) *error = ERROR_SUCCESS;

    // Copia cada JOB_INFO_2 a un JobInfo propio (el búfer se libera al salir).
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
        j.submitted = src.Submitted;  // UTC: la UI la convierte a hora local
        // El DEVMODE del trabajo (no el de la impresora): lo que se pidió al imprimir.
        j.settings = ParseDevMode(src.pDevMode);
        j.state = JobStateFromFlags(src.Status, problems);
        // heldByApp lo marca después el monitor (AutoHoldTracker::Annotate).
        jobs.push_back(std::move(j));
    }
    return jobs;
}

OpResult PauseJob(const std::wstring& printer, std::uint32_t jobId) {
    return ControlJob(printer, jobId, JOB_CONTROL_PAUSE);
}

// Reanudar y reiniciar pasan por ReleaseJob: esperan a que la app deje de
// leer ese trabajo antes de que el spooler empiece a enviarlo (D39).
OpResult ResumeJob(const std::wstring& printer, std::uint32_t jobId) {
    return ReleaseJob(printer, jobId, JOB_CONTROL_RESUME);
}

OpResult CancelJob(const std::wstring& printer, std::uint32_t jobId) {
    // JOB_CONTROL_DELETE elimina el trabajo; es lo que hace la cola nativa de Windows.
    return ControlJob(printer, jobId, JOB_CONTROL_DELETE);
}

OpResult RestartJob(const std::wstring& printer, std::uint32_t jobId) {
    return ReleaseJob(printer, jobId, JOB_CONTROL_RESTART);
}

OpResult SetJobPosition(const std::wstring& printer, std::uint32_t jobId, std::uint32_t position) {
    // 0 significa "sin especificar" para el spooler: se interpreta como "al principio".
    if (position == 0) position = 1;
    return ModifyJobLevel1(printer, jobId, [&](JOB_INFO_1W& info) { info.Position = position; });
}

OpResult SetJobPriority(const std::wstring& printer, std::uint32_t jobId, std::uint32_t priority) {
    priority = std::clamp<std::uint32_t>(priority, MIN_PRIORITY, MAX_PRIORITY);
    return ModifyJobLevel1(printer, jobId, [&](JOB_INFO_1W& info) { info.Priority = priority; });
}

// Pausar/reanudar/purgar la cola entera (SetPrinter con PRINTER_CONTROL_*).
OpResult PausePrinter(const std::wstring& printer) { return ControlPrinter(printer, PRINTER_CONTROL_PAUSE); }
OpResult ResumePrinter(const std::wstring& printer) { return ControlPrinter(printer, PRINTER_CONTROL_RESUME); }
OpResult PurgePrinter(const std::wstring& printer) { return ControlPrinter(printer, PRINTER_CONTROL_PURGE); }

bool CanAdminister(const std::wstring& printer) {
    // El handle se cierra al salir (RAII); solo interesa si se pudo abrir.
    PrinterHandle h;
    return h.Open(printer, true);
}

OpResult ReadJobData(const std::wstring& printer, std::uint32_t jobId, std::uint64_t maxBytes,
                     std::vector<std::uint8_t>& out, bool* truncated, const DataSink& beyond) {
    out.clear();
    if (truncated) *truncated = false;
    // Paso 1 (D32): serializar.
    // Dos lecturas simultáneas del mismo trabajo hacen que la segunda reciba 0
    // bytes (comprobado): todas las lecturas de la app pasan de a una.
    static std::mutex readMutex;
    std::lock_guard readLock(readMutex);

    // Paso 2 (D39): registrarse en la puerta de lecturas.
    // Nunca se lee a la vez que el spooler envía el trabajo a la impresora: las
    // lecturas comparten el avance y a la impresora le llegarían datos
    // incompletos. Se registra la lectura (una reanudación de la app la espera)...
    auto& gate = ReadGate();
    const std::wstring key = ReadGateKey(printer, jobId);
    {
        std::lock_guard lock(gate.mutex);
        // Ya hay una reanudación en camino: no se empieza a leer.
        if (gate.releasing.count(key)) return OpResult::Fail(ERROR_BUSY);
        gate.reading = key;
    }
    // Al salir por cualquier camino (incluidos los return de error) se libera
    // la puerta y se despierta a las reanudaciones que esperaban.
    struct ReadingGuard {
        JobReadGate& gate;
        ~ReadingGuard() {
            {
                std::lock_guard lock(gate.mutex);
                gate.reading.clear();
            }
            gate.idle.notify_all();
        }
    } readingGuard{gate};
    // ...y se comprueba el estado real antes y durante la lectura (otro operador
    // o la cola de Windows pueden reanudarlo).
    // Paso 3: handle para consultar el estado real. Se prefiere el nombre DNS
    // completo (va al servidor, D28); si no existe o no abre, el de la conexión.
    PrinterHandle statusHandle;
    const std::wstring direct = CacheBypassName(printer);
    if ((direct.empty() || !statusHandle.Open(direct, false)) && !statusHandle.Open(printer, false))
        return OpResult::FromLastError();
    // Seguro = nadie de la app pidió reanudarlo, el estado se pudo consultar y
    // cumple CanReadJobData (en pausa o impreso; nunca recibiéndose, etc.).
    auto safeToRead = [&] {
        DWORD status = 0;
        return !ReleaseRequested(key) && LiveJobStatus(statusHandle.get(), jobId, status) && CanReadJobData(status);
    };
    if (!safeToRead()) {
        util::LogInfo(L"No se leen los datos del trabajo %u en '%ls': no está en pausa o se está imprimiendo", jobId,
                      printer.c_str());
        return OpResult::Fail(ERROR_BUSY);
    }

    // Paso 4 (D22): abrir el trabajo.
    // "Impresora,Job N" abre el trabajo (no la impresora) para leer sus datos.
    // Se usa el nombre de la conexión: es el que tiene los permisos delegados.
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
    // Paso 5: leer por bloques de 256 KB. Antes de cada bloque (salvo el
    // primero, recién comprobado) se vuelve a consultar el estado real.
    std::vector<std::uint8_t> chunk(256 * 1024);
    DWORD error = ERROR_SUCCESS;
    for (;;) {
        // overflow = ya se guardó todo lo permitido; lo que siga va a 'beyond'
        // (si se dio) o solo se comprueba si queda algo.
        const bool overflow = out.size() >= maxBytes;
        if (overflow && !beyond) {
            // Comprueba si quedaba algo más sin leerlo entero.
            BYTE probe = 0;
            DWORD got = 0;
            if (truncated && ReadPrinter(handle.get(), &probe, 1, &got) && got > 0) *truncated = true;
            break;
        }
        if (overflow && !safeToRead()) {
            // Lo que se guardó se leyó en pausa y sirve; el resto ya no se recorre.
            util::LogInfo(L"Lectura del resto del trabajo %u en '%ls' cortada: se reanudó", jobId, printer.c_str());
            if (truncated) *truncated = true;
            break;
        }
        if (!out.empty() && !safeToRead()) {
            // Lo reanudaron mientras se leía: se corta para no quitarle datos a la impresora.
            util::LogInfo(L"Lectura del trabajo %u en '%ls' cortada: se reanudó", jobId, printer.c_str());
            out.clear();
            return OpResult::Fail(ERROR_BUSY);
        }
        // Sin desbordar se pide como mucho lo que falta hasta maxBytes, para no
        // guardar de más; desbordando, bloques completos para 'beyond'.
        DWORD want = overflow ? static_cast<DWORD>(chunk.size())
                              : static_cast<DWORD>(std::min<std::uint64_t>(chunk.size(), maxBytes - out.size()));
        DWORD got = 0;
        if (!ReadPrinter(handle.get(), chunk.data(), want, &got)) {
            error = GetLastError();
            break;
        }
        if (got == 0) break;  // fin de los datos
        if (overflow) {
            // Pasa de maxBytes: se entrega a 'beyond' sin guardarlo (D53).
            if (truncated) *truncated = true;
            if (!beyond(chunk.data(), got)) break;
            continue;
        }
        out.insert(out.end(), chunk.begin(), chunk.begin() + got);
    }
    // Paso 6: resultado. Un error de ReadPrinter sin nada leído es un fallo; con
    // datos leídos se devuelve lo que hay marcado como truncado. Comparar con
    // JOB_INFO_2.Size para detectar lecturas incompletas es tarea de quien
    // llama (preview::ReadLooksIncomplete, D48).
    if (error != ERROR_SUCCESS) {
        if (out.empty()) return OpResult::Fail(error);
        if (truncated) *truncated = true;  // se cortó a mitad: lo leído sirve, pero está incompleto
    }
    return OpResult::Success();
}

std::wstring GetSpoolDirectory(const std::wstring& serverIn) {
    const std::wstring server = NormalizeServerName(serverIn);
    // Handle de servidor (sin admin: leer este valor no lo requiere).
    PrinterHandle h;
    if (!h.Open(server, false)) return L"";
    DWORD type = 0;
    DWORD needed = 0;
    // Se reserva un wchar_t al final (tamaño - 1) para garantizar el nulo
    // aunque el valor ocupe todo el búfer.
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
    // Un valor vacío o de otro tipo (REG_EXPAND_SZ...) se trata como "no definido".
    if (rc != ERROR_SUCCESS || type != REG_SZ) return L"";
    return buffer;
}

std::wstring GetDefaultPrinterName() {
    // Primera llamada: solo el tamaño necesario (en caracteres, con el nulo).
    DWORD size = 0;
    GetDefaultPrinterW(nullptr, &size);
    if (size == 0) return L"";
    std::wstring name(size, L'\0');
    if (!GetDefaultPrinterW(name.data(), &size)) return L"";
    // size incluye el nulo final: se quita para que la cadena no lo lleve dentro.
    name.resize(size > 0 ? size - 1 : 0);
    return name;
}

}  // namespace spooler
}  // namespace cola::core
