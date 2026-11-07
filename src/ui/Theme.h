#pragma once
// Paletas de color (modo claro / oscuro) y detección del tema del sistema.
// Todos los colores de la UI salen de aquí: para cambiar el aspecto basta con
// editar las paletas en Theme.cpp (las pruebas verifican el contraste WCAG).
//
// Los controles no leen estas paletas directamente: piden la activa a
// ui::Style (Style::Get().palette()), que decide entre claro y oscuro según
// la configuración (D8, D9). Este archivo no depende de ventanas: solo define
// datos y funciones puras de color, por eso las pruebas unitarias lo usan.

#include <windows.h>

#include "config/Settings.h"
#include "core/PrintTypes.h"

namespace cola::ui {

// Tono semántico de una fila / insignia de estado.
// Neutral = en cola u otros sin significado especial; Info = imprimiéndose o
// recibiéndose; Success = impreso; Warning = en pausa/retenido; Danger =
// error; Special = impresora sin conexión. El color concreto de cada tono lo
// da la paleta (Palette::ToneColor), así el mismo tono funciona en ambos modos.
enum class Tone { Neutral, Info, Success, Warning, Danger, Special };

// Juego completo de colores de la interfaz para un modo (claro u oscuro).
// Los campos se inicializan por posición en Theme.cpp: si se agrega uno hay
// que agregarlo en el mismo lugar en kLight y kDark.
struct Palette {
    COLORREF window;        // fondo general
    COLORREF surface;       // lista, paneles
    COLORREF surfaceAlt;    // cabecera, barra de estado, controles
    COLORREF border;        // líneas divisorias y bordes de controles
    COLORREF text;          // texto principal
    COLORREF textMuted;     // texto secundario (subtítulos, ayudas, títulos de columna)
    COLORREF accent;        // color de acento: foco, botones primarios, enlaces
    COLORREF accentText;    // texto sobre accent
    COLORREF hover;         // fondo de un elemento bajo el mouse
    COLORREF selection;     // fondo de las filas seleccionadas
    COLORREF selectionText; // texto sobre selection
    COLORREF previewBackdrop; // fondo del área de la vista previa (detrás de la página)
    COLORREF pageShadow;    // sombra que se dibuja bajo la página de la vista previa
    COLORREF overlayScrim;  // fondo del panel de ayuda
    // Tonos de estado (texto/ícono) — deben contrastar ≥ 4.5 con surface.
    COLORREF neutral;       // Tone::Neutral
    COLORREF info;          // Tone::Info
    COLORREF success;       // Tone::Success
    COLORREF warning;       // Tone::Warning
    COLORREF danger;        // Tone::Danger
    COLORREF special;       // Tone::Special
    bool dark;              // true en la paleta oscura (ajusta mezclas y la barra de título DWM)

    // Color de texto/ícono del tono dado (neutral para valores desconocidos).
    COLORREF ToneColor(Tone tone) const;
    // Fondo suave para la insignia (mezcla del tono con surface).
    COLORREF ToneBackground(Tone tone) const;
};

// Paletas fijas de la aplicación (objetos estáticos, válidos toda la ejecución).
const Palette& LightPalette();
const Palette& DarkPalette();

// Lee HKCU\...\Themes\Personalize\AppsUseLightTheme.
// Devuelve true si el usuario eligió el modo oscuro para las aplicaciones;
// false si eligió el claro o si el valor no existe (Windows antiguos).
bool SystemPrefersDark();

// Resuelve el modo configurado (System/Light/Dark) a claro u oscuro.
// Devuelve true para oscuro. "System" consulta SystemPrefersDark().
bool ResolveDark(config::ThemeMode mode);

// Tono con el que se muestra un estado de trabajo (color del texto/insignia
// de la columna Estado).
Tone ToneForState(core::JobState state);

// Utilidades de color.
// Interpolación lineal por canal entre a y b; t se limita a [0, 1].
COLORREF Blend(COLORREF a, COLORREF b, double t);  // t=0 -> a, t=1 -> b
// Luminancia relativa WCAG 2.x (0 = negro, 1 = blanco) con corrección gamma sRGB.
double RelativeLuminance(COLORREF c);
// Relación de contraste (1 a 21) entre dos colores, sin importar el orden.
double ContrastRatio(COLORREF a, COLORREF b);  // WCAG 2.x

}  // namespace cola::ui
