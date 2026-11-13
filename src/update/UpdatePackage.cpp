// Lectura e instalación del paquete de actualización. Ver UpdatePackage.h.
//
// Estructuras ZIP que se interpretan (little-endian, formato PKWARE APPNOTE):
//   Fin del directorio central (EOCD, 22 bytes + comentario), firma "PK\5\6":
//     +10 total de entradas  +12 tamaño del directorio  +16 su posición
//   Entrada del directorio central (46 bytes + nombre + extra + comentario),
//   firma "PK\1\2":
//     +8 flags (bit 0 = cifrada)  +10 método  +16 CRC-32  +20 tamaño comprimido
//     +24 tamaño sin comprimir  +28 largo del nombre  +30 largo del extra
//     +32 largo del comentario  +42 posición del encabezado local
//   Encabezado local (30 bytes + nombre + extra + datos), firma "PK\3\4":
//     +26 largo del nombre  +28 largo del extra
// Los tamaños y el CRC se toman del directorio central: en el encabezado local
// pueden venir en cero (cuando el ZIP se escribió con "data descriptor").
#include "update/UpdatePackage.h"

#include <windows.h>

#include <algorithm>
#include <set>

#include "config/Paths.h"
#include "util/Inflate.h"
#include "util/Log.h"
#include "util/Strings.h"
#include "util/ZipWriter.h"

namespace cola::update {

namespace {

// Lectores little-endian byte a byte.
std::uint32_t U32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint16_t U16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

constexpr std::uint32_t kEndOfCentralDir = 0x06054b50;  // "PK\5\6"
constexpr std::uint32_t kCentralDirEntry = 0x02014b50;  // "PK\1\2"
constexpr std::uint32_t kLocalHeader = 0x04034b50;      // "PK\3\4"

bool Fail(std::wstring* error, const std::wstring& message) {
    if (error) *error = message;
    return false;
}

// Nombre del archivo sin su carpeta.
std::wstring FileName(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// Carpeta de una ruta ("" si no tiene).
std::wstring ParentDir(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}

bool FileExists(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// Lee un archivo completo (compartiendo lectura, escritura y borrado para
// poder leer también un .exe en uso). false si no se puede.
bool ReadWholeFile(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart <= 0x7FFFFFFF;
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = out.empty() || (ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) &&
                             read == out.size());
    }
    CloseHandle(file);
    return ok;
}

// Escribe un archivo nuevo completo (falla si ya existe).
bool WriteWholeFile(const std::wstring& path, const std::string& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = data.empty() ||
              (WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size());
    // El contenido debe estar en el disco antes de renombrar sobre el original.
    if (ok) FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

// Borra una carpeta con todo su contenido (solo se usa con la carpeta temporal).
void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW found{};
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = found.cFileName;
            if (name == L"." || name == L"..") continue;
            std::wstring path = dir + L"\\" + name;
            // Un enlace o punto de montaje no se recorre: se quita el enlace.
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                RemoveTree(path);
            } else if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirectoryW(path.c_str());
            } else {
                SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(path.c_str());
            }
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    RemoveDirectoryW(dir.c_str());
}

// Bloqueo entre copias de la app (otra sesión de Windows, otra instancia): un
// archivo abierto sin compartir que se borra solo al cerrarlo.
class InstallLock {
public:
    explicit InstallLock(const std::wstring& dir) {
        handle_ = CreateFileW((dir + L"\\" + kLockFile).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        error_ = handle_ == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    }
    ~InstallLock() {
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    }
    InstallLock(const InstallLock&) = delete;
    InstallLock& operator=(const InstallLock&) = delete;
    bool held() const { return handle_ != INVALID_HANDLE_VALUE; }
    DWORD error() const { return error_; }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    DWORD error_ = ERROR_SUCCESS;
};

// Nombre libre para apartar el archivo instalado: "<ruta>.cola-old" o, si ese
// sigue en uso por otra sesión que corre una versión aún más vieja,
// "<ruta>.1.cola-old"... "" si no hay ninguno libre.
std::wstring FreeOldName(const std::wstring& path) {
    for (int i = 0; i < 10; ++i) {
        std::wstring name = path + (i ? util::Format(L".%d", i) : std::wstring()) + kOldSuffix;
        if (!FileExists(name)) return name;
        SetFileAttributesW(name.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(name.c_str())) return name;
    }
    return L"";
}

// Recorrido de CleanupOldFiles.
void DeleteOldFilesIn(const std::wstring& dir, int depth) {
    WIN32_FIND_DATAW found{};
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) return;
    const std::wstring suffix = kOldSuffix;
    do {
        std::wstring name = found.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring path = dir + L"\\" + name;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth > 0 && !(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) DeleteOldFilesIn(path, depth - 1);
        } else if (name.size() > suffix.size() &&
                   util::EqualsInsensitive(name.substr(name.size() - suffix.size()), suffix)) {
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileW(path.c_str())) util::LogInfo(L"Actualización: borrado %ls", path.c_str());
        }
    } while (FindNextFileW(find, &found));
    FindClose(find);
}

}  // namespace

