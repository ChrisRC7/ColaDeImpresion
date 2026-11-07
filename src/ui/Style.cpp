// Implementación del estilo global: detección de las familias de fuentes
// disponibles, creación de las fuentes escaladas al DPI y caché de pinceles.

#include "ui/Style.h"

namespace cola::ui {

namespace {

// Callback de EnumFontFamiliesExW: con la primera coincidencia marca la
// bandera (LPARAM apunta a un bool) y devuelve 0 para cortar la enumeración.
int CALLBACK FontExistsProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}

// true si la familia `face` está instalada. Se pregunta explícitamente porque
// CreateFontIndirectW nunca falla: con un nombre inexistente sustituye otra
// fuente en silencio (y para los íconos eso mostraría cuadros vacíos).
bool FontExists(const wchar_t* face) {
    HDC dc = GetDC(nullptr);
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    bool found = false;
    EnumFontFamiliesExW(dc, &lf, FontExistsProc, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

// Crea una fuente de la familia `face` con `pixelHeight` píxeles de alto de
// carácter y el grosor `weight` (FW_*). La altura va negativa: en LOGFONT eso
// pide la altura del carácter sin el espaciado interno, que es lo que
// equivale al "tamaño" de diseño. Devuelve el HFONT (lo libera quien llama).
HFONT MakeFont(const std::wstring& face, int pixelHeight, int weight) {
    LOGFONTW lf{};
    lf.lfHeight = -pixelHeight;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    // ClearType explícito: el texto se dibuja en búferes de memoria (doble
    // búfer) y así conserva el suavizado subpíxel.
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, face.c_str(), _TRUNCATE);
    return CreateFontIndirectW(&lf);
}

}  // namespace

Style& Style::Get() {
    // Singleton de Meyers: construcción perezosa y segura en C++11+.
    static Style instance;
    return instance;
}

Style::Style() : palette_(&LightPalette()) {
    // Windows 11 trae "Segoe UI Variable Text" y "Segoe Fluent Icons"; en
    // Windows 10 se cae a "Segoe UI" y "Segoe MDL2 Assets" (mismos glifos).
    textFace_ = FontExists(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    iconFace_ = FontExists(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    RebuildFonts();
}

Style::~Style() {
    ReleaseFonts();
    ReleaseBrushes();
}

void Style::SetDpi(UINT dpi) {
    // GetDpiForWindow puede devolver 0 si la ventana no es válida: se usa 100 %.
    if (dpi == 0) dpi = 96;
    // Sin cambios no se recrean las fuentes (los controles podrían tenerlas
    // seleccionadas en ese momento).
    if (dpi == dpi_) return;
    dpi_ = dpi;
    RebuildFonts();
}

void Style::SetDark(bool dark) {
    palette_ = dark ? &DarkPalette() : &LightPalette();
    // Los pinceles de la paleta anterior ya no se usarán: se liberan para que
    // la caché no se llene con colores viejos.
    ReleaseBrushes();
}

HBRUSH Style::Brush(COLORREF color) {
    // Búsqueda lineal: con ≤ 32 entradas es más rápido que un mapa.
    for (int i = 0; i < brushCount_; ++i)
        if (brushes_[i].color == color) return brushes_[i].brush;
    // Caché llena: se vacía entera (caso raro, muchas mezclas distintas). Por
    // eso quien recibe un pincel no debe guardarlo.
    if (brushCount_ == static_cast<int>(std::size(brushes_))) ReleaseBrushes();
    HBRUSH brush = CreateSolidBrush(color);
    brushes_[brushCount_++] = {color, brush};
    return brush;
}

void Style::RebuildFonts() {
    ReleaseFonts();
    // Tamaños en DIP; Px los convierte al DPI actual.
    fonts_.normal = MakeFont(textFace_, Px(13.5), FW_NORMAL);
    fonts_.small = MakeFont(textFace_, Px(12), FW_NORMAL);
    fonts_.semibold = MakeFont(textFace_, Px(13.5), FW_SEMIBOLD);
    fonts_.title = MakeFont(textFace_, Px(16), FW_SEMIBOLD);
    fonts_.icon = MakeFont(iconFace_, Px(15), FW_NORMAL);
    fonts_.iconSmall = MakeFont(iconFace_, Px(12), FW_NORMAL);
    fonts_.iconLarge = MakeFont(iconFace_, Px(40), FW_NORMAL);
}

void Style::ReleaseFonts() {
    for (HFONT* f : {&fonts_.normal, &fonts_.small, &fonts_.semibold, &fonts_.title, &fonts_.icon, &fonts_.iconSmall,
                     &fonts_.iconLarge}) {
        if (*f) DeleteObject(*f);
        *f = nullptr;
    }
}

void Style::ReleaseBrushes() {
    for (int i = 0; i < brushCount_; ++i) DeleteObject(brushes_[i].brush);
    brushCount_ = 0;
}

}  // namespace cola::ui
