#include "ui/Style.h"

namespace cola::ui {

namespace {

int CALLBACK FontExistsProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}

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

HFONT MakeFont(const std::wstring& face, int pixelHeight, int weight) {
    LOGFONTW lf{};
    lf.lfHeight = -pixelHeight;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, face.c_str(), _TRUNCATE);
    return CreateFontIndirectW(&lf);
}

}  // namespace

Style& Style::Get() {
    static Style instance;
    return instance;
}

Style::Style() : palette_(&LightPalette()) {
    textFace_ = FontExists(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    iconFace_ = FontExists(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    RebuildFonts();
}

Style::~Style() {
    ReleaseFonts();
    ReleaseBrushes();
}

void Style::SetDpi(UINT dpi) {
    if (dpi == 0) dpi = 96;
    if (dpi == dpi_) return;
    dpi_ = dpi;
    RebuildFonts();
}

void Style::SetDark(bool dark) {
    palette_ = dark ? &DarkPalette() : &LightPalette();
    ReleaseBrushes();
}

HBRUSH Style::Brush(COLORREF color) {
    for (int i = 0; i < brushCount_; ++i)
        if (brushes_[i].color == color) return brushes_[i].brush;
    if (brushCount_ == static_cast<int>(std::size(brushes_))) ReleaseBrushes();
    HBRUSH brush = CreateSolidBrush(color);
    brushes_[brushCount_++] = {color, brush};
    return brush;
}

void Style::RebuildFonts() {
    ReleaseFonts();
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
