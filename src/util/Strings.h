#pragma once
// Utilidades de cadenas: conversión UTF-8 <-> UTF-16 y helpers comunes.

#include <string>
#include <string_view>
#include <vector>

namespace cola::util {

std::string ToUtf8(std::wstring_view text);
std::wstring FromUtf8(std::string_view text);

std::wstring ToLower(std::wstring_view text);
std::wstring Trim(std::wstring_view text);

// Búsqueda sin distinguir mayúsculas/minúsculas (para el filtro de trabajos).
bool ContainsInsensitive(std::wstring_view haystack, std::wstring_view needle);
bool EqualsInsensitive(std::wstring_view a, std::wstring_view b);

std::vector<std::wstring> Split(std::wstring_view text, wchar_t separator);

// Formato estilo printf devolviendo std::wstring.
std::wstring Format(const wchar_t* fmt, ...);

// Mensaje legible para un código de error Win32 (en el idioma del sistema).
std::wstring Win32ErrorMessage(unsigned long error);

}  // namespace cola::util
