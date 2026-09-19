#pragma once
// Inicio automático con Windows (clave Run de HKCU, no requiere administrador).

namespace cola::config {

bool IsAutostartEnabled();
// Al iniciar desde el registro se pasa "--tray" para arrancar minimizada.
bool SetAutostart(bool enabled);

}  // namespace cola::config
