// Implementación de la lectura de los datos de un trabajo (API del spooler o
// archivo .SPL) y de la detección del lenguaje de impresión. Ver SpoolFile.h.
//
// Orden de lectura en ReadJobSpool: carpeta alternativa -> API -> archivo.
// Formatos que distingue DetectFormat (por contenido, en este orden):
// EMFSPOOL / EMF, ZIP (XPS), EJL de Epson, PJL ENTER LANGUAGE, PDF,
// PostScript, PCL XL, PCL 5 (ESC), imágenes PNG/JPEG y texto plano.
#include "preview/SpoolFile.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>

#include "core/JobStatus.h"
#include "core/Spooler.h"
#include "util/Log.h"
#include "util/Strings.h"

namespace cola::preview {

namespace {

// Caché de la carpeta de spool por servidor (consultarla es una llamada RPC).
// Clave: nombre del servidor normalizado ("" = este equipo). Valor: ruta
// local en ese servidor (p. ej. "C:\Windows\System32\spool\PRINTERS").
// Protegida por g_spoolDirMutex porque la usan varios hilos de fondo.
std::mutex g_spoolDirMutex;
std::map<std::wstring, std::wstring> g_spoolDirs;

// Carpeta de spool predeterminada del servidor (ruta local del servidor).
// Si la consulta falla se supone la de fábrica bajo el %windir% de ESTE
// equipo (lo habitual es que coincida con la del servidor). La consulta RPC
// se hace fuera del candado para no bloquear a otros hilos; si dos hilos la
// hacen a la vez, el segundo simplemente sobrescribe con el mismo valor.
std::wstring SpoolDirectoryFor(const std::wstring& server) {
    {
        std::lock_guard lock(g_spoolDirMutex);
        auto it = g_spoolDirs.find(server);
        if (it != g_spoolDirs.end()) return it->second;
    }
    std::wstring dir = core::spooler::GetSpoolDirectory(server);
    if (dir.empty()) {
        wchar_t windir[MAX_PATH] = L"C:\\Windows";
        GetWindowsDirectoryW(windir, MAX_PATH);
        dir = std::wstring(windir) + L"\\System32\\spool\\PRINTERS";
    }
    std::lock_guard lock(g_spoolDirMutex);
    g_spoolDirs[server] = dir;
    return dir;
}

// true si los datos empiezan con la cadena C 'prefix' (sin el '\0' final).
// Ojo: el prefijo no puede contener bytes 0 porque se mide con strlen.
bool StartsWith(const std::uint8_t* data, size_t size, const char* prefix) {
    size_t len = std::strlen(prefix);
    return size >= len && std::memcmp(data, prefix, len) == 0;
}

// Lee un DWORD little-endian sin exigir alineación (memcpy evita lecturas
// desalineadas y problemas de aliasing). El llamador garantiza 4 bytes.
std::uint32_t ReadU32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

}  // namespace

// Encabezado EJL (Epson Job Language): opcionalmente "ESC 01" (0x1B 0x01,
// el equivalente Epson del UEL de PJL) seguido de líneas "@EJL ...". El
// lenguaje que sigue se declara con "LA=". Solo se examinan los primeros
// 8 KB, pasados a mayúsculas, porque el encabezado siempre es corto.
DataFormat EjlLanguage(const std::uint8_t* data, size_t size) {
    size_t start = 0;
    if (StartsWith(data, size, "\x1b\x01")) start = 2;
    if (!StartsWith(data + start, size - start, "@EJL")) return DataFormat::Unknown;
    std::string header(reinterpret_cast<const char*>(data), std::min<size_t>(size, 8192));
    for (auto& c : header) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    // "@EJL EN LA=ESC/PAGE-COLOR" (entrar al lenguaje) o "@EJL SE LA=..." (seleccionar).
    size_t la = header.find("LA=");
    if (la == std::string::npos) return DataFormat::EscPage;  // EJL sin lenguaje declarado: es de Epson
    // El valor llega hasta el primer espacio o fin de línea (24 caracteres bastan).
    std::string lang = header.substr(la + 3, 24);
    lang = lang.substr(0, lang.find_first_of(" \r\n"));
    // rfind(x, 0) == 0 equivale a "empieza con x". ESC/PAGE cubre también
    // ESC/PAGE-COLOR (las colas Epson del LAIMI). "ESC/P" va después de
    // "ESC/PAGE" para que no lo capture primero.
    if (lang.rfind("ESC/PAGE", 0) == 0) return DataFormat::EscPage;
    if (lang.rfind("ESCPR", 0) == 0 || lang.rfind("ESC/P-R", 0) == 0 || lang.rfind("ESC/P", 0) == 0)
        return DataFormat::EscPr;
    if (lang.rfind("POSTSCRIPT", 0) == 0) return DataFormat::PostScript;
    if (lang.rfind("PCLXL", 0) == 0) return DataFormat::PclXl;
    if (lang.rfind("PCL", 0) == 0) return DataFormat::Pcl;
    // Lenguaje desconocido pero con EJL: se asume ESC/Page (nunca PCL, que
    // haría fallar a GhostPCL con datos Epson).
    return DataFormat::EscPage;
}

// "C:\Windows\..." en el servidor "\\SRV" -> "\\SRV\C$\Windows\...".
std::wstring ToAdminShare(const std::wstring& server, const std::wstring& localPath) {
    if (server.empty()) return localPath;
    if (localPath.rfind(L"\\\\", 0) == 0) return localPath;  // ya es UNC
    if (localPath.size() >= 2 && localPath[1] == L':')
        return server + L"\\" + localPath.substr(0, 1) + L"$" + localPath.substr(2);
    return localPath;
}

std::vector<std::wstring> SpoolDirectories(const SpoolSearch& search) {
    const std::wstring server = core::spooler::NormalizeServerName(search.server);
    // Si la app corre en el propio servidor, las rutas locales evitan pasar por
    // \\SRV\C$ (que exige SMB y ser administrador aunque el archivo esté al lado).
    const bool local = core::spooler::IsLocalServer(server);
    std::vector<std::wstring> dirs;

    // Agrega una carpeta quitando barras/espacios finales y sin repetir
    // (comparación sin distinguir mayúsculas, como el sistema de archivos).
    auto add = [&dirs](std::wstring dir) {
        while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L' ')) dir.pop_back();
        if (dir.empty()) return;
        for (const auto& existing : dirs)
            if (util::EqualsInsensitive(existing, dir)) return;
        dirs.push_back(std::move(dir));
    };
    // Una carpeta local del servidor, en las formas en que puede alcanzarse.
    // Ej.: "C:\Windows\System32\spool\PRINTERS" en \\SRV produce
    //   "\\SRV\C$\Windows\System32\spool\PRINTERS" y
    //   "\\SRV\ADMIN$\System32\spool\PRINTERS" (útil si %windir% no está en C:).
    auto addServerDir = [&](const std::wstring& dir) {
        if (dir.empty()) return;
        if (local || server.empty()) add(dir);
        if (!server.empty()) {
            add(ToAdminShare(server, dir));
            // Alternativa por ADMIN$ (apunta a %windir% del servidor).
            std::wstring lower = util::ToLower(dir);
            size_t pos = lower.find(L"\\system32\\spool");
            if (pos != std::wstring::npos) add(server + L"\\ADMIN$" + dir.substr(pos));
        }
    };

    // Orden de preferencia: alternativa del operador, carpeta de la cola,
    // carpeta general del servidor.
    if (!search.overrideDir.empty()) add(search.overrideDir);
    // Carpeta propia de la cola, si tiene una configurada.
    if (!search.printer.empty()) addServerDir(core::spooler::GetPrinterSpoolDirectory(search.printer));
    addServerDir(SpoolDirectoryFor(server));
    return dirs;
}

