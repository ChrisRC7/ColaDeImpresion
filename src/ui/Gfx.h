#pragma once
// Primitivas de dibujo reutilizadas por los controles propios.
// Texto con GDI (ClearType); formas redondeadas con GDI+ (antialias).

#include <windows.h>

#include <string>

namespace cola::ui::gfx {

// Pintado con doble búfer: evita parpadeo. Uso típico en WM_PAINT:
//   gfx::BufferedPaint paint(hwnd);
//   Dibujar(paint.dc(), paint.rect());
class BufferedPaint {
public:
    explicit BufferedPaint(HWND hwnd);
    ~BufferedPaint();
    BufferedPaint(const BufferedPaint&) = delete;
    BufferedPaint& operator=(const BufferedPaint&) = delete;

    HDC dc() const { return memDc_; }
    const RECT& rect() const { return client_; }

private:
    HWND hwnd_;
    PAINTSTRUCT ps_{};
    HDC windowDc_ = nullptr;
    HDC memDc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    RECT client_{};
};

void StartGdiPlus();
void StopGdiPlus();

void Fill(HDC dc, const RECT& rect, COLORREF color);
void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF color);
// Igual, pero translúcido (alpha 0..255): p. ej. el rectángulo de selección.
void FillRoundedAlpha(HDC dc, const RECT& rect, int radius, COLORREF color, int alpha);
void StrokeRounded(HDC dc, const RECT& rect, int radius, COLORREF color, float width = 1.0f);
void FillCircle(HDC dc, int cx, int cy, int radius, COLORREF color);
void HLine(HDC dc, int x1, int x2, int y, COLORREF color);
void VLine(HDC dc, int x, int y1, int y2, COLORREF color);

// Dibuja texto con la fuente y color dados. 'format' son flags DT_*.
void Text(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color,
          UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
SIZE MeasureText(HDC dc, const std::wstring& text, HFONT font);
// Alto de un bloque de texto con ajuste de línea en el ancho dado.
int MeasureWrapped(HDC dc, const std::wstring& text, HFONT font, int width);

// Dibuja un glifo de la fuente de íconos centrado en rect.
void Glyph(HDC dc, wchar_t glyph, const RECT& rect, HFONT iconFont, COLORREF color);

inline int Width(const RECT& r) { return r.right - r.left; }
inline int Height(const RECT& r) { return r.bottom - r.top; }
inline bool Contains(const RECT& r, POINT p) { return PtInRect(&r, p) != FALSE; }
RECT Inset(const RECT& r, int dx, int dy);

}  // namespace cola::ui::gfx
