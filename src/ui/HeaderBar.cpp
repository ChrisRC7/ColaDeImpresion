#include "ui/HeaderBar.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>

#include "i18n/Strings.h"
#include "ui/DarkMode.h"
#include "ui/Gfx.h"
#include "ui/Style.h"

namespace cola::ui {

using i18n::S;
using i18n::Tr;

namespace {
constexpr int kEditId = 100;
}

bool HeaderBar::Create(HWND parent, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    if (!CreateWin(L"ColaHeaderBar", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, 0,
                   parent))
        return false;
    edit_ = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)), GetModuleHandleW(nullptr), nullptr);
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd_, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    for (int i = 0; i < static_cast<int>(HeaderButton::Count_); ++i) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = hwnd_;
        ti.uId = static_cast<UINT_PTR>(i + 1);
        ti.lpszText = const_cast<LPWSTR>(L"");
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    }
    OnStyleChanged();
    return true;
}

int HeaderBar::PreferredHeight() const { return Style::Get().Px(56); }

void HeaderBar::OnStyleChanged() {
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(Style::Get().fonts().normal), TRUE);
    ApplyControlDarkMode(edit_, Style::Get().dark());
    UpdateCueBanner();
    UpdateTooltips();
    Layout();
    Invalidate();
    InvalidateRect(edit_, nullptr, TRUE);
}

void HeaderBar::UpdateTooltips() {
    const S texts[] = {S::TipPrinter, S::TipHistory, S::TipHelp, S::TipTheme, S::TipMenu};
    for (int i = 0; i < static_cast<int>(HeaderButton::Count_); ++i) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.hwnd = hwnd_;
        ti.uId = static_cast<UINT_PTR>(i + 1);
        ti.rect = buttons_[i];
        ti.lpszText = const_cast<LPWSTR>(Tr(texts[i]));
        SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
        SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    }
}

void HeaderBar::UpdateCueBanner() {
    SendMessageW(edit_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(Tr(serverMode_ ? S::ServerPlaceholder : S::SearchPlaceholder)));
}

void HeaderBar::SetPrinterLabel(const std::wstring& title, const std::wstring& subtitle, bool warning) {
    if (title == printerTitle_ && subtitle == printerSubtitle_ && warning == printerWarning_) return;
    printerTitle_ = title;
    printerSubtitle_ = subtitle;
    printerWarning_ = warning;
    Layout();
    Invalidate();
}

void HeaderBar::SetHistoryActive(bool active) {
    historyActive_ = active;
    Invalidate();
}

void HeaderBar::FocusSearch() {
    SetFocus(edit_);
    SendMessageW(edit_, EM_SETSEL, 0, -1);
}

std::wstring HeaderBar::EditText() const {
    int len = GetWindowTextLengthW(edit_);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(edit_, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
    return text;
}

void HeaderBar::ClearSearch() {
    if (serverMode_) return;
    SetWindowTextW(edit_, L"");
}

void HeaderBar::BeginServerInput(const std::wstring& current) {
    if (!serverMode_) savedSearch_ = EditText();
    serverMode_ = true;
    suppressChange_ = true;
    SetWindowTextW(edit_, current.c_str());
    suppressChange_ = false;
    UpdateCueBanner();
    FocusSearch();
    Invalidate();
}

void HeaderBar::EndServerInput() {
    if (!serverMode_) return;
    serverMode_ = false;
    suppressChange_ = true;
    SetWindowTextW(edit_, savedSearch_.c_str());
    suppressChange_ = false;
    UpdateCueBanner();
    Invalidate();
}

RECT HeaderBar::ButtonScreenRect(HeaderButton button) const {
    RECT r = buttons_[static_cast<int>(button)];
    MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&r), 2);
    return r;
}

void HeaderBar::Layout() {
    if (!hwnd_) return;
    const auto& style = Style::Get();
    RECT client;
    GetClientRect(hwnd_, &client);
    const int pad = style.Px(12);
    const int btn = style.Px(36);
    const int cy = (client.top + client.bottom) / 2;

    // Botones a la derecha (de derecha a izquierda: menú, tema, ayuda, historial).
    int x = client.right - pad;
    for (HeaderButton b : {HeaderButton::Menu, HeaderButton::Theme, HeaderButton::Help, HeaderButton::History}) {
        buttons_[static_cast<int>(b)] = {x - btn, cy - btn / 2, x, cy + btn / 2};
        x -= btn + style.Px(2);
    }
    const int rightLimit = x - style.Px(10);

    // Selector de impresora a la izquierda (ancho según el texto).
    HDC dc = GetDC(hwnd_);
    SIZE titleSize = gfx::MeasureText(dc, printerTitle_, style.fonts().semibold);
    SIZE subSize = gfx::MeasureText(dc, printerSubtitle_, style.fonts().small);
    ReleaseDC(hwnd_, dc);
    int printerW = style.Px(12 + 24 + 8) + static_cast<int>(std::max(titleSize.cx, subSize.cx)) + style.Px(28);
    printerW = std::clamp(printerW, style.Px(160), std::max(style.Px(160), (rightLimit - pad) / 2));
    buttons_[static_cast<int>(HeaderButton::Printer)] = {client.left + pad, cy - style.Px(20),
                                                         client.left + pad + printerW, cy + style.Px(20)};

    // Búsqueda: el espacio restante (máx. 480 dip).
    int left = buttons_[static_cast<int>(HeaderButton::Printer)].right + style.Px(12);
    int width = std::min(rightLimit - left, style.Px(480));
    if (width < style.Px(80)) width = 0;
    searchBox_ = {left, cy - style.Px(17), left + width, cy + style.Px(17)};

    HFONT font = style.fonts().normal;
    dc = GetDC(hwnd_);
    SIZE lineSize = gfx::MeasureText(dc, L"Ag", font);
    ReleaseDC(hwnd_, dc);
    int editLeft = searchBox_.left + style.Px(34);
    int editH = lineSize.cy + style.Px(2);
    MoveWindow(edit_, editLeft, cy - editH / 2, std::max(0, static_cast<int>(searchBox_.right) - style.Px(10) - editLeft), editH, TRUE);
    ShowWindow(edit_, width > 0 ? SW_SHOW : SW_HIDE);
    UpdateTooltips();
}

