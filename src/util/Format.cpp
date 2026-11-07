// Implementación del formato de tamaños y fechas para la UI (ver Format.h).
// Todas las cadenas se arman con util::Format (printf de cadenas anchas).

#include "util/Format.h"

#include "util/Strings.h"

namespace cola::util {

std::wstring FormatBytes(std::uint64_t bytes) {
    // 0 = tamaño desconocido (trabajo recién creado, o "Microsoft Print to
    // PDF", que baja Size a 0 al imprimirse; ver D42).
    if (bytes == 0) return L"—";
    static const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    // Se divide por 1024 hasta que el valor quede por debajo de 1024 o se
    // llegue a la última unidad (TB, índice 4).
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    // Bytes: entero exacto, sin decimales.
    if (unit == 0) return Format(L"%llu B", static_cast<unsigned long long>(bytes));
    // Desde 100 el decimal no aporta y alarga la columna.
    return Format(value >= 100.0 ? L"%.0f %ls" : L"%.1f %ls", value, units[unit]);
}

SYSTEMTIME ToLocalTime(const SYSTEMTIME& utc) {
    SYSTEMTIME local{};
    // nullptr = zona horaria activa del equipo. Si falla (fecha inválida), se
    // devuelve la misma hora para no mostrar basura.
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) local = utc;
    return local;
}

// Alias interno corto usado por las funciones de formato de este archivo.
static SYSTEMTIME ToLocal(const SYSTEMTIME& utc) { return ToLocalTime(utc); }

std::wstring FormatTimeShort(const SYSTEMTIME& utc) {
    if (utc.wYear == 0) return L"";
    SYSTEMTIME local = ToLocal(utc);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    // Hoy: basta la hora. Otro día: se antepone día/mes (formato del TEC,
    // día primero) para que no se confunda con un trabajo de hoy.
    if (now.wYear == local.wYear && now.wMonth == local.wMonth && now.wDay == local.wDay)
        return Format(L"%02u:%02u", local.wHour, local.wMinute);
    return Format(L"%02u/%02u %02u:%02u", local.wDay, local.wMonth, local.wHour, local.wMinute);
}

std::wstring FormatDateTime(const SYSTEMTIME& utc) {
    if (utc.wYear == 0) return L"";
    SYSTEMTIME l = ToLocal(utc);
    return Format(L"%02u/%02u/%04u %02u:%02u", l.wDay, l.wMonth, l.wYear, l.wHour, l.wMinute);
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
    // SYSTEMTIME -> FILETIME (intervalos de 100 ns desde 1601). Falla con
    // campos fuera de rango (p. ej. un SYSTEMTIME vacío): se devuelve 0.
    FILETIME ft{};
    if (!SystemTimeToFileTime(&utc, &ft)) return 0;
    // Las dos mitades de 32 bits se unen en un entero de 64 bits.
    ULARGE_INTEGER li{};
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    // Se corre el origen a 1970 y se pasa de 100 ns a segundos (10^7).
    return (static_cast<std::int64_t>(li.QuadPart) - kEpochDelta) / 10000000LL;
}

SYSTEMTIME FromUnixSeconds(std::int64_t seconds) {
    // Camino inverso: segundos -> 100 ns, origen 1601 -> SYSTEMTIME (UTC).
    // Si la conversión falla, st queda en ceros (wYear == 0 = "sin fecha").
    ULARGE_INTEGER li{};
    li.QuadPart = static_cast<ULONGLONG>(seconds * 10000000LL + kEpochDelta);
    FILETIME ft{li.LowPart, li.HighPart};
    SYSTEMTIME st{};
    FileTimeToSystemTime(&ft, &st);
    return st;
}

}  // namespace cola::util
