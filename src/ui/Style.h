#pragma once
// Estilo compartido por todos los controles: paleta activa, fuentes y escala
// DPI. Es un único objeto global porque la app tiene una sola ventana
// principal; MainWindow lo actualiza al cambiar el tema o el DPI y luego
// repinta todo.
//
// Regla de la UI (docs/GUIA_IA.md): los controles toman los colores SOLO de
// Style::Get().palette() y convierten toda medida con Style::Get().Px(dip), así
// la interfaz se ve igual a 100 %, 150 % o 200 % de escala y en ambos modos.

#include <windows.h>

#include <string>

#include "ui/Theme.h"

namespace cola::ui {

// Glifos de "Segoe Fluent Icons" / "Segoe MDL2 Assets".
// Son puntos de código del área de uso privado de esas fuentes: se dibujan
// como texto con Fonts::icon / iconSmall / iconLarge (no hay imágenes de
// íconos en los recursos). Ambas fuentes comparten los códigos, por eso sirve
// la de respaldo de Windows 10 (MDL2) si falta la de Windows 11 (Fluent).
namespace glyph {
constexpr wchar_t Printer = 0xE749;
constexpr wchar_t Pause = 0xE769;
constexpr wchar_t Play = 0xE768;
constexpr wchar_t Cancel = 0xE711;
constexpr wchar_t Search = 0xE721;
constexpr wchar_t More = 0xE712;
constexpr wchar_t Sun = 0xE706;
constexpr wchar_t Moon = 0xE708;
constexpr wchar_t ChevronDown = 0xE70D;
constexpr wchar_t ChevronLeft = 0xE76B;
constexpr wchar_t ChevronRight = 0xE76C;
constexpr wchar_t Warning = 0xE7BA;
constexpr wchar_t Error = 0xEA39;
constexpr wchar_t Completed = 0xE930;
constexpr wchar_t Clock = 0xE823;
constexpr wchar_t History = 0xE81C;
constexpr wchar_t Help = 0xE897;
constexpr wchar_t Document = 0xE8A5;
constexpr wchar_t Refresh = 0xE72C;
constexpr wchar_t Server = 0xE968;
constexpr wchar_t Keyboard = 0xE765;
constexpr wchar_t Live = 0xE9D9;
constexpr wchar_t Shield = 0xEA18;
constexpr wchar_t ZoomIn = 0xE8A3;
constexpr wchar_t ZoomOut = 0xE71F;
constexpr wchar_t OpenPane = 0xE8A0;
constexpr wchar_t ClosePane = 0xE89F;
constexpr wchar_t Export = 0xEDE1;
constexpr wchar_t About = 0xE783;  // signo de exclamación en un círculo
}  // namespace glyph

// Fuentes GDI de la interfaz, ya escaladas al DPI actual. Son propiedad de
// Style (se recrean al cambiar el DPI): los controles las seleccionan en su DC
// pero nunca las borran ni guardan el HFONT entre mensajes.
struct Fonts {
    HFONT normal = nullptr;     // texto general (13,5 DIP)
    HFONT small = nullptr;      // texto secundario: títulos de columna, barra de estado (12 DIP)
    HFONT semibold = nullptr;   // texto resaltado del tamaño normal (13,5 DIP, seminegrita)
    HFONT title = nullptr;      // títulos de paneles y diálogos (16 DIP, seminegrita)
    HFONT icon = nullptr;       // glifos de íconos en botones (15 DIP)
    HFONT iconSmall = nullptr;  // glifos pequeños: chips, flechas de orden (12 DIP)
    HFONT iconLarge = nullptr;  // glifos grandes: estados vacíos de la vista previa (40 DIP)
};

// Estilo global (singleton): paleta activa, fuentes, DPI y caché de pinceles.
// Solo se usa desde el hilo de la interfaz, así que no necesita sincronización.
class Style {
public:
    // Instancia única; se construye la primera vez que se pide.
    static Style& Get();

    // Cambia la escala (dpi = 96 es 100 %; 0 se trata como 96) y recrea las
    // fuentes. Lo llama MainWindow al crearse y en WM_DPICHANGED.
    void SetDpi(UINT dpi);
    // Elige la paleta oscura (true) o clara (false) y vacía la caché de pinceles.
    void SetDark(bool dark);

    // DPI actual de la ventana principal.
    UINT dpi() const { return dpi_; }
    // true si la paleta activa es la oscura.
    bool dark() const { return palette_->dark; }
    // Paleta activa; única fuente de colores de los controles.
    const Palette& palette() const { return *palette_; }
    // Fuentes escaladas al DPI actual.
    const Fonts& fonts() const { return fonts_; }

    // Convierte píxeles independientes del dispositivo (96 DPI) a píxeles reales.
    // Se multiplica por 100 antes de MulDiv para admitir medias medidas (13,5
    // DIP) sin perder la fracción; MulDiv redondea al entero más cercano.
    int Px(double dip) const { return MulDiv(static_cast<int>(dip * 100), static_cast<int>(dpi_), 9600); }

    // Pinceles cacheados por color (se liberan al cambiar de tema).
    // El pincel devuelto es de Style: no se debe borrar ni guardar; usarlo de
    // inmediato (p. ej. en FillRect), porque si la caché se llena se vacía entera.
    HBRUSH Brush(COLORREF color);

    // Libera fuentes y pinceles al terminar el programa.
    ~Style();

private:
    // Elige las familias de fuentes disponibles y crea las fuentes a 96 DPI con
    // la paleta clara; MainWindow aplica después el DPI y el tema reales.
    Style();
    // Recrea todas las fuentes con el DPI actual.
    void RebuildFonts();
    // Borra las fuentes y deja los punteros en nullptr.
    void ReleaseFonts();
    // Borra todos los pinceles cacheados.
    void ReleaseBrushes();

    UINT dpi_ = 96;               // escala actual (96 = 100 %)
    const Palette* palette_;      // paleta activa (apunta a kLight o kDark)
    Fonts fonts_;                 // fuentes creadas para dpi_
    std::wstring textFace_;       // familia del texto ("Segoe UI Variable Text" o "Segoe UI")
    std::wstring iconFace_;       // familia de íconos ("Segoe Fluent Icons" o "Segoe MDL2 Assets")
    // Entrada de la caché de pinceles: color y su HBRUSH sólido.
    struct BrushEntry {
        COLORREF color;
        HBRUSH brush;
    };
    // Caché pequeña de búsqueda lineal: la UI usa pocos colores distintos
    // (los de la paleta y algunas mezclas), 32 alcanzan de sobra.
    BrushEntry brushes_[32] = {};
    int brushCount_ = 0;          // entradas usadas de brushes_
};

}  // namespace cola::ui
