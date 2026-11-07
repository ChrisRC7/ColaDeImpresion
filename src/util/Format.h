#pragma once
// Formato de valores para mostrar en la UI (tamaños, fechas).
//
// Convención de horas: el spooler entrega JOB_INFO_2.Submitted en UTC, así que
// todas las funciones reciben un SYSTEMTIME en UTC y lo pasan a la hora local
// del equipo para mostrarlo. Un SYSTEMTIME con wYear == 0 significa "sin
// fecha" y se muestra como cadena vacía (salvo FormatDateKey). Las funciones
// no tienen estado y se pueden llamar desde cualquier hilo.

#include <windows.h>

#include <cstdint>
#include <string>

namespace cola::util {

// 1536 -> "1.5 KB", 5 * 1024 * 1024 -> "5.0 MB". 0 -> "—".
// Unidades binarias (1 KB = 1024 B), hasta TB. Un decimal por debajo de 100
// y ninguno desde 100 ("123 MB"), para que el texto quepa en la columna.
// 0 se trata como "desconocido" (el spooler da Size = 0 mientras no sabe).
std::wstring FormatBytes(std::uint64_t bytes);

// Hora local corta: "14:05" si es hoy, "18/09 14:05" si es otro día.
std::wstring FormatTimeShort(const SYSTEMTIME& utc);

// Fecha y hora local "02/10/2026 14:05" (columna "Enviado" de la lista).
std::wstring FormatDateTime(const SYSTEMTIME& utc);

// Fecha y hora ISO local "2026-09-19 14:05:33" (para historial).
std::wstring FormatTimeIso(const SYSTEMTIME& utc);

// Fecha local "2026-09-19" (clave de estadísticas por día).
// El formato año-mes-día hace que el orden alfabético sea el cronológico.
// A diferencia de las demás no comprueba wYear == 0.
std::wstring FormatDateKey(const SYSTEMTIME& utc);

// Hora local de una hora UTC (la misma si falla la conversión).
// Usa la zona horaria actual del equipo (incluido el horario de verano).
SYSTEMTIME ToLocalTime(const SYSTEMTIME& utc);

// Hora UTC actual.
SYSTEMTIME NowUtc();

// Conversión SYSTEMTIME <-> segundos desde epoch Unix (para persistir).
// Se guarda como entero en historial.json porque es compacto, independiente
// de la zona horaria y fácil de comparar. ToUnixSeconds devuelve 0 si la
// fecha no es válida; FromUnixSeconds trunca a segundos (sin milisegundos).
std::int64_t ToUnixSeconds(const SYSTEMTIME& utc);
SYSTEMTIME FromUnixSeconds(std::int64_t seconds);

}  // namespace cola::util
