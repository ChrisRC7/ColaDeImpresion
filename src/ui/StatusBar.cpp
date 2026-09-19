#include "ui/StatusBar.h"

#include "ui/Gfx.h"
#include "ui/Style.h"

namespace cola::ui {

bool StatusBar::Create(HWND parent) {
    return CreateWin(L"ColaStatusBar", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, 0, parent);
}

int StatusBar::PreferredHeight() const { return Style::Get().Px(30); }

void StatusBar::SetSummary(const std::wstring& text) {
    if (text == summary_) return;
    summary_ = text;
    Invalidate();
}

void StatusBar::SetItems(std::vector<StatusItem> items) {
    items_ = std::move(items);
    Invalidate();
}

void StatusBar::Paint(HDC dc, const RECT& client) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    gfx::Fill(dc, client, pal.surfaceAlt);
    gfx::HLine(dc, client.left, client.right, client.top, pal.border);

    const int pad = style.Px(14);
    int right = client.right - pad;
    // Indicadores de derecha a izquierda.
    for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
        SIZE sz = gfx::MeasureText(dc, it->text, style.fonts().small);
        int w = sz.cx + (it->glyph ? style.Px(20) : 0);
        int left = std::max<int>(client.left + style.Px(200), right - w);
        if (left >= right) break;
        COLORREF color = it->tone == Tone::Neutral ? pal.textMuted : pal.ToneColor(it->tone);
        int textLeft = left;
        if (it->glyph) {
            gfx::Glyph(dc, it->glyph, RECT{left, client.top, left + style.Px(16), client.bottom}, style.fonts().iconSmall, color);
            textLeft += style.Px(20);
        }
        gfx::Text(dc, it->text, RECT{textLeft, client.top, right, client.bottom}, style.fonts().small, color);
        right = left - style.Px(18);
    }
    gfx::Text(dc, summary_, RECT{client.left + pad, client.top, right, client.bottom}, style.fonts().small, pal.textMuted);
}

LRESULT StatusBar::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            gfx::BufferedPaint paint(hwnd_);
            Paint(paint.dc(), paint.rect());
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: Invalidate(); return 0;
    }
    return Window::HandleMessage(msg, wParam, lParam);
}

}  // namespace cola::ui
