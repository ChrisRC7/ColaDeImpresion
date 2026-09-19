#pragma once
// Rutas de la aplicación.
//
// Por defecto los datos van en %APPDATA%\ColaDeImpresion (por usuario/operador).
// Modo portable: si junto al .exe existe un archivo "portable.txt", los datos se
// guardan en la carpeta del ejecutable (útil para desplegar en el servidor del
// laboratorio desde una carpeta compartida o USB).

#include <string>

namespace cola::config {

std::wstring ExecutablePath();
std::wstring ExecutableDir();
std::wstring DataDir();  // la crea si no existe
std::wstring SettingsFile();
std::wstring HistoryFile();
std::wstring LogFile();
bool IsPortableMode();

bool EnsureDirectory(const std::wstring& path);

}  // namespace cola::config
