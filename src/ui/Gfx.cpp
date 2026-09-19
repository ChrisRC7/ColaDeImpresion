#include "ui/Gfx.h"

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
ULONG_PTR g_gdiplusToken = 0;

Gdiplus::Color ToColor(COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); }

void AddRoundedRect(Gdiplus::GraphicsPath& path, const RECT& r, int radius) {
    const float x = static_cast<float>(r.left);
    const float y = static_cast<float>(r.top);
    const float w = static_cast<float>(r.right - r.left);
    const float h = static_cast<float>(r.bottom - r.top);
    float d = static_cast<float>(radius * 2);
    d = std::min({d, w, h});
    if (d <= 0) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}
}  // namespace

BufferedPaint::BufferedPaint(HWND hwnd) : hwnd_(hwnd) {
    windowDc_ = BeginPaint(hwnd, &ps_);
    GetClientRect(hwnd, &client_);
    memDc_ = CreateCompatibleDC(windowDc_);
    bitmap_ = CreateCompatibleBitmap(windowDc_, std::max(1, Width(client_)), std::max(1, Height(client_)));
    oldBitmap_ = SelectObject(memDc_, bitmap_);
    SetBkMode(memDc_, TRANSPARENT);
}

BufferedPaint::~BufferedPaint() {
    BitBlt(windowDc_, 0, 0, Width(client_), Height(client_), memDc_, 0, 0, SRCCOPY);
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
    COLORREF old = SetBkColor(dc, color);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rect, nullptr, 0, nullptr);
    SetBkColor(dc, old);
}

void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF color) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::GraphicsPath path;
    AddRoundedRect(path, rect, radius);
    Gdiplus::SolidBrush brush(ToColor(color));
    g.FillPath(&brush, &path);
}

void StrokeRounded(HDC dc, const RECT& rect, int radius, COLORREF color, float width) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
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
    RECT r{0, 0, width, 0};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    return r.bottom - r.top;
}

void Glyph(HDC dc, wchar_t glyph, const RECT& rect, HFONT iconFont, COLORREF color) {
    wchar_t text[2] = {glyph, 0};
    Text(dc, text, rect, iconFont, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

RECT Inset(const RECT& r, int dx, int dy) { return {r.left + dx, r.top + dy, r.right - dx, r.bottom - dy}; }

}  // namespace cola::ui::gfx
