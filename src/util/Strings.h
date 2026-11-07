#pragma once
// Utilidades de cadenas: conversión UTF-8 <-> UTF-16 y helpers comunes.
//
// Convención del proyecto: dentro de la app todo el texto es UTF-16
// (std::wstring, como la API Win32 "W"); UTF-8 solo aparece en los bordes
// (archivos JSON, log, XML del .xlsx). Funciones sin estado, seguras entre hilos.

#include <string>
#include <string_view>
#include <vector>

namespace cola::util {

// UTF-16 -> UTF-8. Cadena vacía si la entrada está vacía. Los sustitutos
// sueltos (UTF-16 inválido) se reemplazan por U+FFFD.
std::string ToUtf8(std::wstring_view text);
// UTF-8 -> UTF-16. Los bytes inválidos se reemplazan por U+FFFD.
std::wstring FromUtf8(std::string_view text);

// Minúsculas según las reglas de Windows (CharLowerBuffW: respeta tildes y ñ).
std::wstring ToLower(std::wstring_view text);
// Quita los espacios en blanco (iswspace: espacio, tab, CR, LF...) del
// principio y del final.
std::wstring Trim(std::wstring_view text);

// Búsqueda sin distinguir mayúsculas/minúsculas (para el filtro de trabajos).
// Una aguja vacía siempre coincide (filtro vacío = mostrar todo).
bool ContainsInsensitive(std::wstring_view haystack, std::wstring_view needle);
// Igualdad sin distinguir mayúsculas/minúsculas, ordinal (no lingüística):
// adecuada para nombres de impresoras, servidores y usuarios.
bool EqualsInsensitive(std::wstring_view a, std::wstring_view b);

// Parte el texto en cada separador. Conserva las partes vacías: "a,,b" da
// {"a", "", "b"} y "" da {""} (siempre al menos un elemento).
std::vector<std::wstring> Split(std::wstring_view text, wchar_t separator);

// Acorta un texto largo conservando el principio y el final ("C:\Wind...\PRINTERS").
// Se usa para mostrar rutas de spool sin romper el ajuste de línea.
// Con maxChars < 8 o un texto que ya cabe se devuelve sin cambios. El
// resultado mide exactamente maxChars (un tercio al principio, "…" y el resto
// al final, que suele ser la parte más informativa de una ruta).
std::wstring EllipsisMiddle(const std::wstring& text, size_t maxChars);

// Formato estilo printf devolviendo std::wstring.
// Usa la variante de Microsoft (_vscwprintf/_vsnwprintf_s): con cadenas
// anchas %ls es wchar_t* y %hs char*. Calcula primero el largo exacto, así
// que no hay límite de tamaño. Devuelve "" si el formato no produce nada.
std::wstring Format(const wchar_t* fmt, ...);

// Mensaje legible para un código de error Win32 (en el idioma del sistema).
// Sin el salto de línea final que agrega FormatMessage; si Windows no conoce
// el código devuelve "Error N".
std::wstring Win32ErrorMessage(unsigned long error);

}  // namespace cola::util
