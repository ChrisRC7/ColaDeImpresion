#include "ui/HelpOverlay.h"

#include <windowsx.h>

#include <algorithm>

#include "i18n/Strings.h"
#include "ui/Gfx.h"
#include "ui/Style.h"
#include "util/Strings.h"

namespace cola::ui {

using i18n::S;
using i18n::Tr;
using input::Command;
using input::kCommandCount;

bool HelpOverlay::Create(HWND parent, const input::ShortcutMap* shortcuts, Callbacks callbacks) {
    shortcuts_ = shortcuts;
    callbacks_ = std::move(callbacks);
    return CreateWin(L"ColaHelpOverlay", L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, 0, parent);
}

void HelpOverlay::Open() {
    capturing_ = false;
    message_.clear();
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetFocus(hwnd_);
    Invalidate();
}

void HelpOverlay::Close() {
    capturing_ = false;
    Show(false);
}

int HelpOverlay::RowHeight() const { return Style::Get().Px(28); }

RECT HelpOverlay::CardRect(const RECT& client) const {
    const auto& style = Style::Get();
    int w = std::min(gfx::Width(client) - style.Px(32), style.Px(620));
    int h = std::min(gfx::Height(client) - style.Px(32), style.Px(640));
    int x = (client.left + client.right - w) / 2;
    int y = (client.top + client.bottom - h) / 2;
    return {x, y, x + w, y + h};
}

void HelpOverlay::EnsureVisible() {
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + visibleRows_) scroll_ = selected_ - visibleRows_ + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, kCommandCount - visibleRows_));
}

bool HelpOverlay::HandleKey(UINT vk) {
    if (capturing_) {
        if (vk == VK_ESCAPE) {
            capturing_ = false;
            message_.clear();
            Invalidate();
            return true;
        }
        if (vk == VK_DELETE || vk == VK_BACK) {
            if (callbacks_.clear) callbacks_.clear(static_cast<Command>(selected_));
            capturing_ = false;
            message_.clear();
            Invalidate();
            return true;
        }
        // Ignora las teclas modificadoras solas: se espera la combinación completa.
        if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN ||
            vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LSHIFT || vk == VK_RSHIFT)
            return true;
        input::KeyChord chord = input::KeyChord::FromKeyEvent(vk);
        capturing_ = false;
        message_ = callbacks_.assign ? callbacks_.assign(static_cast<Command>(selected_), chord) : L"";
        Invalidate();
        return true;
    }

    switch (vk) {
        case VK_ESCAPE:
        case VK_F1:
            if (callbacks_.close) callbacks_.close();
            return true;
        case VK_UP: selected_ = std::max(0, selected_ - 1); break;
        case VK_DOWN: selected_ = std::min(kCommandCount - 1, selected_ + 1); break;
        case VK_PRIOR: selected_ = std::max(0, selected_ - visibleRows_); break;
        case VK_NEXT: selected_ = std::min(kCommandCount - 1, selected_ + visibleRows_); break;
        case VK_HOME: selected_ = 0; break;
        case VK_END: selected_ = kCommandCount - 1; break;
        case VK_RETURN:
            capturing_ = true;
            message_ = util::Format(Tr(S::ShortcutPrompt), Tr(input::GetCommandInfo(static_cast<Command>(selected_)).label));
            break;
        case VK_DELETE:
            if (callbacks_.clear) callbacks_.clear(static_cast<Command>(selected_));
            message_.clear();
            break;
        case 'R':
            if (callbacks_.resetDefaults) callbacks_.resetDefaults();
            message_.clear();
            break;
        default: return true;  // el panel es modal: consume todo
    }
    EnsureVisible();
    Invalidate();
    return true;
}

void HelpOverlay::Paint(HDC dc, const RECT& client) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const auto& fonts = style.fonts();
    gfx::Fill(dc, client, Blend(pal.window, pal.dark ? RGB(0, 0, 0) : RGB(40, 44, 52), pal.dark ? 0.45 : 0.28));

    RECT card = CardRect(client);
    gfx::FillRounded(dc, card, style.Px(12), pal.overlayScrim);
    gfx::StrokeRounded(dc, card, style.Px(12), pal.border);

    const int pad = style.Px(22);
    RECT title{card.left + pad, card.top + style.Px(16), card.right - pad, card.top + style.Px(44)};
    gfx::Text(dc, Tr(S::HelpTitle), title, fonts.title, pal.text);

    RECT footer{card.left + pad, card.bottom - style.Px(62), card.right - pad, card.bottom - style.Px(12)};
    std::wstring footerText = message_.empty() ? std::wstring(Tr(S::HelpFooter)) + L"\n" + Tr(S::HelpFixedKeys) : message_;
    gfx::Text(dc, footerText, footer, fonts.small, message_.empty() ? pal.textMuted : pal.accent,
              DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    RECT listArea{card.left + style.Px(10), title.bottom + style.Px(10), card.right - style.Px(10), footer.top - style.Px(8)};
    visibleRows_ = std::max(1, gfx::Height(listArea) / RowHeight());
    EnsureVisible();

    for (int i = scroll_; i < kCommandCount && i < scroll_ + visibleRows_; ++i) {
        int top = listArea.top + (i - scroll_) * RowHeight();
        RECT row{listArea.left, top, listArea.right, top + RowHeight()};
        const bool sel = i == selected_;
        if (sel) gfx::FillRounded(dc, row, style.Px(6), capturing_ ? Blend(pal.selection, pal.accent, 0.25) : pal.selection);
        const auto& info = input::GetCommandInfo(static_cast<Command>(i));
        RECT label{row.left + style.Px(12), row.top, row.right - style.Px(200), row.bottom};
        gfx::Text(dc, Tr(info.label), label, fonts.normal, sel ? pal.selectionText : pal.text);

        std::wstring keys = input::FormatChordList(shortcuts_->Get(static_cast<Command>(i)));
        bool none = keys.empty();
        if (none) keys = Tr(S::HelpUnassigned);
        RECT keyRect{row.right - style.Px(196), row.top, row.right - style.Px(12), row.bottom};
        SIZE sz = gfx::MeasureText(dc, keys, fonts.small);
        if (!none) {
            RECT chip{keyRect.right - sz.cx - style.Px(14), row.top + style.Px(4), keyRect.right, row.bottom - style.Px(4)};
            chip.left = std::max(chip.left, keyRect.left);
            gfx::FillRounded(dc, chip, style.Px(5), sel ? pal.surface : pal.surfaceAlt);
            gfx::StrokeRounded(dc, chip, style.Px(5), pal.border);
            gfx::Text(dc, keys, chip, fonts.small, pal.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else {
            gfx::Text(dc, keys, keyRect, fonts.small, pal.textMuted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
}

LRESULT HelpOverlay::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            gfx::BufferedPaint paint(hwnd_);
            Paint(paint.dc(), paint.rect());
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: Invalidate(); return 0;
        case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            HandleKey(static_cast<UINT>(wParam));
            return 0;
        case WM_MOUSEWHEEL:
            scroll_ -= GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 3;
            scroll_ = std::clamp(scroll_, 0, std::max(0, kCommandCount - visibleRows_));
            Invalidate();
            return 0;
        case WM_LBUTTONDOWN: {
            RECT client;
            GetClientRect(hwnd_, &client);
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (!gfx::Contains(CardRect(client), pt)) {
                if (callbacks_.close) callbacks_.close();
            }
            SetFocus(hwnd_);
            return 0;
        }
    }
    return Window::HandleMessage(msg, wParam, lParam);
}

}  // namespace cola::ui
