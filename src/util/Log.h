#pragma once
// Registro simple a archivo (%LOCALAPPDATA%\ColaDeImpresion\app.log).
// Útil para diagnosticar en el servidor del laboratorio sin depurador.

#include <string>

namespace cola::util {

void LogInit(const std::wstring& path);
void LogInfo(const wchar_t* fmt, ...);
void LogError(const wchar_t* fmt, ...);

}  // namespace cola::util
