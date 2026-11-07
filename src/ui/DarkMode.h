#pragma once
// Integración con el modo oscuro de Windows 10/11:
//  - barra de título oscura (DWM, API documentada)
//  - menús contextuales y barras de desplazamiento oscuras (API no documentada
//    de uxtheme usada por Notepad++, Explorer, etc.; se carga dinámicamente y
//    si no existe simplemente no hace nada).
// Ver la decisión D9 de docs/CONTEXTO.md. Los controles propios no necesitan
// nada de esto (se pintan con la paleta, D8): solo lo que dibuja Windows.

#include <windows.h>

namespace cola::ui {

// Busca en uxtheme.dll las funciones no documentadas (ordinales 133, 135 y
// 136). Llamar una vez al iniciar, antes de crear ventanas y menús; si no
// están, las demás funciones solo aplican la parte documentada.
void InitDarkModeSupport();
// Cambia el modo de los menús emergentes y contextuales de toda la aplicación
// (dark = true los vuelve oscuros) y refresca los temas de menú en caché.
void ApplyAppDarkMode(bool dark);          // menús emergentes
// Barra de título (DWM) y tema de la ventana de nivel superior `hwnd`.
void ApplyWindowDarkMode(HWND hwnd, bool dark);  // barra de título + scrollbars
// Tema de un control nativo hijo (p. ej. el EDIT de búsqueda) y le avisa con
// WM_THEMECHANGED para que repinte sus partes con el tema nuevo.
void ApplyControlDarkMode(HWND control, bool dark);  // EDIT, scrollbars nativas

}  // namespace cola::ui