std::vector<std::wstring> SpoolFileCandidates(const SpoolSearch& search, std::uint32_t jobId) {
    std::vector<std::wstring> candidates;
    for (const auto& dir : SpoolDirectories(search))
        candidates.push_back(dir + L"\\" + util::Format(L"%05u.SPL", jobId));
    return candidates;
}

bool ShadowMatchesJob(const std::uint8_t* data, size_t size, std::uint32_t jobId, const std::wstring& document,
                      bool requireDocument) {
    // El id del trabajo está en el encabezado del .SHD (los primeros campos DWORD;
    // el desplazamiento exacto varía entre versiones de Windows y 32/64 bits).
    // Por eso se prueba cada DWORD entre los bytes 4 y 64 (el primero es la
    // firma/versión del .SHD). Es una heurística: un id pequeño podría
    // coincidir por azar con otro campo, de ahí la confirmación por documento.
    bool idFound = false;
    for (size_t offset = 4; offset + 4 <= std::min<size_t>(size, 64); offset += 4) {
        if (ReadU32(data + offset) == jobId) {
            idFound = true;
            break;
        }
    }
    if (!idFound) return false;
    if (document.empty() || !requireDocument) return true;
    // Confirma con el nombre del documento (UTF-16 dentro del .SHD).
    // Basta con sus primeros 32 caracteres; se busca en todo el archivo con
    // paso de 2 bytes (alineación de UTF-16).
    std::wstring needle = document.substr(0, std::min<size_t>(document.size(), 32));
    const size_t needleBytes = needle.size() * sizeof(wchar_t);
    for (size_t offset = 0; offset + needleBytes <= size; offset += 2)
        if (std::memcmp(data + offset, needle.data(), needleBytes) == 0) return true;
    return false;
}

