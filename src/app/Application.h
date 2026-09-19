#pragma once
// Punto de arranque de la aplicación: argumentos de línea de comandos,
// instancia única, inicialización de COM / GDI+ / controles comunes, y bucle
// de mensajes.
//
// Argumentos:
//   --tray               inicia oculta en la bandeja (lo usa el inicio con Windows)
//   --server NOMBRE      conecta a ese servidor de impresión ("" = este equipo)
//   --elevated-restart   (interno) espera a que la instancia anterior termine

namespace cola::app {

int RunApplication();

}  // namespace cola::app
