#include "util/Log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

#include "util/Strings.h"

namespace cola::util {

namespace {
std::mutex g_mutex;
std::wstring g_path;
constexpr LONGLONG kMaxLogBytes = 2 * 1024 * 1024;  // rota al superar 2 MB

void Write(const wchar_t* level, const wchar_t* fmt, va_list args) {
    std::lock_guard lock(g_mutex);
    if (g_path.empty()) return;

    wchar_t message[2048];
    _vsnwprintf_s(message, _countof(message), _TRUNCATE, fmt, args);

    SYSTEMTIME t{};
    GetLocalTime(&t);
    std::wstring line = Format(L"%04u-%02u-%02u %02u:%02u:%02u [%ls] %ls\r\n", t.wYear, t.wMonth, t.wDay, t.wHour,
                               t.wMinute, t.wSecond, level, message);
    std::string utf8 = ToUtf8(line);

    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(g_path.c_str(), GetFileExInfoStandard, &info)) {
        LONGLONG size = (static_cast<LONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        if (size > kMaxLogBytes) MoveFileExW(g_path.c_str(), (g_path + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    HANDLE file = CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
#ifndef NDEBUG
    OutputDebugStringW(line.c_str());
#endif
}
}  // namespace

void LogInit(const std::wstring& path) {
    std::lock_guard lock(g_mutex);
    g_path = path;
}

void LogInfo(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Write(L"INFO", fmt, args);
    va_end(args);
}

void LogError(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Write(L"ERROR", fmt, args);
    va_end(args);
}

}  // namespace cola::util
