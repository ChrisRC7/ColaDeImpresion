#include "util/Strings.h"

#include <windows.h>

#include <cstdarg>
#include <cwctype>

namespace cola::util {

std::string ToUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                   nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr,
                        nullptr);
    return out;
}

std::wstring FromUtf8(std::string_view text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::wstring ToLower(std::wstring_view text) {
    std::wstring out(text);
    if (!out.empty()) CharLowerBuffW(out.data(), static_cast<DWORD>(out.size()));
    return out;
}

std::wstring Trim(std::wstring_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::iswspace(text[begin])) ++begin;
    while (end > begin && std::iswspace(text[end - 1])) --end;
    return std::wstring(text.substr(begin, end - begin));
}

bool ContainsInsensitive(std::wstring_view haystack, std::wstring_view needle) {
    if (needle.empty()) return true;
    return ToLower(haystack).find(ToLower(needle)) != std::wstring::npos;
}

bool EqualsInsensitive(std::wstring_view a, std::wstring_view b) {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()),
                                TRUE) == CSTR_EQUAL;
}

std::vector<std::wstring> Split(std::wstring_view text, wchar_t separator) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= text.size()) {
        size_t pos = text.find(separator, start);
        if (pos == std::wstring_view::npos) pos = text.size();
        parts.emplace_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

std::wstring Format(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int size = _vscwprintf(fmt, copy);
    va_end(copy);
    std::wstring out;
    if (size > 0) {
        out.resize(static_cast<size_t>(size) + 1);
        _vsnwprintf_s(out.data(), out.size(), _TRUNCATE, fmt, args);
        out.resize(static_cast<size_t>(size));
    }
    va_end(args);
    return out;
}

std::wstring Win32ErrorMessage(unsigned long error) {
    wchar_t* buffer = nullptr;
    DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message;
    if (len && buffer) {
        message.assign(buffer, len);
        LocalFree(buffer);
    }
    message = Trim(message);
    if (message.empty()) message = Format(L"Error %lu", error);
    return message;
}

}  // namespace cola::util
