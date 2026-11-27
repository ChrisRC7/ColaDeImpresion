#pragma once
// Ventana modal "Acerca de": nombre y versión de la aplicación, para qué se
// hizo y sus autores principales, con un botón para buscar actualizaciones
// (D58). Pintada con la paleta de la app (claro/oscuro). Esc, Enter o Cerrar
// la cierran; B equivale a Buscar actualizaciones.

#include <windows.h>

namespace cola::ui {

// Muestra el diálogo centrado sobre owner (que queda deshabilitado mientras
// tanto) y vuelve cuando el usuario lo cierra. Se abre desde el menú
// principal ("Acerca de...") y la cabecera. Devuelve true si se cerró con
// Buscar actualizaciones: quien lo abrió debe iniciar la búsqueda.
bool ShowAboutDialog(HWND owner);

}  // namespace cola::ui
