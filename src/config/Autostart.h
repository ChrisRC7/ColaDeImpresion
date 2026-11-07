#pragma once
// Inicio automático con Windows (clave Run de HKCU, no requiere administrador).
//
// Se guarda un valor "ColaDeImpresion" en
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run con la ruta del .exe.
// Es por usuario de Windows: cada operador decide si la app arranca con su
// sesión. El estado real vive en el registro (no en config.json), así que la
// opción del menú siempre refleja lo que de verdad pasará al iniciar sesión.

namespace cola::config {

// true si existe el valor en la clave Run (no comprueba que apunte a este .exe).
bool IsAutostartEnabled();
// Al iniciar desde el registro se pasa "--tray" para arrancar minimizada.
// enabled = true escribe (o reescribe) el valor con la ruta actual del .exe,
// lo que también corrige una ruta vieja si la app se movió de carpeta;
// enabled = false lo borra (borrar uno inexistente cuenta como éxito).
// Devuelve false si no se pudo abrir o modificar la clave.
bool SetAutostart(bool enabled);

}  // namespace cola::config
