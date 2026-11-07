// Implementación de las rutas de la aplicación (ver Paths.h, D11 y D49).

#include "config/Paths.h"

#include <windows.h>
#include <shlobj.h>

namespace cola::config {

std::wstring ExecutablePath() {
    // GetModuleFileNameW no informa el largo necesario: si el búfer se llena
    // (len == tamaño) la ruta pudo quedar truncada, así que se duplica el
    // búfer y se reintenta. Cubre rutas largas (\\?\..., > 260 caracteres).
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (len < path.size()) {
            path.resize(len);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring ExecutableDir() {
    std::wstring path = ExecutablePath();
    // Se acepta "\" o "/" por si la ruta viniera con barras normales.
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

bool IsPortableMode() {
    // Basta con que exista algo llamado portable.txt (su contenido no importa).
    std::wstring flag = ExecutableDir() + L"\\portable.txt";
    return GetFileAttributesW(flag.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool EnsureDirectory(const std::wstring& path) {
    // SHCreateDirectoryExW crea también las carpetas intermedias. "Ya existe"
    // cuenta como éxito; ERROR_FILE_EXISTS aparece si ya hay algo con ese nombre.
    int rc = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS;
}

std::wstring DataDir() {
    // Se calcula la primera vez (inicialización de static local segura entre
    // hilos) y se reutiliza.
    static std::wstring dir = [] {
        // Portable: los datos viajan con el .exe (carpeta compartida o USB).
        if (IsPortableMode()) return ExecutableDir();
        std::wstring result;
        PWSTR appData = nullptr;
        // %APPDATA% (Roaming): el historial sigue al usuario si el perfil es
        // móvil. SHGetKnownFolderPath reserva la cadena con CoTaskMemAlloc.
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
            result = std::wstring(appData) + L"\\ColaDeImpresion";
            CoTaskMemFree(appData);
        } else {
            // Caso muy raro (perfil dañado): se usa la carpeta del .exe.
            result = ExecutableDir();
        }
        EnsureDirectory(result);
        return result;
    }();
    return dir;
}

namespace {

// Ruta de --config (vacía = no hay). Función con static local en vez de una
// variable global para no depender del orden de inicialización entre archivos.
std::wstring& SettingsFileOverride() {
    static std::wstring path;
    return path;
}

// "<carpeta conocida>\ColaDeImpresion" (sin crearla); vacía si Windows no
// puede dar la carpeta.
std::wstring KnownFolderSubdir(REFKNOWNFOLDERID folder) {
    std::wstring result;
    PWSTR base = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(folder, 0, nullptr, &base))) {
        result = std::wstring(base) + L"\\ColaDeImpresion";
        CoTaskMemFree(base);
    }
    return result;
}

// true si la ruta existe y es un archivo (no una carpeta).
bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

void SetSettingsFileOverride(const std::wstring& path) { SettingsFileOverride() = path; }

std::wstring SettingsFile() {
    // Se resuelve una sola vez por proceso (ver Paths.h).
    static std::wstring file = [] {
        // --config (desarrollo) manda sobre todo lo demás y no se migra nada.
        if (!SettingsFileOverride().empty()) return SettingsFileOverride();
        // D49: %LOCALAPPDATA% (Local, no Roaming) aun en modo portable, para
        // que cada usuario de Windows tenga sus propias preferencias.
        std::wstring dir = KnownFolderSubdir(FOLDERID_LocalAppData);
        if (dir.empty()) return DataDir() + L"\\config.json";
        EnsureDirectory(dir);
        std::wstring result = dir + L"\\config.json";
        // Primera vez con la configuración por usuario: se parte de la que se
        // usaba antes (%APPDATA% o, en modo portable, la de junto al .exe).
        // CopyFileW con TRUE (no sobrescribir) por si otra instancia lo creó
        // entre la comprobación y la copia. El original no se borra.
        if (!FileExists(result)) {
            std::wstring roaming = KnownFolderSubdir(FOLDERID_RoamingAppData);
            if (!roaming.empty()) roaming += L"\\config.json";
            std::wstring portable = ExecutableDir() + L"\\config.json";
            if (!roaming.empty() && FileExists(roaming)) {
                CopyFileW(roaming.c_str(), result.c_str(), TRUE);
            } else if (IsPortableMode() && FileExists(portable)) {
                CopyFileW(portable.c_str(), result.c_str(), TRUE);
            }
        }
        return result;
    }();
    return file;
}

// Carpeta que contiene config.json (también con --config). Es la que abre
// el menú "Abrir carpeta de configuración".
std::wstring SettingsDir() {
    std::wstring file = SettingsFile();
    size_t slash = file.find_last_of(L"\\/");
    std::wstring dir = slash == std::wstring::npos ? L"." : file.substr(0, slash);
    EnsureDirectory(dir);
    return dir;
}

std::wstring HistoryFile() { return DataDir() + L"\\historial.json"; }
std::wstring LogFile() { return DataDir() + L"\\app.log"; }

}  // namespace cola::config
