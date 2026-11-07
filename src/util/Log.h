#pragma once
// Registro simple a archivo (%LOCALAPPDATA%\ColaDeImpresion\app.log).
// Útil para diagnosticar en el servidor del laboratorio sin depurador.
// (La ruta real la decide quien llama a LogInit: app.log va en
// config::DataDir(), que en modo portable es la carpeta del .exe; ver D11/D49.)
//
// Formato de cada línea (UTF-8, CRLF):
//   2026-09-19 14:05:33 [INFO] mensaje
// Al superar 2 MB el archivo se renombra a app.log.old (se pierde el .old
// anterior) y se empieza uno nuevo. Las funciones son seguras entre hilos
// (un mutex global) y no hacen nada mientras no se haya llamado a LogInit.

#include <string>

namespace cola::util {

// Fija la ruta del archivo de log. Se llama una vez al arrancar, antes de
// registrar nada; los mensajes anteriores se descartan en silencio.
void LogInit(const std::wstring& path);
// Registra un mensaje con formato estilo printf (cadenas anchas: %ls, %lu...).
// Mensajes de más de 2047 caracteres se truncan.
void LogInfo(const wchar_t* fmt, ...);
// Igual que LogInfo pero con nivel [ERROR].
void LogError(const wchar_t* fmt, ...);

}  // namespace cola::util
