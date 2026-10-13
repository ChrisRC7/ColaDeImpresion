#pragma once
// Ventana modal "Acerca de": nombre y versión de la aplicación, para qué se
// hizo y sus autores principales con su correo. Pintada con la paleta de la
// app (claro/oscuro). Un clic en un correo abre el programa de correo; el
// botón de al lado lo copia al portapapeles (en el servidor puede no haber
// programa de correo). Tab / Mayús+Tab recorren, Enter/Espacio activan y Esc
// cierra.

#include <windows.h>

namespace cola::ui {

void ShowAboutDialog(HWND owner);

}  // namespace cola::ui