bool IsSafeRelativePath(const std::wstring& pathIn) {
    if (pathIn.empty() || pathIn.size() >= 200) return false;
    std::wstring path = pathIn;
    std::replace(path.begin(), path.end(), L'/', L'\\');
    static const std::set<std::wstring> kDevices = {L"con",  L"prn",  L"aux",  L"nul",  L"com1", L"com2", L"com3", L"com4",
                                                    L"com5", L"com6", L"com7", L"com8", L"com9", L"lpt1", L"lpt2", L"lpt3",
                                                    L"lpt4", L"lpt5", L"lpt6", L"lpt7", L"lpt8", L"lpt9"};
    for (const auto& part : util::Split(path, L'\\')) {
        // Vacío = barra inicial, final o doble (también descarta rutas UNC).
        if (part.empty() || part == L"." || part == L"..") return false;
        for (wchar_t c : part)
            if (c < 32 || c == L':' || c == L'<' || c == L'>' || c == L'"' || c == L'|' || c == L'?' || c == L'*')
                return false;
        // Windows quita los puntos y espacios finales: "x." sería otro archivo.
        if (part.back() == L'.' || part.back() == L' ') return false;
        // Nombres de dispositivo, con o sin extensión ("nul.txt").
        std::wstring base = util::ToLower(part.substr(0, part.find(L'.')));
        if (kDevices.count(base)) return false;
    }
    return true;
}

bool IsProtectedFile(const std::wstring& path) {
    std::wstring name = util::ToLower(FileName(path));
    static const wchar_t* const kNames[] = {L"servidor-laimi.txt", L"portable.txt", L"config.json", L"historial.json"};
    for (const wchar_t* fixed : kNames)
        if (name == fixed) return true;
    // app.log, app.log.1 (rotación), config.json.danado, historial.json.danado...
    return name.rfind(L"app.log", 0) == 0 || name.rfind(L"config.json.", 0) == 0 ||
           name.rfind(L"historial.json.", 0) == 0;
}

