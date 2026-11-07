// Implementación del registro a archivo (ver Log.h).
// Diseño: el archivo se abre, se escribe y se cierra en cada línea. Es más
// lento que dejarlo abierto, pero el volumen es bajo y así el log se puede
// leer, copiar o borrar mientras la app corre, y nunca queda una línea a
// medias en un búfer si el proceso termina de golpe.

#include "util/Log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

#include "util/Strings.h"

namespace cola::util {

namespace {
// Serializa las escrituras de todos los hilos (monitor, vista previa,
// acciones, UI) para que las líneas no se mezclen ni la rotación compita.
std::mutex g_mutex;
// Ruta del log; vacía = registro desactivado (antes de LogInit).
std::wstring g_path;
constexpr LONGLONG kMaxLogBytes = 2 * 1024 * 1024;  // rota al superar 2 MB

// Escribe una línea con el nivel dado. `args` ya viene iniciado por el
// llamador (va_start) y lo cierra él (va_end).
void Write(const wchar_t* level, const wchar_t* fmt, va_list args) {
    std::lock_guard lock(g_mutex);
    if (g_path.empty()) return;

    // Búfer fijo: _TRUNCATE corta los mensajes muy largos en vez de fallar.
    wchar_t message[2048];
    _vsnwprintf_s(message, _countof(message), _TRUNCATE, fmt, args);

    // Marca de tiempo en hora local (es lo que el operador compara con el reloj).
    SYSTEMTIME t{};
    GetLocalTime(&t);
    std::wstring line = Format(L"%04u-%02u-%02u %02u:%02u:%02u [%ls] %ls\r\n", t.wYear, t.wMonth, t.wDay, t.wHour,
                               t.wMinute, t.wSecond, level, message);
    // Se guarda en UTF-8 (sin BOM) para que se lea bien en cualquier editor.
    std::string utf8 = ToUtf8(line);

    // Rotación: si el archivo ya pasa del límite se renombra a .old
    // (reemplazando el anterior) y la línea va a un archivo nuevo. Si el
    // renombrado falla (otro proceso lo tiene abierto sin compartir borrado)
    // se sigue escribiendo en el mismo.
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(g_path.c_str(), GetFileExInfoStandard, &info)) {
        LONGLONG size = (static_cast<LONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        if (size > kMaxLogBytes) MoveFileExW(g_path.c_str(), (g_path + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    // FILE_APPEND_DATA: cada escritura va al final aunque otra copia de la app
    // (otro usuario en modo portable) escriba en el mismo archivo. Se comparte
    // lectura y escritura para no bloquear a nadie. OPEN_ALWAYS lo crea si no existe.
    HANDLE file = CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    // Si no se puede abrir (carpeta de solo lectura, etc.) se pierde la línea:
    // el log nunca debe hacer fallar a la app.
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
#ifndef NDEBUG
    // En compilaciones de depuración también se ve en el depurador/DebugView.
    OutputDebugStringW(line.c_str());
#endif
}
}  // namespace

void LogInit(const std::wstring& path) {
    std::lock_guard lock(g_mutex);
    g_path = path;
}

// Las dos funciones públicas solo arman la va_list y delegan en Write.
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
