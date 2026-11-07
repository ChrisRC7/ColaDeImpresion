// Implementación del inicio automático mediante la clave Run de HKCU (ver Autostart.h).

#include "config/Autostart.h"

#include <windows.h>

#include <string>

#include "config/Paths.h"

namespace cola::config {

namespace {
// Clave estándar que Windows recorre al iniciar la sesión del usuario.
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
// Nombre del valor (también es lo que muestra el Administrador de tareas en
// la pestaña "Inicio").
constexpr wchar_t kValueName[] = L"ColaDeImpresion";
}  // namespace

bool IsAutostartEnabled() {
    HKEY key = nullptr;
    // Si la clave no se puede abrir se considera desactivado.
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    // Solo interesa si el valor existe: no se leen tipo ni datos.
    LONG rc = RegQueryValueExW(key, kValueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

bool SetAutostart(bool enabled) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
    LONG rc;
    if (enabled) {
        // Ruta entre comillas porque puede tener espacios ("Program Files").
        // --tray: arrancar en la bandeja sin mostrar la ventana.
        std::wstring command = L"\"" + ExecutablePath() + L"\" --tray";
        // REG_SZ exige incluir el nulo final en el tamaño (en bytes).
        rc = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kValueName);
        // Si ya no existía, el resultado deseado (desactivado) se cumple.
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

}  // namespace cola::config
