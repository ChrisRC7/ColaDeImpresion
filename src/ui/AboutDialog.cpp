#include "ui/AboutDialog.h"

#include <windowsx.h>

#include <algorithm>

#include "i18n/Strings.h"
#include "ui/DarkMode.h"
#include "ui/Gfx.h"
#include "ui/Style.h"
#include "ui/Window.h"
#include "util/Strings.h"

namespace cola::ui {

using i18n::S;
using i18n::Tr;

namespace {

struct Author {
    const wchar_t* name;
    const wchar_t* initials;
};

// Nombres propios: no se traducen.
constexpr Author kAuthors[] = {
    {L"Christopher David Rodríguez Cordero", L"CR"},
    {L"Alejandro Barquero Fuentes", L"AB"},
};
constexpr int kAuthorCount = static_cast<int>(sizeof(kAuthors) / sizeof(kAuthors[0]));

class AboutDialog : public Window {
public:
    ~AboutDialog() override {
        if (icon_) DestroyIcon(icon_);
    }

    void Run(HWND owner) {
        const Style& style = Style::Get();
        const int size = style.Px(64);
        icon_ = static_cast<HICON>(
            LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
        const RECT client{0, 0, style.Px(kWidth), Layout()};
        RECT frame = client;
        const DWORD ws = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        const DWORD ex = WS_EX_DLGMODALFRAME;
        AdjustWindowRectEx(&frame, ws, FALSE, ex);
        const int w = frame.right - frame.left, h = frame.bottom - frame.top;
        RECT ownerRect{};
        GetWindowRect(owner, &ownerRect);
        const int x = ownerRect.left + (ownerRect.right - ownerRect.left - w) / 2;
        const int y = ownerRect.top + std::max(0L, (ownerRect.bottom - ownerRect.top - h) / 3);
        if (!CreateWin(L"ColaAboutDialog", Tr(S::AboutTitle), ws, ex, x, y, w, h, owner)) return;
        ApplyWindowDarkMode(hwnd_, style.dark());

        EnableWindow(owner, FALSE);
        ShowWindow(hwnd_, SW_SHOW);
        SetFocus(hwnd_);
        MSG msg;
        bool quit = false;
        while (!done_) {
            if (GetMessageW(&msg, nullptr, 0, 0) <= 0) {
                quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        // Reactivar el dueño antes de destruir: si no, Windows activa otra aplicación.
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
        DestroyWindow(hwnd_);
        if (quit) PostQuitMessage(static_cast<int>(msg.wParam));
    }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override {
        switch (msg) {
            case WM_PAINT: Paint(); return 0;
            case WM_ERASEBKGND: return 1;
            case WM_MOUSEMOVE: {
                const bool hit = gfx::Contains(close_, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                if (hit != hoverClose_) {
                    hoverClose_ = hit;
                    Invalidate();
                }
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                return 0;
            }
            case WM_MOUSELEAVE:
                hoverClose_ = false;
                Invalidate();
                return 0;
            case WM_LBUTTONUP:
                if (gfx::Contains(close_, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)})) done_ = true;
                return 0;
            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE || wParam == VK_RETURN || wParam == VK_SPACE) done_ = true;
                return 0;
            case WM_CLOSE: done_ = true; return 0;
            default: break;
        }
        return DefaultProc(msg, wParam, lParam);
    }

private:
    static constexpr int kWidth = 480;  // dip

    // Calcula los rectángulos y devuelve el alto del área cliente.
    int Layout() {
        const Style& style = Style::Get();
        const Fonts& fonts = style.fonts();
        const int pad = style.Px(24);
        const int right = style.Px(kWidth) - pad;

        // Encabezado: ícono, nombre, versión y descripción sobre una franja de color.
        int y = pad;
        iconRect_ = {pad, y, pad + style.Px(64), y + style.Px(64)};
        const int textLeft = iconRect_.right + style.Px(18);
        name_ = {textLeft, y, right, y + style.Px(26)};
        version_ = {textLeft, name_.bottom, right, name_.bottom + style.Px(20)};
        HDC dc = GetDC(nullptr);
        const int descH = gfx::MeasureWrapped(dc, Tr(S::AboutDescription), fonts.small, right - textLeft);
        ReleaseDC(nullptr, dc);
        description_ = {textLeft, version_.bottom + style.Px(8), right, version_.bottom + style.Px(8) + descH};
        band_ = {0, 0, style.Px(kWidth), std::max(iconRect_.bottom, description_.bottom) + pad};

        // Autores.
        y = band_.bottom + style.Px(20);
        section_ = {pad, y, right, y + style.Px(18)};
        y = section_.bottom + style.Px(10);
        for (RECT& card : cards_) {
            card = {pad, y, right, y + style.Px(56)};
            y = card.bottom + style.Px(10);
        }

        // Pie: créditos y el botón Cerrar.
        y += style.Px(10);
        const int buttonH = style.Px(34);
        close_ = {right - style.Px(110), y, right, y + buttonH};
        footer_ = {pad, y, close_.left - style.Px(12), y + buttonH};
        return y + buttonH + pad;
    }

    void Paint() {
        gfx::BufferedPaint paint(hwnd_);
        HDC dc = paint.dc();
        const Style& style = Style::Get();
        const Palette& pal = style.palette();
        const Fonts& fonts = style.fonts();
        const int radius = style.Px(8);
        gfx::Fill(dc, paint.rect(), pal.surface);

        // Encabezado.
        gfx::Fill(dc, band_, Blend(pal.surface, pal.accent, pal.dark ? 0.14 : 0.08));
        gfx::HLine(dc, band_.left, band_.right, band_.bottom - 1, pal.border);
        if (icon_) {
            DrawIconEx(dc, iconRect_.left, iconRect_.top, icon_, gfx::Width(iconRect_), gfx::Height(iconRect_), 0,
                       nullptr, DI_NORMAL);
        } else {
            gfx::Glyph(dc, glyph::Printer, iconRect_, fonts.iconLarge, pal.accent);
        }
        gfx::Text(dc, Tr(S::AboutAppName), name_, fonts.title, pal.text);
        gfx::Text(dc, util::Format(Tr(S::AboutVersion), COLA_VERSION_STRING), version_, fonts.normal, pal.accent);
        gfx::Text(dc, Tr(S::AboutDescription), description_, fonts.small, pal.textMuted,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

        // Autores: iniciales en un círculo y el nombre.
        gfx::Text(dc, Tr(S::AboutAuthors), section_, fonts.small, pal.textMuted);
        for (int i = 0; i < kAuthorCount; ++i) {
            const RECT& card = cards_[i];
            gfx::FillRounded(dc, card, radius, pal.surfaceAlt);
            gfx::StrokeRounded(dc, card, radius, pal.border);
            const int r = style.Px(18);
            const int cx = card.left + style.Px(16) + r, cy = (card.top + card.bottom) / 2;
            gfx::FillCircle(dc, cx, cy, r, pal.accent);
            gfx::Text(dc, kAuthors[i].initials, {cx - r, cy - r, cx + r, cy + r}, fonts.semibold, pal.accentText,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            gfx::Text(dc, kAuthors[i].name, {cx + r + style.Px(14), card.top, card.right - style.Px(12), card.bottom},
                      fonts.semibold, pal.text);
        }

        // Pie.
        gfx::Text(dc, Tr(S::AboutFooter), footer_, fonts.small, pal.textMuted);
        gfx::FillRounded(dc, close_, radius, hoverClose_ ? Blend(pal.accent, pal.text, 0.15) : pal.accent);
        const RECT ring = gfx::Inset(close_, -style.Px(2), -style.Px(2));
        gfx::StrokeRounded(dc, ring, radius + style.Px(2), pal.accent, 1.5f);
        gfx::Text(dc, Tr(S::AboutClose), close_, fonts.semibold, pal.accentText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    HICON icon_ = nullptr;
    RECT cards_[kAuthorCount]{};
    RECT band_{}, iconRect_{}, name_{}, version_{}, description_{}, section_{}, footer_{}, close_{};
    bool hoverClose_ = false;
    bool done_ = false;
};

}  // namespace

void ShowAboutDialog(HWND owner) {
    AboutDialog dialog;
    dialog.Run(owner);
}

}  // namespace cola::ui
