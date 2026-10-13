#include "ui/AboutDialog.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <string>

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
    const wchar_t* email;
};

// Nombres propios: no se traducen.
constexpr Author kAuthors[] = {
    {L"Christopher David Rodríguez Cordero", L"CR", L"chrisrc7@live.com"},
    {L"Alejandro Barquero Fuentes", L"AB", L"alebfuentes@gmail.com"},
};
constexpr int kAuthorCount = static_cast<int>(sizeof(kAuthors) / sizeof(kAuthors[0]));

constexpr wchar_t kGlyphMail = 0xE715;
constexpr wchar_t kGlyphCopy = 0xE8C8;
constexpr UINT_PTR kTimerCopied = 1;

// Elementos que reciben el foco: correo y copiar de cada autor, y Cerrar.
constexpr int MailItem(int author) { return author * 2; }
constexpr int CopyItem(int author) { return author * 2 + 1; }
constexpr int kClose = kAuthorCount * 2;
constexpr int kItemCount = kClose + 1;

bool CopyToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (mem) {
        if (void* p = GlobalLock(mem)) {
            std::memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        }
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

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
                const int hit = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                if (hit != hover_) {
                    hover_ = hit;
                    Invalidate();
                }
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                return 0;
            }
            case WM_MOUSELEAVE:
                hover_ = -1;
                Invalidate();
                return 0;
            case WM_SETCURSOR:
                if (LOWORD(lParam) == HTCLIENT && hover_ >= 0) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
                break;
            case WM_LBUTTONDOWN: {
                const int hit = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                if (hit >= 0) {
                    focus_ = hit;
                    Invalidate();
                    Activate(hit);
                }
                return 0;
            }
            case WM_KEYDOWN: OnKey(static_cast<UINT>(wParam)); return 0;
            case WM_TIMER:
                if (wParam == kTimerCopied) {
                    KillTimer(hwnd_, kTimerCopied);
                    copied_ = -1;
                    Invalidate();
                    return 0;
                }
                break;
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
        HDC dc = GetDC(nullptr);

        // Encabezado: ícono, nombre, versión y descripción sobre una franja de color.
        int y = pad;
        iconRect_ = {pad, y, pad + style.Px(64), y + style.Px(64)};
        const int textLeft = iconRect_.right + style.Px(18);
        name_ = {textLeft, y, right, y + style.Px(26)};
        version_ = {textLeft, name_.bottom, right, name_.bottom + style.Px(20)};
        const int descH = gfx::MeasureWrapped(dc, Tr(S::AboutDescription), fonts.small, right - textLeft);
        description_ = {textLeft, version_.bottom + style.Px(8), right, version_.bottom + style.Px(8) + descH};
        band_ = {0, 0, style.Px(kWidth), std::max(iconRect_.bottom, description_.bottom) + pad};

        // Autores.
        y = band_.bottom + style.Px(20);
        section_ = {pad, y, right, y + style.Px(18)};
        y = section_.bottom + style.Px(10);
        for (int i = 0; i < kAuthorCount; ++i) {
            RECT& card = cards_[i];
            card = {pad, y, right, y + style.Px(64)};
            const int infoLeft = card.left + style.Px(16 + 40 + 14);  // margen, avatar, separación
            const int copySize = style.Px(32);
            const int midY = (card.top + card.bottom) / 2;
            rects_[CopyItem(i)] = {card.right - style.Px(12) - copySize, midY - copySize / 2, card.right - style.Px(12),
                                   midY + copySize / 2};
            names_[i] = {infoLeft, card.top + style.Px(10), rects_[CopyItem(i)].left - style.Px(8), midY + style.Px(2)};
            // El enlace del correo ocupa solo el ancho del texto (más el ícono).
            const LONG mailW = gfx::MeasureText(dc, kAuthors[i].email, fonts.normal).cx + style.Px(22);
            rects_[MailItem(i)] = {infoLeft - style.Px(4), midY + style.Px(1),
                                   std::min(infoLeft + mailW, rects_[CopyItem(i)].left - style.Px(8)),
                                   card.bottom - style.Px(8)};
            y = card.bottom + style.Px(10);
        }

        // Pie: créditos o aviso de copiado, y el botón Cerrar.
        y += style.Px(10);
        const int buttonH = style.Px(34);
        rects_[kClose] = {right - style.Px(110), y, right, y + buttonH};
        footer_ = {pad, y, rects_[kClose].left - style.Px(12), y + buttonH};
        ReleaseDC(nullptr, dc);
        return y + buttonH + pad;
    }

    int HitTest(POINT p) const {
        for (int i = 0; i < kItemCount; ++i)
            if (gfx::Contains(rects_[i], p)) return i;
        return -1;
    }

    void Activate(int item) {
        if (item == kClose) {
            done_ = true;
            return;
        }
        const int author = item / 2;
        const std::wstring email = kAuthors[author].email;
        if (item == MailItem(author)) {
            const std::wstring url = L"mailto:" + email;
            const auto result = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwnd_, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
            if (result > 32) return;
            // Sin programa de correo: se copia para que no quede sin respuesta.
        }
        if (CopyToClipboard(hwnd_, email)) {
            copied_ = author;
            SetTimer(hwnd_, kTimerCopied, 2500, nullptr);
        }
        Invalidate();
    }

    void OnKey(UINT key) {
        const bool shift = GetKeyState(VK_SHIFT) < 0;
        switch (key) {
            case VK_ESCAPE: done_ = true; return;
            case VK_RETURN:
            case VK_SPACE: Activate(focus_); return;
            case VK_TAB: focus_ = (focus_ + (shift ? kItemCount - 1 : 1)) % kItemCount; break;
            case VK_UP:
            case VK_LEFT: focus_ = (focus_ + kItemCount - 1) % kItemCount; break;
            case VK_DOWN:
            case VK_RIGHT: focus_ = (focus_ + 1) % kItemCount; break;
            default: return;
        }
        Invalidate();
    }

    void FocusRing(HDC dc, const RECT& r, int radius) const {
        const Style& style = Style::Get();
        const RECT ring = gfx::Inset(r, -style.Px(2), -style.Px(2));
        gfx::StrokeRounded(dc, ring, radius + style.Px(2), style.palette().accent, 1.5f);
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
        const COLORREF bandColor = Blend(pal.surface, pal.accent, pal.dark ? 0.14 : 0.08);
        gfx::Fill(dc, band_, bandColor);
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

        // Autores.
        gfx::Text(dc, Tr(S::AboutAuthors), section_, fonts.small, pal.textMuted);
        for (int i = 0; i < kAuthorCount; ++i) {
            const RECT& card = cards_[i];
            gfx::FillRounded(dc, card, radius, pal.surfaceAlt);
            gfx::StrokeRounded(dc, card, radius, pal.border);
            const int cx = card.left + style.Px(16 + 20), cy = (card.top + card.bottom) / 2;
            gfx::FillCircle(dc, cx, cy, style.Px(20), pal.accent);
            gfx::Text(dc, kAuthors[i].initials, {cx - style.Px(20), cy - style.Px(20), cx + style.Px(20), cy + style.Px(20)},
                      fonts.semibold, pal.accentText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            gfx::Text(dc, kAuthors[i].name, names_[i], fonts.semibold, pal.text);

            // Correo como enlace: subrayado al pasar el mouse.
            const RECT& mail = rects_[MailItem(i)];
            const bool mailHover = hover_ == MailItem(i);
            if (mailHover) gfx::FillRounded(dc, mail, style.Px(4), pal.hover);
            if (focus_ == MailItem(i)) FocusRing(dc, mail, style.Px(4));
            const RECT mailGlyph{mail.left + style.Px(4), mail.top, mail.left + style.Px(20), mail.bottom};
            gfx::Glyph(dc, kGlyphMail, mailGlyph, fonts.iconSmall, pal.accent);
            const RECT mailText{mailGlyph.right + style.Px(4), mail.top, mail.right, mail.bottom};
            gfx::Text(dc, kAuthors[i].email, mailText, fonts.normal, pal.accent);
            if (mailHover) {
                const LONG w = gfx::MeasureText(dc, kAuthors[i].email, fonts.normal).cx;
                const int baseline = (mail.top + mail.bottom) / 2 + style.Px(9);
                gfx::HLine(dc, mailText.left, std::min(mailText.left + w, mail.right), baseline, pal.accent);
            }

            // Copiar: muestra una marca de listo durante unos segundos.
            const RECT& copy = rects_[CopyItem(i)];
            if (hover_ == CopyItem(i)) gfx::FillRounded(dc, copy, radius, pal.hover);
            if (focus_ == CopyItem(i)) FocusRing(dc, copy, radius);
            const bool done = copied_ == i;
            gfx::Glyph(dc, done ? glyph::Completed : kGlyphCopy, copy, fonts.iconSmall,
                       done ? pal.success : pal.textMuted);
        }

        // Pie.
        if (copied_ >= 0) gfx::Text(dc, Tr(S::AboutCopied), footer_, fonts.small, pal.success);
        else gfx::Text(dc, Tr(S::AboutFooter), footer_, fonts.small, pal.textMuted);
        const RECT& close = rects_[kClose];
        gfx::FillRounded(dc, close, radius, pal.accent);
        if (focus_ == kClose) FocusRing(dc, close, radius);
        gfx::Text(dc, Tr(S::AboutClose), close, fonts.semibold, pal.accentText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    HICON icon_ = nullptr;
    RECT rects_[kItemCount]{};
    RECT cards_[kAuthorCount]{};
    RECT names_[kAuthorCount]{};
    RECT band_{}, iconRect_{}, name_{}, version_{}, description_{}, section_{}, footer_{};
    int focus_ = kClose;
    int hover_ = -1;
    int copied_ = -1;  // autor cuyo correo se acaba de copiar
    bool done_ = false;
};

}  // namespace

void ShowAboutDialog(HWND owner) {
    AboutDialog dialog;
    dialog.Run(owner);
}

}  // namespace cola::ui