std::wstring FindPooledSpoolFile(const std::wstring& dir, std::uint32_t jobId, const std::wstring& document,
                                 unsigned long* error) {
    // Con "spool file pooling" (Windows Vista+) los archivos se llaman FPnnnnn.SPL/.SHD
    // y el número no es el id del trabajo: hay que buscarlo en los .SHD.
    // Paso 1: listar todos los .SHD de la carpeta.
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((dir + L"\\*.SHD").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        // Sin archivos .SHD la carpeta existe pero el trabajo no está ahí.
        if (error) *error = err == ERROR_FILE_NOT_FOUND ? ERROR_FILE_NOT_FOUND : err;
        return L"";
    }
    std::vector<std::wstring> shadows;
    do {
        shadows.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(find, &fd));
    FindClose(find);

    // Primera pasada: id + nombre del documento. Segunda: solo el id (algunos
    // servidores guardan el documento de otra forma y si no, no se encontraba).
    // Cada .SHD se lee hasta 256 KB (son pequeños: encabezado, DEVMODE y
    // cadenas). 'worst' conserva el error de lectura más informativo.
    unsigned long worst = ERROR_FILE_NOT_FOUND;
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& shd : shadows) {
            std::vector<std::uint8_t> bytes;
            unsigned long err = 0;
            if (!ReadFileShared(shd, 256 * 1024, bytes, nullptr, &err)) {
                // No se pudo leer el .SHD (permisos, bloqueo): hay que reportarlo,
                // si no el trabajo parece "inexistente".
                if (pass == 0 && err != ERROR_FILE_NOT_FOUND) worst = err;
                continue;
            }
            if (ShadowMatchesJob(bytes.data(), bytes.size(), jobId, document, pass == 0)) {
                if (error) *error = ERROR_SUCCESS;
                // FPnnnnn.SHD -> FPnnnnn.SPL (mismo nombre, otra extensión).
                return shd.substr(0, shd.size() - 4) + L".SPL";
            }
        }
        if (document.empty()) break;  // la segunda pasada sería idéntica
    }
    if (error) *error = worst;
    return L"";
}

bool ReadFileShared(const std::wstring& path, std::uint64_t maxBytes, std::vector<std::uint8_t>& out,
                    std::uint64_t* fileSize, unsigned long* error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) *error = GetLastError();
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    if (fileSize) *fileSize = static_cast<std::uint64_t>(size.QuadPart);
    std::uint64_t toRead = std::min<std::uint64_t>(static_cast<std::uint64_t>(size.QuadPart), maxBytes);
    out.resize(static_cast<size_t>(toRead));
    // ReadFile acepta como máximo un DWORD por llamada: se lee en bloques de
    // 16 MB. Si el archivo se acorta mientras tanto (el spooler lo está
    // escribiendo o lo borró), read == 0 corta el bucle y out se ajusta a lo
    // realmente leído.
    size_t offset = 0;
    while (offset < out.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(out.size() - offset, 16 * 1024 * 1024));
        DWORD read = 0;
        if (!ReadFile(file, out.data() + offset, chunk, &read, nullptr) || read == 0) break;
        offset += read;
    }
    out.resize(offset);
    CloseHandle(file);
    if (error) *error = ERROR_SUCCESS;
    return true;
}

