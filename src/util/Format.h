#pragma once
// Formato de valores para mostrar en la UI (tamaños, fechas).

#include <windows.h>

#include <cstdint>
#include <string>

namespace cola::util {

// 1536 -> "1.5 KB", 5 * 1024 * 1024 -> "5.0 MB". 0 -> "—".
std::wstring FormatBytes(std::uint64_t bytes);

// Hora local corta: "14:05" si es hoy, "18/09 14:05" si es otro día.
std::wstring FormatTimeShort(const SYSTEMTIME& utc);

// Fecha y hora local "02/10/2026 14:05" (columna "Enviado" de la lista).
std::wstring FormatDateTime(const SYSTEMTIME& utc);

// Fecha y hora ISO local "2026-09-19 14:05:33" (para historial).
std::wstring FormatTimeIso(const SYSTEMTIME& utc);

// Fecha local "2026-09-19" (clave de estadísticas por día).
std::wstring FormatDateKey(const SYSTEMTIME& utc);

// Hora local de una hora UTC (la misma si falla la conversión).
SYSTEMTIME ToLocalTime(const SYSTEMTIME& utc);

// Hora UTC actual.
SYSTEMTIME NowUtc();

// Conversión SYSTEMTIME <-> segundos desde epoch Unix (para persistir).
std::int64_t ToUnixSeconds(const SYSTEMTIME& utc);
SYSTEMTIME FromUnixSeconds(std::int64_t seconds);

}  // namespace cola::util