bool ReadPackage(std::string_view zip, std::vector<PackageEntry>& entries, std::wstring* error) {
    entries.clear();
    const auto* data = reinterpret_cast<const unsigned char*>(zip.data());
    const size_t size = zip.size();
    if (size < 22) return Fail(error, L"el paquete no es un archivo ZIP");
    // El EOCD se busca desde el final (el comentario mide como máximo 64 KB).
    size_t eocd = std::string::npos;
    size_t minPos = size > 22 + 65535 ? size - 22 - 65535 : 0;
    for (size_t i = size - 22 + 1; i-- > minPos;) {
        if (U32(data + i) == kEndOfCentralDir) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) return Fail(error, L"el paquete no es un archivo ZIP");
    const std::uint16_t total = U16(data + eocd + 10);
    size_t pos = U32(data + eocd + 16);
    if (total == 0xFFFF || pos == 0xFFFFFFFFu) return Fail(error, L"el paquete usa ZIP64, que no se admite");

    std::set<std::wstring> seen;  // rutas en minúsculas, para detectar repetidas
    for (std::uint16_t i = 0; i < total; ++i) {
        if (pos > size || size - pos < 46 || U32(data + pos) != kCentralDirEntry)
            return Fail(error, L"el directorio del paquete está dañado");
        const std::uint16_t flags = U16(data + pos + 8);
        PackageEntry entry;
        entry.method = U16(data + pos + 10);
        entry.crc = U32(data + pos + 16);
        entry.compressedSize = U32(data + pos + 20);
        entry.size = U32(data + pos + 24);
        const std::uint16_t nameLen = U16(data + pos + 28);
        const std::uint16_t extraLen = U16(data + pos + 30);
        const std::uint16_t commentLen = U16(data + pos + 32);
        const size_t localOffset = U32(data + pos + 42);
        if (size - pos - 46 < static_cast<size_t>(nameLen) + extraLen + commentLen)
            return Fail(error, L"el directorio del paquete está dañado");
        std::string name(reinterpret_cast<const char*>(data + pos + 46), nameLen);
        pos += 46 + static_cast<size_t>(nameLen) + extraLen + commentLen;

        // Las carpetas (nombre terminado en "/") no llevan datos: se crean
        // solas al extraer los archivos que contienen.
        if (!name.empty() && (name.back() == '/' || name.back() == '\\')) continue;
        std::wstring path = util::FromUtf8(name);
        if (!IsSafeRelativePath(path)) return Fail(error, L"el paquete trae una ruta no permitida: " + path);
        std::replace(path.begin(), path.end(), L'/', L'\\');
        if (!seen.insert(util::ToLower(path)).second) return Fail(error, L"el paquete trae un archivo repetido: " + path);
        if (flags & 1) return Fail(error, L"el paquete trae archivos cifrados");
        if (entry.method != 0 && entry.method != 8)
            return Fail(error, L"el paquete usa un método de compresión no admitido");
        if (entry.size == 0xFFFFFFFFu || entry.compressedSize == 0xFFFFFFFFu || localOffset == 0xFFFFFFFFu)
            return Fail(error, L"el paquete usa ZIP64, que no se admite");
        if (entry.method == 0 && entry.size != entry.compressedSize)
            return Fail(error, L"el directorio del paquete está dañado");
        // Los datos empiezan tras el encabezado local, su nombre y su extra.
        if (localOffset > size || size - localOffset < 30 || U32(data + localOffset) != kLocalHeader)
            return Fail(error, L"el paquete está dañado (encabezado local)");
        const size_t headerLen =
            30 + static_cast<size_t>(U16(data + localOffset + 26)) + U16(data + localOffset + 28);
        if (size - localOffset < headerLen || size - localOffset - headerLen < entry.compressedSize)
            return Fail(error, L"el paquete está cortado");
        entry.dataOffset = localOffset + headerLen;
        entry.path = std::move(path);
        entries.push_back(std::move(entry));
    }
    if (entries.empty()) return Fail(error, L"el paquete está vacío");
    return true;
}

bool ExtractEntry(std::string_view zip, const PackageEntry& entry, std::string& out) {
    if (entry.dataOffset > zip.size() || zip.size() - entry.dataOffset < entry.compressedSize) return false;
    std::string_view raw = zip.substr(entry.dataOffset, entry.compressedSize);
    if (entry.method == 0) {
        out.assign(raw);
    } else if (entry.method == 8) {
        if (!util::Inflate(reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size(), entry.size, out)) return false;
    } else {
        return false;
    }
    return out.size() == entry.size && util::Crc32(out) == entry.crc;
}

bool CanWriteTo(const std::wstring& targetDir) {
    InstallLock lock(targetDir);
    // Ocupado por otra copia = sí se puede escribir (lo está haciendo ella).
    return lock.held() || lock.error() == ERROR_SHARING_VIOLATION;
}

