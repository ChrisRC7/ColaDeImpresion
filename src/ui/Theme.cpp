// Valores de las paletas clara y oscura, detección del tema de Windows y
// funciones de color (mezcla y contraste WCAG). Las pruebas de `tests/`
// comprueban que los tonos de estado contrasten al menos 4.5:1 con surface.

#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

namespace cola::ui {

namespace {
// Convierte un color escrito como en CSS (0xRRGGBB) a COLORREF, que en Win32
// guarda los canales al revés (0x00BBGGRR). Así las paletas se leen como hex web.
constexpr COLORREF Hex(unsigned rgb) {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// Paleta clara: grises fríos con acento azul. Los comentarios /*campo*/
// indican a qué miembro de Palette corresponde cada valor (inicialización por
// posición).
const Palette kLight = {
    /*window*/ Hex(0xF4F5F7),
    /*surface*/ Hex(0xFFFFFF),
    /*surfaceAlt*/ Hex(0xF4F5F7),
    /*border*/ Hex(0xE2E4E9),
    /*text*/ Hex(0x1B1D22),
    /*textMuted*/ Hex(0x5B616E),
    /*accent*/ Hex(0x2458D6),
    /*accentText*/ Hex(0xFFFFFF),
    /*hover*/ Hex(0xF1F4FA),
    /*selection*/ Hex(0xDDE7FD),
    /*selectionText*/ Hex(0x0E1E47),
    /*previewBackdrop*/ Hex(0xE8EAEE),
    /*pageShadow*/ Hex(0xC9CDD4),
    /*overlayScrim*/ Hex(0xFFFFFF),
    /*neutral*/ Hex(0x5B616E),
    /*info*/ Hex(0x2458D6),
    /*success*/ Hex(0x157A3A),
    /*warning*/ Hex(0x9A5B00),
    /*danger*/ Hex(0xC62828),
    /*special*/ Hex(0x7036C9),
    /*dark*/ false,
};

// Paleta oscura: fondos casi negros y tonos más claros/saturados para que el
// texto de estado mantenga el contraste sobre surface oscuro. accentText es
// oscuro porque el acento es claro.
const Palette kDark = {
    /*window*/ Hex(0x141518),
    /*surface*/ Hex(0x1C1D21),
    /*surfaceAlt*/ Hex(0x141518),
    /*border*/ Hex(0x2E3138),
    /*text*/ Hex(0xE9EBEF),
    /*textMuted*/ Hex(0xA3A9B4),
    /*accent*/ Hex(0x7AA7FF),
    /*accentText*/ Hex(0x0B1633),
    /*hover*/ Hex(0x25272D),
    /*selection*/ Hex(0x28395E),
    /*selectionText*/ Hex(0xFFFFFF),
    /*previewBackdrop*/ Hex(0x101114),
    /*pageShadow*/ Hex(0x050506),
    /*overlayScrim*/ Hex(0x1C1D21),
    /*neutral*/ Hex(0xA3A9B4),
    /*info*/ Hex(0x7AA7FF),
    /*success*/ Hex(0x5BD48A),
    /*warning*/ Hex(0xF2B84B),
    /*danger*/ Hex(0xFF7B7B),
    /*special*/ Hex(0xC3A6FF),
    /*dark*/ true,
};
}  // namespace

COLORREF Palette::ToneColor(Tone tone) const {
    switch (tone) {
        case Tone::Info: return info;
        case Tone::Success: return success;
        case Tone::Warning: return warning;
        case Tone::Danger: return danger;
        case Tone::Special: return special;
        default: return neutral;
    }
}

// En oscuro se mezcla más (18 % frente a 10 %) porque sobre un fondo oscuro
// un tinte leve casi no se percibe.
COLORREF Palette::ToneBackground(Tone tone) const { return Blend(surface, ToneColor(tone), dark ? 0.18 : 0.10); }

const Palette& LightPalette() { return kLight; }
const Palette& DarkPalette() { return kDark; }

bool SystemPrefersDark() {
    // Valor predeterminado 1 (= claro) por si la lectura falla a medias.
    DWORD value = 1;
    DWORD size = sizeof(value);
    // RRF_RT_REG_DWORD rechaza el valor si no es un DWORD; si la clave no
    // existe (Windows 7/8 o perfiles sin esa preferencia) se asume claro.
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return false;
    // AppsUseLightTheme = 0 significa que las aplicaciones usan el modo oscuro.
    return value == 0;
}

bool ResolveDark(config::ThemeMode mode) {
    switch (mode) {
        case config::ThemeMode::Dark: return true;
        case config::ThemeMode::Light: return false;
        default: return SystemPrefersDark();
    }
}

Tone ToneForState(core::JobState state) {
    using core::JobState;
    switch (state) {
        case JobState::Printing:
        case JobState::Spooling: return Tone::Info;
        case JobState::Printed: return Tone::Success;
        case JobState::Paused: return Tone::Warning;
        case JobState::Error: return Tone::Danger;
        case JobState::Offline: return Tone::Special;
        default: return Tone::Neutral;
    }
}

COLORREF Blend(COLORREF a, COLORREF b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    // Interpolación en el espacio sRGB (sin linealizar): suficiente para
    // tintes y estados hover; se redondea para no oscurecer por truncamiento.
    auto mix = [t](int x, int y) { return static_cast<int>(std::lround(x + (y - x) * t)); };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

double RelativeLuminance(COLORREF c) {
    // Conversión de un canal sRGB (0..255) a luz lineal, según la fórmula de
    // WCAG 2.x (umbral 0.03928 y exponente 2.4).
    auto channel = [](int v) {
        double s = v / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    // Pesos de la percepción humana: el verde aporta la mayor parte del brillo.
    return 0.2126 * channel(GetRValue(c)) + 0.7152 * channel(GetGValue(c)) + 0.0722 * channel(GetBValue(c));
}

double ContrastRatio(COLORREF a, COLORREF b) {
    double la = RelativeLuminance(a);
    double lb = RelativeLuminance(b);
    // El más claro va en el numerador para que el resultado sea siempre ≥ 1.
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

}  // namespace cola::ui
