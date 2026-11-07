#pragma once
// Ventana modal "Acerca de": nombre y versión de la aplicación, para qué se
// hizo y sus autores principales. Pintada con la paleta de la app
// (claro/oscuro). Esc, Enter o Cerrar la cierran.

#include <windows.h>

namespace cola::ui {

// Muestra el diálogo centrado sobre owner (que queda deshabilitado mientras
// tanto) y vuelve cuando el usuario lo cierra. Se abre desde el menú
// principal ("Acerca de...").
void ShowAboutDialog(HWND owner);

}  // namespace cola::ui