SpoolData ReadSpoolFile(const SpoolSearch& search, std::uint32_t jobId, const std::wstring& document,
                        std::uint64_t maxBytes) {
    SpoolData data;
    data.error = ERROR_FILE_NOT_FOUND;
    // Conserva el error más informativo (acceso denegado > no encontrado).
    // Un "no encontrado" no pisa un error más concreto ya guardado.
    auto keepError = [&](unsigned long err) {
        if (err == ERROR_ACCESS_DENIED || data.error == ERROR_FILE_NOT_FOUND || data.error == ERROR_PATH_NOT_FOUND)
            data.error = err;
    };
    // Intenta leer una ruta; si lo logra completa 'data' (ruta, recorte y
    // formato) y devuelve true.
    auto tryRead = [&](const std::wstring& path) {
        unsigned long err = 0;
        if (ReadFileShared(path, maxBytes, data.bytes, &data.fileSize, &err)) {
            data.path = path;
            data.error = 0;
            data.truncated = data.bytes.size() < data.fileSize;
            data.format = DetectFormat(data.bytes.data(), data.bytes.size());
            return true;
        }
        keepError(err);
        return false;
    };

    // En cada carpeta: primero el nombre clásico NNNNN.SPL y, si no está, el
    // archivo FPnnnnn.SPL del pooling localizado por su .SHD.
    data.searched = SpoolDirectories(search);
    for (const auto& dir : data.searched) {
        if (tryRead(dir + L"\\" + util::Format(L"%05u.SPL", jobId))) {
            util::LogInfo(L"Spool del trabajo %u: %ls (%llu bytes)", jobId, data.path.c_str(),
                          static_cast<unsigned long long>(data.fileSize));
            return data;
        }
        unsigned long err = 0;
        std::wstring pooled = FindPooledSpoolFile(dir, jobId, document, &err);
        if (!pooled.empty() && tryRead(pooled)) {
            util::LogInfo(L"Spool del trabajo %u: %ls (%llu bytes)", jobId, data.path.c_str(),
                          static_cast<unsigned long long>(data.fileSize));
            return data;
        }
        keepError(err);
        // Una línea de diagnóstico por carpeta (D21), para la UI y el log.
        if (!data.diagnostics.empty()) data.diagnostics += L"\n";
        data.diagnostics += dir + L" — " + util::Win32ErrorMessage(err ? err : ERROR_FILE_NOT_FOUND);
    }
    if (data.searched.empty()) data.diagnostics = util::Win32ErrorMessage(data.error);
    util::LogError(L"No se encontró el spool del trabajo %u ('%ls'). Carpetas probadas:\n%ls", jobId, document.c_str(),
                   data.diagnostics.c_str());
    return data;
}

