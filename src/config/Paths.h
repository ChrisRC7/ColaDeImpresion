#pragma once
// Rutas de la aplicación.
//
// Las preferencias (config.json) son siempre de cada usuario de Windows: van en
// %LOCALAPPDATA%\ColaDeImpresion, también en modo portable, porque en las PCs
// institucionales varias personas ejecutan la misma copia de la app y se
// sobrescribirían la configuración unas a otras.
//
// El resto de los datos (historial, app.log) van por defecto en
// %APPDATA%\ColaDeImpresion. Modo portable: si junto al .exe existe un archivo
// "portable.txt", esos datos se guardan en la carpeta del ejecutable (útil para
// desplegar en el servidor del laboratorio desde una carpeta compartida o USB).

#include <string>

namespace cola::config {

std::wstring ExecutablePath();
std::wstring ExecutableDir();
std::wstring DataDir();      // la crea si no existe
std::wstring SettingsDir();  // carpeta de config.json; la crea si no existe
std::wstring SettingsFile();
std::wstring HistoryFile();
std::wstring LogFile();
bool IsPortableMode();

// Solo para desarrollo (--config RUTA): usar otro config.json. Llamar antes del
// primer SettingsFile().
void SetSettingsFileOverride(const std::wstring& path);

bool EnsureDirectory(const std::wstring& path);

}  // namespace cola::config
