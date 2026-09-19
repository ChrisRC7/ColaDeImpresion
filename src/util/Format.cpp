#include "util/Format.h"

#include "util/Strings.h"

namespace cola::util {

std::wstring FormatBytes(std::uint64_t bytes) {
    if (bytes == 0) return L"—";
    static const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) return Format(L"%llu B", static_cast<unsigned long long>(bytes));
    return Format(value >= 100.0 ? L"%.0f %ls" : L"%.1f %ls", value, units[unit]);
}

static SYSTEMTIME ToLocal(const SYSTEMTIME& utc) {
    SYSTEMTIME local{};
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) local = utc;
    return local;
}

std::wstring FormatTimeShort(const SYSTEMTIME& utc) {
    if (utc.wYear == 0) return L"";
    SYSTEMTIME local = ToLocal(utc);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    if (now.wYear == local.wYear && now.wMonth == local.wMonth && now.wDay == local.wDay)
        return Format(L"%02u:%02u", local.wHour, local.wMinute);
    return Format(L"%02u/%02u %02u:%02u", local.wDay, local.wMonth, local.wHour, local.wMinute);
}

std::wstring FormatTimeIso(const SYSTEMTIME& utc) {
    if (utc.wYear == 0) return L"";
    SYSTEMTIME l = ToLocal(utc);
    return Format(L"%04u-%02u-%02u %02u:%02u:%02u", l.wYear, l.wMonth, l.wDay, l.wHour, l.wMinute, l.wSecond);
}

std::wstring FormatDateKey(const SYSTEMTIME& utc) {
    SYSTEMTIME l = ToLocal(utc);
    return Format(L"%04u-%02u-%02u", l.wYear, l.wMonth, l.wDay);
}

SYSTEMTIME NowUtc() {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    return st;
}

// Diferencia entre 1601-01-01 (FILETIME) y 1970-01-01 en intervalos de 100 ns.
static constexpr std::int64_t kEpochDelta = 116444736000000000LL;

std::int64_t ToUnixSeconds(const SYSTEMTIME& utc) {
    FILETIME ft{};
    if (!SystemTimeToFileTime(&utc, &ft)) return 0;
    ULARGE_INTEGER li{};
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return (static_cast<std::int64_t>(li.QuadPart) - kEpochDelta) / 10000000LL;
}

SYSTEMTIME FromUnixSeconds(std::int64_t seconds) {
    ULARGE_INTEGER li{};
    li.QuadPart = static_cast<ULONGLONG>(seconds * 10000000LL + kEpochDelta);
    FILETIME ft{li.LowPart, li.HighPart};
    SYSTEMTIME st{};
    FileTimeToSystemTime(&ft, &st);
    return st;
}

}  // namespace cola::util
