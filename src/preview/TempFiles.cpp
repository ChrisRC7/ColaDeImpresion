#include "preview/TempFiles.h"

#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <cwchar>
#include <mutex>
#include <vector>

#include "util/Log.h"
#include "util/Strings.h"

namespace cola::preview {

namespace {

std::mutex g_mutex;
std::wstring g_dir;
bool g_tried = false;
std::atomic<unsigned> g_counter{0};

std::wstring BaseDir() {
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    return std::wstring(temp) + L"ColaDeImpresion";
}

// SID del usuario del proceso en formato texto ("S-1-5-21-...").
std::wstring CurrentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return L"";
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    std::wstring result;
    if (size && GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        LPWSTR text = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &text)) {
            result = text;
            LocalFree(text);
        }
    }
    CloseHandle(token);
    return result;
}

// Crea una carpeta con DACL protegida: acceso total solo para el usuario actual.
bool CreatePrivateDirectory(const std::wstring& path) {
    const std::wstring sid = CurrentUserSid();
    if (sid.empty()) return false;
    // D:P = DACL protegida (no hereda de %TEMP%); OICI = la heredan archivos y subcarpetas.
    const std::wstring sddl = L"D:P(A;OICI;FA;;;" + sid + L")";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr))
        return false;
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    BOOL ok = CreateDirectoryW(path.c_str(), &sa);
    LocalFree(sd);
    return ok != FALSE;
}

void DeleteTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == L'.') continue;
            std::wstring child = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(child);
            else DeleteFileW(child.c_str());
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    RemoveDirectoryW(dir.c_str());
}

bool ProcessAlive(DWORD pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return GetLastError() == ERROR_ACCESS_DENIED;  // existe, pero es de otro usuario
    bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

}  // namespace

std::wstring PrivateTempDir() {
    std::lock_guard lock(g_mutex);
    if (g_tried) return g_dir;
    g_tried = true;
    const std::wstring base = BaseDir();
    CreateDirectoryW(base.c_str(), nullptr);
    std::wstring dir = base + util::Format(L"\\s%lu-%llu", GetCurrentProcessId(),
                                           static_cast<unsigned long long>(GetTickCount64()));
    if (CreatePrivateDirectory(dir)) {
        g_dir = dir;
    } else {
        util::LogError(L"No se pudo crear la carpeta temporal privada %ls: %lu", dir.c_str(), GetLastError());
    }
    return g_dir;
}

std::wstring NewTempPath(const wchar_t* extension) {
    std::wstring dir = PrivateTempDir();
    if (dir.empty()) return L"";
    return dir + util::Format(L"\\t%u%ls", ++g_counter, extension ? extension : L"");
}

void RemovePrivateTempDir() {
    std::lock_guard lock(g_mutex);
    if (!g_dir.empty()) DeleteTree(g_dir);
    g_dir.clear();
    g_tried = false;
}

void CleanupStaleTempFiles() {
    const std::wstring base = BaseDir();
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((base + L"\\*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == L'.') continue;
            std::wstring path = base + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                // s<pid>-<tick>: se borra si el proceso que la creó ya no existe.
                DWORD pid = 0;
                if (swscanf(fd.cFileName, L"s%lu-", &pid) == 1 && pid != GetCurrentProcessId() && !ProcessAlive(pid))
                    DeleteTree(path);
            } else {
                // Archivos sueltos que dejaban versiones anteriores (r<pid>_<n>.*).
                if (fd.cFileName[0] == L'r') DeleteFileW(path.c_str());
            }
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    // Miniaturas temporales de versiones anteriores en la raíz de %TEMP%.
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    find = FindFirstFileW((std::wstring(temp) + L"ColaDeImpresion_thumb_*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            DeleteFileW((std::wstring(temp) + fd.cFileName).c_str());
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
}

}  // namespace cola::preview
