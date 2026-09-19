#pragma once
// Paletas de color (modo claro / oscuro) y detección del tema del sistema.
// Todos los colores de la UI salen de aquí: para cambiar el aspecto basta con
// editar las paletas en Theme.cpp (las pruebas verifican el contraste WCAG).

#include <windows.h>

#include "config/Settings.h"
#include "core/PrintTypes.h"

namespace cola::ui {

// Tono semántico de una fila / insignia de estado.
enum class Tone { Neutral, Info, Success, Warning, Danger, Special };

struct Palette {
    COLORREF window;        // fondo general
    COLORREF surface;       // lista, paneles
    COLORREF surfaceAlt;    // cabecera, barra de estado, controles
    COLORREF border;
    COLORREF text;
    COLORREF textMuted;
    COLORREF accent;
    COLORREF accentText;    // texto sobre accent
    COLORREF hover;
    COLORREF selection;
    COLORREF selectionText;
    COLORREF previewBackdrop;
    COLORREF pageShadow;
    COLORREF overlayScrim;  // fondo del panel de ayuda
    // Tonos de estado (texto/ícono) — deben contrastar ≥ 4.5 con surface.
    COLORREF neutral;
    COLORREF info;
    COLORREF success;
    COLORREF warning;
    COLORREF danger;
    COLORREF special;
    bool dark;

    COLORREF ToneColor(Tone tone) const;
    // Fondo suave para la insignia (mezcla del tono con surface).
    COLORREF ToneBackground(Tone tone) const;
};

const Palette& LightPalette();
const Palette& DarkPalette();

// Lee HKCU\...\Themes\Personalize\AppsUseLightTheme.
bool SystemPrefersDark();

// Resuelve el modo configurado (System/Light/Dark) a claro u oscuro.
bool ResolveDark(config::ThemeMode mode);

Tone ToneForState(core::JobState state);

// Utilidades de color.
COLORREF Blend(COLORREF a, COLORREF b, double t);  // t=0 -> a, t=1 -> b
double RelativeLuminance(COLORREF c);
double ContrastRatio(COLORREF a, COLORREF b);  // WCAG 2.x

}  // namespace cola::ui
