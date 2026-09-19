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

std::wstring SettingsFile() { return DataDir() + L"\\config.json"; }
std::wstring HistoryFile() { return DataDir() + L"\\historial.json"; }
std::wstring LogFile() { return DataDir() + L"\\app.log"; }

}  // namespace cola::config