// Corre en hilos de fondo (PreviewService, JobFactsService, reimpresión).
// Las lecturas ReadPrinter quedan serializadas en todo el proceso dentro de
// core::spooler::ReadJobData (D32).
SpoolData ReadJobSpool(const SpoolSearch& search, const core::JobInfo& job, std::uint64_t maxBytes, bool allowFile) {
    // 1. Carpeta alternativa configurada a mano: tiene prioridad (pruebas, copias del spool).
    if (!search.overrideDir.empty()) {
        SpoolData data = ReadSpoolFile(search, job.id, job.document, maxBytes);
        if (data.error == 0) {
            data.origin = SpoolOrigin::File;
            return data;
        }
    }

    SpoolData data;
    if (job.rawStatus & JOB_STATUS_SPOOLING) {
        // ReadPrinter esperaría a que llegaran más datos: se intenta al terminar la recepción.
        data.stillSpooling = true;
        data.error = ERROR_BUSY;
        return data;
    }
    if (!core::CanReadJobData(job.rawStatus)) {
        // Se está imprimiendo o puede empezar en cualquier momento: no se toca.
        data.notHeld = true;
        data.error = ERROR_BUSY;
        return data;
    }

    // 2. API del spooler. Un ESC/Page más largo que maxBytes se sigue leyendo
    // sin guardarlo para contar sus páginas: en TotalPages el servidor cuenta el
    // dorso en blanco de la última hoja (D46, D53).
    bool truncated = false;                // ReadJobData cortó en maxBytes
    std::optional<EscPageCounter> counter; // contador incremental (solo ESC/Page)
    bool counterDecided = false;           // ya se decidió si hace falta contar
    // Callback que ReadJobData llama con cada bloque que llega DESPUÉS de
    // llenar maxBytes (bloques que no se guardan). En la primera llamada se
    // decide, con lo ya guardado, si el trabajo es ESC/Page; si no lo es,
    // devuelve false y ReadJobData deja de leer. Si lo es, el contador se
    // alimenta primero con lo guardado y luego con cada bloque. Devolver
    // false también corta la lectura cuando el contador encuentra datos que
    // no entiende (no tiene sentido seguir).
    auto countRest = [&](const std::uint8_t* chunk, size_t size) {
        if (!counterDecided) {
            counterDecided = true;
            if (DetectFormat(data.bytes.data(), data.bytes.size()) != DataFormat::EscPage) return false;
            counter.emplace();
            counter->Feed(data.bytes.data(), data.bytes.size());
        }
        if (!counter) return false;
        counter->Feed(chunk, size);
        return !counter->Failed();
    };
    // Una lectura completa desde el principio; reinicia el contador para que
    // los reintentos no sumen páginas dos veces.
    auto read = [&] {
        counter.reset();
        counterDecided = false;
        return core::spooler::ReadJobData(job.printer, job.id, maxBytes, data.bytes, &truncated, countRest);
    };
    core::OpResult api = read();
    // 0 bytes o bloques sueltos de un trabajo que sí tiene datos: otro programa
    // (otra copia de la app) lo estaba leyendo a la vez. Se reintenta con esperas
    // crecientes y algo de azar para que las copias no vuelvan a coincidir. Cada
    // intento pasa por ReadJobData, que comprueba que siga en pausa: si lo
    // liberan no se vuelve a leer (D39).
    // true si la última lectura salió bien pero llegó vacía o incompleta.
    auto partial = [&] {
        if (!api.ok) return false;
        return data.bytes.empty() ? job.sizeBytes > 0 : ReadLooksIncomplete(job.sizeBytes, data.bytes.size(), truncated);
    };
    // Hasta 5 reintentos (≈ 8 s en total más 0-300 ms de azar en cada uno,
    // derivado del reloj y del id de proceso). D48.
    for (DWORD wait : {250, 500, 1000, 2000, 4000}) {
        if (!partial()) break;
        Sleep(wait + (GetTickCount() ^ GetCurrentProcessId()) % 300);
        api = read();
    }
    if (partial()) {
        // Analizar bloques sueltos daría un formato desconocido y opciones equivocadas.
        util::LogInfo(L"Datos del trabajo %u incompletos (%llu de %llu bytes): otro programa lo leía a la vez",
                      job.id, static_cast<unsigned long long>(data.bytes.size()),
                      static_cast<unsigned long long>(job.sizeBytes));
        data.bytes.clear();
        data.incomplete = true;
        data.error = ERROR_INVALID_DATA;
        return data;
    }
    if (api.ok) {
        data.origin = SpoolOrigin::SpoolerApi;
        data.error = 0;
        data.truncated = truncated;
        // Si se recortó, el tamaño real es el que informa el spooler (o lo
        // leído, si por alguna razón es mayor); si no, lo leído es todo.
        data.fileSize = truncated ? std::max<std::uint64_t>(job.sizeBytes, data.bytes.size()) : data.bytes.size();
        data.format = DetectFormat(data.bytes.data(), data.bytes.size());
        util::LogInfo(L"Datos del trabajo %u leídos por la API del spooler (%llu bytes, %ls)", job.id,
                      static_cast<unsigned long long>(data.bytes.size()), DataFormatName(data.format));
        if (counter) {
            // Solo vale si se llegó al final y no faltan bloques (otra copia de
            // la app pudo llevarse alguno mientras se recorría, D48).
            EscPageCount count = counter->Result();
            const bool whole = !ReadLooksIncomplete(job.sizeBytes, counter->BytesFed(), false);
            util::LogInfo(L"Páginas ESC/Page del trabajo %u contadas en %llu bytes: %u (%ls)", job.id,
                          static_cast<unsigned long long>(counter->BytesFed()), count.pages,
                          count.end != EscPageCount::End::Complete ? L"sin llegar al final"
                          : whole                                   ? L"completo"
                                                                    : L"faltan bloques, no se usa");
            if (count.end == EscPageCount::End::Complete && whole) data.escPageAllData = std::move(count);
        }
        return data;
    }
    if (api.error == ERROR_BUSY) {
        // El estado real (consultado al leer) no permitía leerlo, o se reanudó mientras tanto.
        data.notHeld = true;
        data.error = ERROR_BUSY;
        return data;
    }
    util::LogError(L"ReadPrinter del trabajo %u en '%ls' falló: %lu", job.id, job.printer.c_str(), api.error);
    // Análisis en segundo plano: no se cae al archivo (ver allowFile en el .h).
    if (!allowFile) {
        data.error = api.error;
        data.apiError = api.error;
        return data;
    }

    // 3. Respaldo: archivo de spool (requiere ser administrador del servidor).
    SpoolData file = ReadSpoolFile(search, job.id, job.document, maxBytes);
    file.apiError = api.error;
    if (file.error == 0) {
        file.origin = SpoolOrigin::File;
        return file;
    }
    // Ninguna vía funcionó: el diagnóstico empieza con la línea de la API y
    // sigue con las carpetas probadas.
    std::wstring apiLine = L"API del spooler (OpenPrinter/ReadPrinter) — " + util::Win32ErrorMessage(api.error);
    file.diagnostics = file.diagnostics.empty() ? apiLine : apiLine + L"\n" + file.diagnostics;
    // El motivo que se muestra es el de la API: es la vía principal.
    if (api.error == ERROR_ACCESS_DENIED || file.error == ERROR_FILE_NOT_FOUND || file.error == ERROR_PATH_NOT_FOUND)
        file.error = api.error;
    return file;
}

