#pragma once
// Integración con el modo oscuro de Windows 10/11:
//  - barra de título oscura (DWM, API documentada)
//  - menús contextuales y barras de desplazamiento oscuras (API no documentada
//    de uxtheme usada por Notepad++, Explorer, etc.; se carga dinámicamente y
//    si no existe simplemente no hace nada).

#include <windows.h>

namespace cola::ui {

void InitDarkModeSupport();
void ApplyAppDarkMode(bool dark);          // menús emergentes
void ApplyWindowDarkMode(HWND hwnd, bool dark);  // barra de título + scrollbars
void ApplyControlDarkMode(HWND control, bool dark);  // EDIT, scrollbars nativas

}  // namespace cola::ui
