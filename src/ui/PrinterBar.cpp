#include "ui/PrinterBar.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>

#include "i18n/Strings.h"
#include "ui/Gfx.h"
#include "ui/Style.h"

namespace cola::ui {

using i18n::S;
using i18n::Tr;

namespace {
constexpr int kConfigButton = -2;  // botón "+" al final de la barra
}

bool PrinterBar::Create(HWND parent, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    if (!CreateWin(L"ColaPrinterBar", L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, 0, parent)) return false;
    OnStyleChanged();
    return true;
}

int PrinterBar::PreferredHeight() const { return Style::Get().Px(40); }

void PrinterBar::OnStyleChanged() {
    Layout();
    Invalidate();
}

void PrinterBar::SetChips(std::vector<PrinterChip> chips, const std::wstring& active) {
    chips_ = std::move(chips);
    active_ = active;
    Layout();
    Invalidate();
}

void PrinterBar::Layout() {
    if (!hwnd_) return;
    const auto& style = Style::Get();
    RECT client;
    GetClientRect(hwnd_, &client);
    const int pad = style.Px(12);
    const int gap = style.Px(6);
    const int height = style.Px(28);
    const int top = (client.top + client.bottom - height) / 2;

    HDC dc = GetDC(hwnd_);
    rects_.clear();
    int x = client.left + pad;
    for (const auto& chip : chips_) {
        SIZE size = gfx::MeasureText(dc, chip.label, style.fonts().semibold);
        int width = style.Px(28) + static_cast<int>(size.cx) + style.Px(12);
        if (chip.jobs > 0) width += style.Px(24);
        rects_.push_back({x, top, x + width, top + height});
        x += width + gap;
    }
    ReleaseDC(hwnd_, dc);
    contentWidth_ = x - client.left;

    // El botón "+" queda fijo a la derecha (no se desplaza con el scroll).
    const int btn = style.Px(28);
    configButton_ = {client.right - pad - btn, top, client.right - pad, top + height};

    const int visible = std::max(0, static_cast<int>(configButton_.left - style.Px(8) - (client.left + pad)));
    const int overflow = std::max(0, contentWidth_ - pad - visible);
    scrollX_ = std::clamp(scrollX_, 0, overflow);
    for (auto& r : rects_) OffsetRect(&r, -scrollX_, 0);
}

void PrinterBar::ScrollBy(int delta) {
    int before = scrollX_;
    scrollX_ = std::max(0, scrollX_ + delta);
    Layout();
    if (scrollX_ != before) Invalidate();
}

void PrinterBar::Paint(HDC dc, const RECT& client) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const auto& fonts = style.fonts();
    gfx::Fill(dc, client, pal.surfaceAlt);
    gfx::HLine(dc, client.left, client.right, client.bottom - 1, pal.border);

    // Los chips se recortan antes del botón "+" para que no se encimen.
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, client.left, client.top, configButton_.left - style.Px(4), client.bottom);
    for (size_t i = 0; i < chips_.size() && i < rects_.size(); ++i) {
        const auto& chip = chips_[i];
        const RECT& r = rects_[i];
        if (r.right < client.left || r.left > client.right) continue;
        const bool selected = chip.name == active_;
        const int index = static_cast<int>(i);
        COLORREF background = selected ? pal.selection : (hover_ == index ? pal.hover : pal.surface);
        gfx::FillRounded(dc, r, style.Px(14), background);
        gfx::StrokeRounded(dc, r, style.Px(14), selected ? pal.accent : pal.border);

        // Punto de estado a la izquierda (color del problema más grave).
        COLORREF dot = chip.tone == Tone::Neutral ? pal.textMuted : pal.ToneColor(chip.tone);
        gfx::FillCircle(dc, r.left + style.Px(14), (r.top + r.bottom) / 2, style.Px(4), dot);

        RECT labelRect{r.left + style.Px(24), r.top, r.right - style.Px(8), r.bottom};
        if (chip.jobs > 0) labelRect.right -= style.Px(22);
        gfx::Text(dc, chip.label, labelRect, fonts.semibold, selected ? pal.selectionText : pal.text);

        if (chip.jobs > 0) {
            // Insignia con la cantidad de trabajos (en el tono de "retenido" si los hay).
            const Tone badgeTone = chip.held > 0 ? Tone::Special : Tone::Info;
            RECT badge{r.right - style.Px(26), (r.top + r.bottom) / 2 - style.Px(9), r.right - style.Px(6),
                       (r.top + r.bottom) / 2 + style.Px(9)};
            gfx::FillRounded(dc, badge, style.Px(9), pal.ToneBackground(badgeTone));
            gfx::Text(dc, std::to_wstring(chip.jobs), badge, fonts.small, pal.ToneColor(badgeTone),
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
    RestoreDC(dc, saved);

    // Botón para elegir qué impresoras se fijan.
    if (hover_ == kConfigButton) gfx::FillRounded(dc, configButton_, style.Px(14), pal.hover);
    gfx::StrokeRounded(dc, configButton_, style.Px(14), pal.border);
    gfx::Text(dc, L"+", configButton_, fonts.semibold, pal.textMuted,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

int PrinterBar::HitTest(POINT pt) const {
    if (gfx::Contains(configButton_, pt)) return kConfigButton;
    if (pt.x >= configButton_.left - Style::Get().Px(4)) return -1;
    for (size_t i = 0; i < rects_.size(); ++i)
        if (gfx::Contains(rects_[i], pt)) return static_cast<int>(i);
    return -1;
}

LRESULT PrinterBar::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            gfx::BufferedPaint paint(hwnd_);
            Paint(paint.dc(), paint.rect());
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: Layout(); return 0;
        case WM_MOUSEMOVE: {
            if (!trackingMouse_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                trackingMouse_ = true;
            }
            int hit = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (hit != hover_) {
                hover_ = hit;
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            hover_ = -1;
            Invalidate();
            return 0;
        case WM_MOUSEWHEEL:
            ScrollBy(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -Style::Get().Px(60) : Style::Get().Px(60));
            return 0;
        case WM_LBUTTONDOWN:
            pressed_ = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case WM_LBUTTONUP: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int hit = HitTest(pt);
            if (hit == pressed_ && hit == kConfigButton && callbacks_.configure) {
                POINT screen{configButton_.left, configButton_.bottom + Style::Get().Px(4)};
                ClientToScreen(hwnd_, &screen);
                callbacks_.configure(screen);
            } else if (hit == pressed_ && hit >= 0 && static_cast<size_t>(hit) < chips_.size() && callbacks_.selected) {
                callbacks_.selected(chips_[static_cast<size_t>(hit)].name);
            }
            pressed_ = -1;
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int hit = HitTest(pt);
            if (hit >= 0 && static_cast<size_t>(hit) < chips_.size() && callbacks_.contextMenu) {
                POINT screen = pt;
                ClientToScreen(hwnd_, &screen);
                callbacks_.contextMenu(chips_[static_cast<size_t>(hit)].name, screen);
            }
            return 0;
        }
    }
    return Window::HandleMessage(msg, wParam, lParam);
}

}  // namespace cola::ui