size_t SkipPjlHeader(const std::uint8_t* data, size_t size) {
    // UEL = ESC%-12345X ; seguido de líneas "@PJL ..." terminadas en LF.
    // Se repite mientras algo avance, porque los drivers mezclan varios UEL,
    // bloques PJL y líneas vacías (p. ej. UEL @PJL... UEL @PJL ENTER ...).
    size_t pos = 0;
    const char* uel = "\x1b%-12345X";
    const size_t uelLen = 9;
    bool changed = true;
    while (changed && pos < size) {
        changed = false;
        if (size - pos >= uelLen && std::memcmp(data + pos, uel, uelLen) == 0) {
            pos += uelLen;
            changed = true;
        }
        if (size - pos >= 4 && std::memcmp(data + pos, "@PJL", 4) == 0) {
            const void* nl = std::memchr(data + pos, '\n', size - pos);
            if (!nl) return size;  // línea PJL sin terminar: no hay cuerpo visible
            pos = static_cast<size_t>(static_cast<const std::uint8_t*>(nl) - data) + 1;
            changed = true;
        }
        while (pos < size && (data[pos] == '\r' || data[pos] == '\n')) {
            ++pos;
            changed = true;
        }
    }
    return pos;
}

// Si se recortó a propósito faltan bytes por diseño: no cuenta como
// incompleta. En otro caso, menos bytes que JOB_INFO_2.Size = incompleta.
bool ReadLooksIncomplete(std::uint64_t jobSize, std::uint64_t bytesRead, bool truncated) {
    return !truncated && bytesRead < jobSize;
}

