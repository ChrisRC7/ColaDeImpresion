#include "config/Paths.h"

#include <windows.h>
#include <shlobj.h>

namespace cola::config {

std::wstring ExecutablePath() {
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
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

bool IsPortableMode() {
    std::wstring flag = ExecutableDir() + L"\\portable.txt";
    return GetFileAttributesW(flag.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool EnsureDirectory(const std::wstring& path) {
    int rc = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS;
}

std::wstring DataDir() {
    static std::wstring dir = [] {
        if (IsPortableMode()) return ExecutableDir();
        std::wstring result;
        PWSTR appData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
            result = std::wstring(appData) + L"\\ColaDeImpresion";
            CoTaskMemFree(appData);
        } else {
            result = ExecutableDir();
        }
        EnsureDirectory(result);
        return result;
    }();
    return dir;
}

namespace {

std::wstring& SettingsFileOverride() {
    static std::wstring path;
    return path;
}

std::wstring KnownFolderSubdir(REFKNOWNFOLDERID folder) {
    std::wstring result;
    PWSTR base = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(folder, 0, nullptr, &base))) {
        result = std::wstring(base) + L"\\ColaDeImpresion";
        CoTaskMemFree(base);
    }
    return result;
}

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

void SetSettingsFileOverride(const std::wstring& path) { SettingsFileOverride() = path; }

std::wstring SettingsFile() {
    static std::wstring file = [] {
        if (!SettingsFileOverride().empty()) return SettingsFileOverride();
        std::wstring dir = KnownFolderSubdir(FOLDERID_LocalAppData);
        if (dir.empty()) return DataDir() + L"\\config.json";
        EnsureDirectory(dir);
        std::wstring result = dir + L"\\config.json";
        // Primera vez con la configuración por usuario: se parte de la que se
        // usaba antes (%APPDATA% o, en modo portable, la de junto al .exe).
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
