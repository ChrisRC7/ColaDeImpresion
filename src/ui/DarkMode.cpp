// Modo oscuro de lo que pinta Windows (barra de título, menús, barras de
// desplazamiento y el EDIT de búsqueda). La parte no documentada se resuelve
// en tiempo de ejecución por ordinal: si una versión de Windows no la tiene,
// los punteros quedan en nullptr y simplemente se omite (D9).

#include "ui/DarkMode.h"

#include <dwmapi.h>
#include <uxtheme.h>

namespace cola::ui {

namespace {
// Valores que acepta SetPreferredAppMode (uxtheme, ordinal 135). Se usa
// ForceDark/ForceLight según la paleta de la app (no "seguir al sistema"),
// porque el operador puede elegir un tema distinto al de Windows.
enum PreferredAppMode { Default, AllowDark, ForceDark, ForceLight };
// Firmas de las funciones no documentadas (tal como las usan Notepad++ y otros).
using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using AllowDarkModeForWindowFn = bool(WINAPI*)(HWND, bool);
using FlushMenuThemesFn = void(WINAPI*)();

// Punteros resueltos por InitDarkModeSupport (nullptr si no existen).
SetPreferredAppModeFn g_setPreferredAppMode = nullptr;
AllowDarkModeForWindowFn g_allowDarkModeForWindow = nullptr;
FlushMenuThemesFn g_flushMenuThemes = nullptr;

// Obtiene una función exportada solo por número (sin nombre). El paso por
// void* evita el aviso de conversión entre tipos de puntero a función.
template <typename T>
T LoadOrdinal(HMODULE module, WORD ordinal) {
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module, MAKEINTRESOURCEA(ordinal))));
}
}  // namespace

void InitDarkModeSupport() {
    // Solo desde System32 (evita cargar una DLL plantada junto al .exe). La
    // biblioteca no se libera: sus funciones se usan durante toda la ejecución.
    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) return;
    // Ordinales estables desde Windows 10 1903.
    g_allowDarkModeForWindow = LoadOrdinal<AllowDarkModeForWindowFn>(uxtheme, 133);
    g_setPreferredAppMode = LoadOrdinal<SetPreferredAppModeFn>(uxtheme, 135);
    g_flushMenuThemes = LoadOrdinal<FlushMenuThemesFn>(uxtheme, 136);
}

void ApplyAppDarkMode(bool dark) {
    if (g_setPreferredAppMode) g_setPreferredAppMode(dark ? ForceDark : ForceLight);
    // Sin el vaciado, los menús ya abiertos alguna vez conservarían el tema
    // anterior hasta reiniciar la app.
    if (g_flushMenuThemes) g_flushMenuThemes();
}

void ApplyWindowDarkMode(HWND hwnd, bool dark) {
    BOOL value = dark ? TRUE : FALSE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 20H1+); 19 en builds anteriores.
    // Se usan los números y no la constante porque los encabezados de MinGW
    // pueden no definirla; en Windows sin soporte ambas llamadas fallan sin efecto.
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &value, sizeof(value))))
        DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value));
    // Permite que las barras de desplazamiento y otras partes temáticas de
    // esta ventana usen la variante oscura.
    if (g_allowDarkModeForWindow) g_allowDarkModeForWindow(hwnd, dark);
    // "DarkMode_Explorer" es la clase de tema oscura que usa el Explorador.
    SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

void ApplyControlDarkMode(HWND control, bool dark) {
    if (g_allowDarkModeForWindow) g_allowDarkModeForWindow(control, dark);
    SetWindowTheme(control, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    // El control no se entera solo del cambio: se le pide que recargue el tema.
    SendMessageW(control, WM_THEMECHANGED, 0, 0);
}

}  // namespace cola::ui
