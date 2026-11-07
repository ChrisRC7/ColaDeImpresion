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
//
// Decisiones: D11 (datos en %APPDATA% o portable) y D49 (config.json por
// usuario). DataDir() y SettingsFile() se calculan una sola vez por proceso
// (static local) y luego devuelven siempre lo mismo: crear portable.txt con
// la app abierta no tiene efecto hasta reiniciarla. Las rutas no llevan barra
// final.

#include <string>

namespace cola::config {

// Ruta completa del .exe en ejecución (sin límite de MAX_PATH).
std::wstring ExecutablePath();
// Carpeta del .exe ("." si la ruta no tuviera barras, caso teórico).
std::wstring ExecutableDir();
std::wstring DataDir();      // la crea si no existe
                             // historial y log: %APPDATA%\ColaDeImpresion o, en
                             // modo portable, la carpeta del .exe
std::wstring SettingsDir();  // carpeta de config.json; la crea si no existe
// Ruta de config.json: la de --config, o %LOCALAPPDATA%\ColaDeImpresion\config.json.
// La primera vez copia la configuración anterior (ver Paths.cpp).
std::wstring SettingsFile();
// DataDir()\historial.json.
std::wstring HistoryFile();
// DataDir()\app.log.
std::wstring LogFile();
// true si existe portable.txt junto al .exe (se consulta cada vez).
bool IsPortableMode();

// Solo para desarrollo (--config RUTA): usar otro config.json. Llamar antes del
// primer SettingsFile().
// (Después de esa primera llamada el valor ya quedó fijado y se ignora.)
void SetSettingsFileOverride(const std::wstring& path);

// Crea la carpeta y todas las intermedias que falten. true si al terminar
// existe (también si ya existía).
bool EnsureDirectory(const std::wstring& path);

}  // namespace cola::config
