#pragma once
// Estilo compartido por todos los controles: paleta activa, fuentes y escala
// DPI. Es un único objeto global porque la app tiene una sola ventana
// principal; MainWindow lo actualiza al cambiar el tema o el DPI y luego
// repinta todo.

#include <windows.h>

#include <string>

#include "ui/Theme.h"

namespace cola::ui {

// Glifos de "Segoe Fluent Icons" / "Segoe MDL2 Assets".
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
}  // namespace glyph

struct Fonts {
    HFONT normal = nullptr;
    HFONT small = nullptr;
    HFONT semibold = nullptr;
    HFONT title = nullptr;
    HFONT icon = nullptr;
    HFONT iconSmall = nullptr;
    HFONT iconLarge = nullptr;
};

class Style {
public:
    static Style& Get();

    void SetDpi(UINT dpi);
    void SetDark(bool dark);

    UINT dpi() const { return dpi_; }
    bool dark() const { return palette_->dark; }
    const Palette& palette() const { return *palette_; }
    const Fonts& fonts() const { return fonts_; }

    // Convierte píxeles independientes del dispositivo (96 DPI) a píxeles reales.
    int Px(double dip) const { return MulDiv(static_cast<int>(dip * 100), static_cast<int>(dpi_), 9600); }

    // Pinceles cacheados por color (se liberan al cambiar de tema).
    HBRUSH Brush(COLORREF color);

    ~Style();

private:
    Style();
    void RebuildFonts();
    void ReleaseFonts();
    void ReleaseBrushes();

    UINT dpi_ = 96;
    const Palette* palette_;
    Fonts fonts_;
    std::wstring textFace_;
    std::wstring iconFace_;
    struct BrushEntry {
        COLORREF color;
        HBRUSH brush;
    };
    BrushEntry brushes_[32] = {};
    int brushCount_ = 0;
};

}  // namespace cola::ui