InstallResult InstallPackage(std::string_view zip, const std::wstring& targetDir) {
    InstallResult result;
    std::vector<PackageEntry> entries;
    if (!ReadPackage(zip, entries, &result.error)) return result;
    bool hasExe = false;
    for (const auto& e : entries) hasExe = hasExe || util::EqualsInsensitive(e.path, L"ColaDeImpresion.exe");
    if (!hasExe) {
        result.error = L"el paquete no trae ColaDeImpresion.exe";
        return result;
    }

    InstallLock lock(targetDir);
    if (!lock.held()) {
        result.busy = lock.error() == ERROR_SHARING_VIOLATION;
        result.accessDenied = !result.busy;
        result.error = result.busy ? L"otra copia de la app está instalando la actualización"
                                   : L"no se puede escribir en la carpeta de la app: " +
                                         util::Win32ErrorMessage(lock.error());
        return result;
    }

    // --- 1. Extraer a la carpeta temporal ---
    const std::wstring staging = targetDir + L"\\" + kStagingDir;
    RemoveTree(staging);  // restos de una instalación interrumpida
    if (!config::EnsureDirectory(staging)) {
        result.accessDenied = GetLastError() == ERROR_ACCESS_DENIED;
        result.error = L"no se pudo crear la carpeta temporal de la actualización";
        return result;
    }
    SetFileAttributesW(staging.c_str(), FILE_ATTRIBUTE_HIDDEN);

    struct Pending {
        std::wstring staged;  // archivo nuevo en la carpeta temporal
        std::wstring dest;    // dónde va
    };
    std::vector<Pending> pending;
    std::string content, existing;
    for (const auto& entry : entries) {
        if (IsProtectedFile(entry.path)) continue;
        if (!ExtractEntry(zip, entry, content)) {
            result.error = L"el paquete está dañado: " + entry.path;
            RemoveTree(staging);
            return result;
        }
        const std::wstring dest = targetDir + L"\\" + entry.path;
        // Idéntico al instalado: no se toca (evita renombrar sin necesidad).
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (GetFileAttributesExW(dest.c_str(), GetFileExInfoStandard, &info) && info.nFileSizeHigh == 0 &&
            info.nFileSizeLow == content.size() && ReadWholeFile(dest, existing) && existing == content) {
            ++result.unchanged;
            continue;
        }
        const std::wstring staged = staging + L"\\" + entry.path;
        if (!config::EnsureDirectory(ParentDir(staged)) || !WriteWholeFile(staged, content)) {
            result.error = L"no se pudo escribir " + staged + L": " + util::Win32ErrorMessage(GetLastError());
            RemoveTree(staging);
            return result;
        }
        pending.push_back({staged, dest});
    }

    // --- 2. Poner cada archivo en su lugar, recordando cómo deshacerlo ---
    struct Done {
        std::wstring dest;
        std::wstring old;  // dónde quedó el archivo anterior ("" si no había)
    };
    std::vector<Done> done;
    bool ok = true;
    for (const auto& item : pending) {
        Done step{item.dest, L""};
        ok = ParentDir(item.dest).empty() || config::EnsureDirectory(ParentDir(item.dest));
        if (ok && FileExists(item.dest)) {
            step.old = FreeOldName(item.dest);
            ok = !step.old.empty() && MoveFileExW(item.dest.c_str(), step.old.c_str(), 0);
            if (!ok) step.old.clear();
        }
        if (ok) {
            done.push_back(step);
            ok = MoveFileExW(item.staged.c_str(), item.dest.c_str(), 0) != 0;
        }
        if (!ok) {
            DWORD code = GetLastError();
            result.accessDenied = code == ERROR_ACCESS_DENIED;
            result.error = L"no se pudo reemplazar " + item.dest + L": " + util::Win32ErrorMessage(code);
            break;
        }
    }

    // --- 3. Si algo falló, deshacer en orden inverso ---
    if (!ok) {
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            DeleteFileW(it->dest.c_str());
            if (!it->old.empty() && !MoveFileExW(it->old.c_str(), it->dest.c_str(), MOVEFILE_REPLACE_EXISTING))
                util::LogError(L"Actualización: no se pudo restaurar %ls desde %ls (%lu)", it->dest.c_str(),
                               it->old.c_str(), GetLastError());
        }
        RemoveTree(staging);
        return result;
    }

    RemoveTree(staging);
    // Los apartados que nadie usa se borran ya; el .exe en ejecución (y lo
    // que tenga abierto otra sesión) queda para CleanupOldFiles.
    for (const auto& step : done)
        if (!step.old.empty()) DeleteFileW(step.old.c_str());
    result.ok = true;
    result.replaced = static_cast<int>(pending.size());
    return result;
}

void CleanupOldFiles(const std::wstring& targetDir) {
    // Si otra copia está instalando ahora mismo, no se le borra nada.
    InstallLock lock(targetDir);
    if (!lock.held()) return;
    const std::wstring staging = targetDir + L"\\" + kStagingDir;
    if (GetFileAttributesW(staging.c_str()) != INVALID_FILE_ATTRIBUTES) RemoveTree(staging);
    DeleteOldFilesIn(targetDir, 4);
}

}  // namespace cola::update
