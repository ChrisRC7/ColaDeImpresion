#include "ui/DarkMode.h"

#include <dwmapi.h>
#include <uxtheme.h>

namespace cola::ui {

namespace {
enum PreferredAppMode { Default, AllowDark, ForceDark, ForceLight };
using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using AllowDarkModeForWindowFn = bool(WINAPI*)(HWND, bool);
using FlushMenuThemesFn = void(WINAPI*)();

SetPreferredAppModeFn g_setPreferredAppMode = nullptr;
AllowDarkModeForWindowFn g_allowDarkModeForWindow = nullptr;
FlushMenuThemesFn g_flushMenuThemes = nullptr;

template <typename T>
T LoadOrdinal(HMODULE module, WORD ordinal) {
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module, MAKEINTRESOURCEA(ordinal))));
}
}  // namespace

void InitDarkModeSupport() {
    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) return;
    // Ordinales estables desde Windows 10 1903.
    g_allowDarkModeForWindow = LoadOrdinal<AllowDarkModeForWindowFn>(uxtheme, 133);
    g_setPreferredAppMode = LoadOrdinal<SetPreferredAppModeFn>(uxtheme, 135);
    g_flushMenuThemes = LoadOrdinal<FlushMenuThemesFn>(uxtheme, 136);
}

void ApplyAppDarkMode(bool dark) {
    if (g_setPreferredAppMode) g_setPreferredAppMode(dark ? ForceDark : ForceLight);
    if (g_flushMenuThemes) g_flushMenuThemes();
}

void ApplyWindowDarkMode(HWND hwnd, bool dark) {
    BOOL value = dark ? TRUE : FALSE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 20H1+); 19 en builds anteriores.
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &value, sizeof(value))))
        DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value));
    if (g_allowDarkModeForWindow) g_allowDarkModeForWindow(hwnd, dark);
    SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

void ApplyControlDarkMode(HWND control, bool dark) {
    if (g_allowDarkModeForWindow) g_allowDarkModeForWindow(control, dark);
    SetWindowTheme(control, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    SendMessageW(control, WM_THEMECHANGED, 0, 0);
}

}  // namespace cola::ui