void HeaderBar::Paint(HDC dc, const RECT& client) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const auto& fonts = style.fonts();
    gfx::Fill(dc, client, pal.surfaceAlt);
    gfx::HLine(dc, client.left, client.right, client.bottom - 1, pal.border);

    // Selector de impresora.
    RECT pr = buttons_[static_cast<int>(HeaderButton::Printer)];
    int printerIndex = static_cast<int>(HeaderButton::Printer);
    gfx::FillRounded(dc, pr, style.Px(8), hover_ == printerIndex ? pal.hover : pal.surface);
    gfx::StrokeRounded(dc, pr, style.Px(8), pal.border);
    RECT icon{pr.left + style.Px(8), pr.top, pr.left + style.Px(34), pr.bottom};
    gfx::Glyph(dc, printerWarning_ ? glyph::Warning : glyph::Printer, icon, fonts.icon,
               printerWarning_ ? pal.warning : pal.accent);
    RECT chevron{pr.right - style.Px(26), pr.top, pr.right - style.Px(6), pr.bottom};
    gfx::Glyph(dc, glyph::ChevronDown, chevron, fonts.iconSmall, pal.textMuted);
    RECT titleRect{icon.right + style.Px(4), pr.top + style.Px(3), chevron.left, (pr.top + pr.bottom) / 2 + style.Px(2)};
    RECT subRect{icon.right + style.Px(4), (pr.top + pr.bottom) / 2, chevron.left, pr.bottom - style.Px(3)};
    if (printerSubtitle_.empty()) {
        gfx::Text(dc, printerTitle_, RECT{titleRect.left, pr.top, titleRect.right, pr.bottom}, fonts.semibold, pal.text);
    } else {
        gfx::Text(dc, printerTitle_, titleRect, fonts.semibold, pal.text,
                  DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        gfx::Text(dc, printerSubtitle_, subRect, fonts.small, printerWarning_ ? pal.warning : pal.textMuted,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    // Cuadro de búsqueda / servidor.
    if (gfx::Width(searchBox_) > 0) {
        bool focused = GetFocus() == edit_;
        gfx::FillRounded(dc, searchBox_, style.Px(8), pal.surface);
        gfx::StrokeRounded(dc, searchBox_, style.Px(8), focused ? pal.accent : pal.border, focused ? 1.5f : 1.0f);
        RECT glyphRect{searchBox_.left + style.Px(6), searchBox_.top, searchBox_.left + style.Px(32), searchBox_.bottom};
        gfx::Glyph(dc, serverMode_ ? glyph::Server : glyph::Search, glyphRect, fonts.iconSmall, pal.textMuted);
    }

    // Botones de ícono.
    struct IconButton {
        HeaderButton id;
        wchar_t glyph;
        bool active;
    };
    const IconButton icons[] = {
        {HeaderButton::History, glyph::History, historyActive_},
        {HeaderButton::Help, glyph::Keyboard, false},
        {HeaderButton::Theme, style.dark() ? glyph::Sun : glyph::Moon, false},
        {HeaderButton::Menu, glyph::More, false},
    };
    for (const auto& b : icons) {
        RECT r = buttons_[static_cast<int>(b.id)];
        int index = static_cast<int>(b.id);
        if (b.active) gfx::FillRounded(dc, r, style.Px(8), pal.selection);
        else if (hover_ == index) gfx::FillRounded(dc, r, style.Px(8), pal.hover);
        gfx::Glyph(dc, b.glyph, r, fonts.icon, b.active ? pal.accent : pal.text);
    }
}

int HeaderBar::HitTest(POINT pt) const {
    for (int i = 0; i < static_cast<int>(HeaderButton::Count_); ++i)
        if (gfx::Contains(buttons_[i], pt)) return i;
    return -1;
}

LRESULT HeaderBar::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            gfx::BufferedPaint paint(hwnd_);
            Paint(paint.dc(), paint.rect());
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: Layout(); return 0;
        case WM_CTLCOLOREDIT: {
            const auto& pal = Style::Get().palette();
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, pal.text);
            SetBkColor(dc, pal.surface);
            return reinterpret_cast<LRESULT>(Style::Get().Brush(pal.surface));
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == kEditId) {
                if (HIWORD(wParam) == EN_CHANGE && !serverMode_ && !suppressChange_ && callbacks_.searchChanged)
                    callbacks_.searchChanged(EditText());
                if (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS) InvalidateRect(hwnd_, &searchBox_, FALSE);
                return 0;
            }
            break;
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
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (gfx::Contains(searchBox_, pt)) {
                SetFocus(edit_);
                return 0;
            }
            pressed_ = HitTest(pt);
            return 0;
        }
        case WM_LBUTTONUP: {
            int hit = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (hit >= 0 && hit == pressed_ && callbacks_.clicked)
                callbacks_.clicked(static_cast<HeaderButton>(hit), ButtonScreenRect(static_cast<HeaderButton>(hit)));
            pressed_ = -1;
            return 0;
        }
    }
    return Window::HandleMessage(msg, wParam, lParam);
}

}  // namespace cola::ui
