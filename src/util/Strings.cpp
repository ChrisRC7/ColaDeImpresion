// Implementación de las utilidades de cadenas (ver Strings.h). Las
// conversiones usan la API de Windows (no <codecvt>, obsoleto en C++17).

#include "util/Strings.h"

#include <windows.h>

#include <cstdarg>
#include <cwctype>

namespace cola::util {

std::string ToUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    // Patrón de dos llamadas: la primera (búfer nullptr) devuelve cuántos
    // bytes hacen falta; la segunda convierte. Se pasa el largo explícito
    // porque un wstring_view no termina necesariamente en nulo.
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                   nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr,
                        nullptr);
    return out;
}

std::wstring FromUtf8(std::string_view text) {
    if (text.empty()) return {};
    // Mismo patrón de dos llamadas que ToUtf8 (medir y luego convertir).
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::wstring ToLower(std::wstring_view text) {
    std::wstring out(text);
    // CharLowerBuffW convierte en el lugar y conoce los caracteres acentuados
    // (towlower de la CRT depende de la configuración regional del proceso).
    if (!out.empty()) CharLowerBuffW(out.data(), static_cast<DWORD>(out.size()));
    return out;
}

std::wstring Trim(std::wstring_view text) {
    // Dos índices que se acercan desde los extremos saltando espacios.
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::iswspace(text[begin])) ++begin;
    while (end > begin && std::iswspace(text[end - 1])) --end;
    return std::wstring(text.substr(begin, end - begin));
}

bool ContainsInsensitive(std::wstring_view haystack, std::wstring_view needle) {
    if (needle.empty()) return true;
    // Se pasan ambos a minúsculas y se busca. Crea dos copias, pero las
    // cadenas son cortas (nombres de documento, usuario, equipo...).
    return ToLower(haystack).find(ToLower(needle)) != std::wstring::npos;
}

bool EqualsInsensitive(std::wstring_view a, std::wstring_view b) {
    // Comparación ordinal (por código de carácter) ignorando mayúsculas: no
    // depende del idioma, que es lo correcto para identificadores del sistema.
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()),
                                TRUE) == CSTR_EQUAL;
}

std::vector<std::wstring> Split(std::wstring_view text, wchar_t separator) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    // `<=` (y no `<`) para emitir también la parte vacía final ("a," da
    // {"a", ""}) y para que un texto vacío dé un elemento vacío.
    while (start <= text.size()) {
        size_t pos = text.find(separator, start);
        if (pos == std::wstring_view::npos) pos = text.size();
        parts.emplace_back(text.substr(start, pos - start));
        // Tras la última parte start queda en size() + 1 y el bucle termina.
        start = pos + 1;
    }
    return parts;
}

std::wstring Format(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    // Una va_list solo se puede recorrer una vez: se copia para medir el largo
    // con _vscwprintf y se usa la original para escribir.
    va_list copy;
    va_copy(copy, args);
    int size = _vscwprintf(fmt, copy);
    va_end(copy);
    std::wstring out;
    // size < 0 = formato inválido; 0 = cadena vacía. En ambos casos queda "".
    if (size > 0) {
        // +1 para el nulo que escribe _vsnwprintf_s; luego se recorta.
        out.resize(static_cast<size_t>(size) + 1);
        _vsnwprintf_s(out.data(), out.size(), _TRUNCATE, fmt, args);
        out.resize(static_cast<size_t>(size));
    }
    va_end(args);
    return out;
}

std::wstring Win32ErrorMessage(unsigned long error) {
    // FORMAT_MESSAGE_ALLOCATE_BUFFER: Windows reserva el búfer (se libera con
    // LocalFree); por eso se pasa la dirección del puntero disfrazada de
    // wchar_t*. IGNORE_INSERTS evita que "%1" en el mensaje lea argumentos
    // inexistentes. Idioma 0 = el del sistema/usuario.
    wchar_t* buffer = nullptr;
    DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message;
    if (len && buffer) {
        message.assign(buffer, len);
        LocalFree(buffer);
    }
    // Los mensajes del sistema terminan en "\r\n": se quitan.
    message = Trim(message);
    if (message.empty()) message = Format(L"Error %lu", error);
    return message;
}

std::wstring EllipsisMiddle(const std::wstring& text, size_t maxChars) {
    // Por debajo de 8 caracteres el recorte dejaría algo ilegible.
    if (maxChars < 8 || text.size() <= maxChars) return text;
    // Un tercio para el principio (unidad/servidor), 1 para "…" y el resto
    // para el final (nombre de la carpeta o archivo).
    const size_t head = maxChars / 3;
    const size_t tail = maxChars - head - 1;
    return text.substr(0, head) + L"…" + text.substr(text.size() - tail);
}

}  // namespace cola::util
