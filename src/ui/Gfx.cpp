// Primitivas de dibujo: doble búfer, rellenos y texto con GDI, formas
// redondeadas y círculos con GDI+ (antialias). GDI+ solo se usa donde hace
// falta suavizado: su texto no tiene ClearType y es más lento.

#include "ui/Gfx.h"

// <objidl.h> declara IStream y otros tipos COM que <gdiplus.h> necesita y que
// WIN32_LEAN_AND_MEAN deja fuera de <windows.h>.
#include <objidl.h>
// GDI+ usa min/max sin prefijo; con NOMINMAX hay que proveerlos.
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

namespace cola::ui::gfx {

namespace {
// Token de GdiplusStartup; 0 = GDI+ no iniciado.
ULONG_PTR g_gdiplusToken = 0;

// COLORREF (0x00BBGGRR) a color GDI+ opaco (ARGB).
Gdiplus::Color ToColor(COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); }

// Agrega a `path` el contorno de r con esquinas de radio `radius`, armado con
// cuatro arcos de 90° (sentido horario desde la esquina superior izquierda);
// CloseFigure une los arcos con los lados rectos.
void AddRoundedRect(Gdiplus::GraphicsPath& path, const RECT& r, int radius) {
    const float x = static_cast<float>(r.left);
    const float y = static_cast<float>(r.top);
    const float w = static_cast<float>(r.right - r.left);
    const float h = static_cast<float>(r.bottom - r.top);
    // d = diámetro del arco. Se limita al lado menor: con un rectángulo más
    // chico que el radio los arcos se cruzarían y el relleno saldría deformado
    // (así un radio grande da una "píldora").
    float d = static_cast<float>(radius * 2);
    d = std::min({d, w, h});
    // Radio 0 (o rectángulo vacío): rectángulo recto.
    if (d <= 0) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    // Ángulos de GDI+ en grados desde el eje X positivo, en sentido horario.
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}
}  // namespace

BufferedPaint::BufferedPaint(HWND hwnd) : hwnd_(hwnd) {
    // BeginPaint valida la región inválida (si no se llamara, Windows seguiría
    // enviando WM_PAINT sin parar).
    windowDc_ = BeginPaint(hwnd, &ps_);
    // Se pinta el área cliente entera, no solo ps_.rcPaint: es más simple y el
    // coste es bajo para controles de este tamaño.
    GetClientRect(hwnd, &client_);
    memDc_ = CreateCompatibleDC(windowDc_);
    // Mínimo 1×1: CreateCompatibleBitmap con tamaño 0 falla (ventana minimizada
    // o colapsada) y el resto del código no tendría dónde dibujar.
    bitmap_ = CreateCompatibleBitmap(windowDc_, std::max(1, Width(client_)), std::max(1, Height(client_)));
    oldBitmap_ = SelectObject(memDc_, bitmap_);
    // El texto se dibuja sin rellenar su fondo: los controles pintan el fondo antes.
    SetBkMode(memDc_, TRANSPARENT);
}

BufferedPaint::~BufferedPaint() {
    // Copia el búfer terminado a la ventana de una sola vez (sin parpadeo).
    BitBlt(windowDc_, 0, 0, Width(client_), Height(client_), memDc_, 0, 0, SRCCOPY);
    // Limpieza en orden inverso: devolver el mapa de bits original antes de
    // borrar el propio (GDI no borra objetos seleccionados en un DC).
    SelectObject(memDc_, oldBitmap_);
    DeleteObject(bitmap_);
    DeleteDC(memDc_);
    EndPaint(hwnd_, &ps_);
}

void StartGdiPlus() {
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
}

void StopGdiPlus() {
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    g_gdiplusToken = 0;
}

void Fill(HDC dc, const RECT& rect, COLORREF color) {
    // Truco clásico de GDI: ExtTextOut con ETO_OPAQUE y sin texto rellena el
    // rectángulo con el color de fondo. Evita crear y borrar un HBRUSH por
    // cada relleno (se llama muchas veces por cuadro).
    COLORREF old = SetBkColor(dc, color);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rect, nullptr, 0, nullptr);
    SetBkColor(dc, old);
}

void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF color) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    // PixelOffsetModeHalf hace que las coordenadas enteras caigan en el borde
    // del píxel (como en GDI), así el relleno ocupa exactamente el RECT.
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::GraphicsPath path;
    AddRoundedRect(path, rect, radius);
    Gdiplus::SolidBrush brush(ToColor(color));
    g.FillPath(&brush, &path);
}

void FillRoundedAlpha(HDC dc, const RECT& rect, int radius, COLORREF color, int alpha) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::GraphicsPath path;
    AddRoundedRect(path, rect, radius);
    // GDI+ mezcla con lo ya dibujado en el búfer según alpha (GDI no sabe).
    Gdiplus::SolidBrush brush(Gdiplus::Color(static_cast<BYTE>(std::clamp(alpha, 0, 255)), GetRValue(color),
                                             GetGValue(color), GetBValue(color)));
    g.FillPath(&brush, &path);
}

void StrokeRounded(HDC dc, const RECT& rect, int radius, COLORREF color, float width) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    // Sin PixelOffsetModeHalf: el trazo se centra sobre la línea y, al achicar
    // el rectángulo 1 píxel a la derecha y abajo, el contorno queda dentro del
    // RECT (right/bottom son exclusivos).
    Gdiplus::GraphicsPath path;
    RECT r = rect;
    r.right -= 1;
    r.bottom -= 1;
    AddRoundedRect(path, r, radius);
    Gdiplus::Pen pen(ToColor(color), width);
    g.DrawPath(&pen, &path);
}

void FillCircle(HDC dc, int cx, int cy, int radius, COLORREF color) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::SolidBrush brush(ToColor(color));
    g.FillEllipse(&brush, cx - radius, cy - radius, radius * 2, radius * 2);
}

void HLine(HDC dc, int x1, int x2, int y, COLORREF color) {
    RECT r{x1, y, x2, y + 1};
    Fill(dc, r, color);
}

void VLine(HDC dc, int x, int y1, int y2, COLORREF color) {
    RECT r{x, y1, x + 1, y2};
    Fill(dc, r, color);
}

// rect va por valor porque DrawTextW puede modificarlo (p. ej. con DT_CALCRECT).
void Text(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color, UINT format) {
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
    SelectObject(dc, old);
}

SIZE MeasureText(HDC dc, const std::wstring& text, HFONT font) {
    HGDIOBJ old = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, old);
    return size;
}

int MeasureWrapped(HDC dc, const std::wstring& text, HFONT font, int width) {
    HGDIOBJ old = SelectObject(dc, font);
    // Con DT_CALCRECT DrawTextW no dibuja: ajusta r.bottom al alto necesario
    // para el texto partido en líneas dentro de `width`.
    RECT r{0, 0, width, 0};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    return r.bottom - r.top;
}

void Glyph(HDC dc, wchar_t glyph, const RECT& rect, HFONT iconFont, COLORREF color) {
    // El glifo es un único carácter de la fuente de íconos (ver ui::glyph).
    wchar_t text[2] = {glyph, 0};
    Text(dc, text, rect, iconFont, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

RECT Inset(const RECT& r, int dx, int dy) { return {r.left + dx, r.top + dy, r.right - dx, r.bottom - dy}; }

}  // namespace cola::ui::gfx