DataFormat DetectFormat(const std::uint8_t* data, size_t size) {
    if (size < 4) return DataFormat::Unknown;

    // EMF spool: encabezado EMFSPOOL con versión 0x00010000.
    // (MS-EMFSPOOL: el primer registro es EMRI_HEADER con dwVersion = 0x10000.)
    if (ReadU32(data) == 0x00010000u) return DataFormat::EmfSpool;
    // EMF suelto (EMR_HEADER con firma " EMF").
    // EMR_HEADER: iType = 1 en el byte 0 y dSignature = 0x464D4520 (" EMF"
    // en little-endian) en el byte 40.
    if (size >= 44 && ReadU32(data) == 1 && ReadU32(data + 40) == 0x464D4520u) return DataFormat::EmfSpool;
    // Firma de encabezado local de ZIP: un paquete XPS/OpenXPS es un ZIP.
    if (StartsWith(data, size, "PK\x03\x04")) return DataFormat::Xps;

    // Encabezado EJL de Epson ("ESC 01 @EJL ..."): el lenguaje va en "LA=".
    // Antes caía en "empieza con ESC => PCL" y GhostPCL fallaba con ESC/Page.
    DataFormat ejl = EjlLanguage(data, size);
    if (ejl != DataFormat::Unknown) return ejl;

    // Busca "ENTER LANGUAGE=" dentro del encabezado PJL para saber qué sigue.
    // Se mira solo lo que SkipPjlHeader reconoció como encabezado (máx. 4 KB),
    // en mayúsculas. PCLXL se prueba antes que PCL porque "PCL" es prefijo.
    size_t start = SkipPjlHeader(data, size);
    std::string header(reinterpret_cast<const char*>(data), std::min<size_t>(start, 4096));
    for (auto& c : header) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    size_t lang = header.find("ENTER LANGUAGE");
    if (lang != std::string::npos) {
        std::string rest = header.substr(lang, 40);
        if (rest.find("POSTSCRIPT") != std::string::npos) return DataFormat::PostScript;
        if (rest.find("PCLXL") != std::string::npos) return DataFormat::PclXl;
        if (rest.find("PDF") != std::string::npos) return DataFormat::Pdf;
        if (rest.find("PCL") != std::string::npos) return DataFormat::Pcl;
        if (rest.find("XPS") != std::string::npos) return DataFormat::Xps;
    }

    // Sin ENTER LANGUAGE: se miran las firmas del cuerpo tras el PJL.
    const std::uint8_t* body = data + start;
    size_t bodySize = size - start;
    // "%!" = PostScript; 0x04 delante es el Ctrl-D que algunos drivers
    // envían para reiniciar el intérprete PostScript.
    if (StartsWith(body, bodySize, "%PDF")) return DataFormat::Pdf;
    if (StartsWith(body, bodySize, "%!") || StartsWith(body, bodySize, "\x04%!")) return DataFormat::PostScript;
    if (StartsWith(body, bodySize, "PK\x03\x04")) return DataFormat::Xps;
    // PCL XL: ") HP-PCL XL;" en el encabezado de flujo.
    if (StartsWith(body, bodySize, ") HP-PCL XL")) return DataFormat::PclXl;
    // Cualquier otro flujo que empiece con ESC se toma como PCL 5 (los
    // ESC/Page con EJL ya se separaron más arriba).
    if (bodySize > 0 && body[0] == 0x1b) return DataFormat::Pcl;
    // Firmas de PNG y JPEG (SOI + marcador).
    if (StartsWith(body, bodySize, "\x89PNG") || StartsWith(body, bodySize, "\xff\xd8\xff")) return DataFormat::Image;

    // Texto plano: casi todo imprimible.
    // Se muestrean hasta 2 KB; cuentan como imprimibles ASCII visible,
    // tabulador, saltos de línea/página y bytes >= 0xA0 (Latin-1 / parte de
    // UTF-8). Umbral: 97 %, para tolerar algún byte de control suelto.
    size_t sample = std::min<size_t>(bodySize, 2048);
    if (sample > 0) {
        size_t printable = 0;
        for (size_t i = 0; i < sample; ++i) {
            std::uint8_t c = body[i];
            if (c == '\n' || c == '\r' || c == '\t' || c == '\f' || (c >= 0x20 && c < 0x7f) || c >= 0xa0) ++printable;
        }
        if (printable * 100 / sample >= 97) return DataFormat::Text;
    }
    return DataFormat::Unknown;
}

}  // namespace cola::preview
