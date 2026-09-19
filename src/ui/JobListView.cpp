#include "ui/JobListView.h"

#include <windowsx.h>

#include <algorithm>
#include <climits>

#include "ui/DarkMode.h"
#include "ui/Gfx.h"
#include "ui/Style.h"

namespace cola::ui {

bool JobListView::Create(HWND parent, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    if (!CreateWin(L"ColaJobList", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                   0, parent))
        return false;
    OnStyleChanged();
    return true;
}

void JobListView::OnStyleChanged() {
    ApplyControlDarkMode(hwnd_, Style::Get().dark());
    UpdateScrollBar();
    Invalidate();
}

int JobListView::RowHeight() const { return Style::Get().Px(54); }

int JobListView::RowAt(int y) const {
    if (rows_.empty()) return -1;
    int row = (y + scrollY_) / RowHeight();
    return (row >= 0 && row < RowCount()) ? row : -1;
}

void JobListView::SetRows(std::vector<ListRow> rows) {
    std::wstring focusKey = FocusedKey();
    rows_ = std::move(rows);

    // Limpia de la selección las claves que ya no existen.
    std::set<std::wstring> alive;
    for (const auto& r : rows_) alive.insert(r.key);
    for (auto it = selected_.begin(); it != selected_.end();)
        it = alive.count(*it) ? std::next(it) : selected_.erase(it);

    int oldFocus = focus_;
    focus_ = -1;
    for (int i = 0; i < RowCount(); ++i)
        if (rows_[static_cast<size_t>(i)].key == focusKey) focus_ = i;
    bool selectionChanged = false;
    if (focus_ < 0 && !rows_.empty()) {
        // El trabajo enfocado desapareció: enfoca el que quedó en su lugar.
        focus_ = std::clamp(oldFocus, 0, RowCount() - 1);
        if (selected_.empty()) {
            selected_.insert(rows_[static_cast<size_t>(focus_)].key);
            selectionChanged = true;
        }
    }
    if (anchor_ >= RowCount()) anchor_ = focus_;
    if (hover_ >= RowCount()) hover_ = -1;
    if (focusKey != FocusedKey()) selectionChanged = true;

    UpdateScrollBar();
    Invalidate();
    if (selectionChanged) NotifySelection();
}

void JobListView::SetEmptyText(std::wstring title, std::wstring hint, wchar_t glyph) {
    emptyTitle_ = std::move(title);
    emptyHint_ = std::move(hint);
    emptyGlyph_ = glyph;
    if (rows_.empty()) Invalidate();
}

std::vector<std::wstring> JobListView::SelectedKeys() const {
    std::vector<std::wstring> keys;
    for (const auto& r : rows_)
        if (selected_.count(r.key)) keys.push_back(r.key);
    return keys;
}

std::wstring JobListView::FocusedKey() const {
    return (focus_ >= 0 && focus_ < RowCount()) ? rows_[static_cast<size_t>(focus_)].key : L"";
}

void JobListView::SelectKey(const std::wstring& key) {
    for (int i = 0; i < RowCount(); ++i) {
        if (rows_[static_cast<size_t>(i)].key == key) {
            SetFocusRow(i, false, false);
            return;
        }
    }
}

void JobListView::SelectAll() {
    for (const auto& r : rows_) selected_.insert(r.key);
    Invalidate();
    NotifySelection();
}

void JobListView::MoveFocus(int delta, bool extend) {
    if (rows_.empty()) return;
    int target;
    if (delta == INT_MIN) target = 0;
    else if (delta == INT_MAX) target = RowCount() - 1;
    else target = std::clamp((focus_ < 0 ? (delta > 0 ? -1 : RowCount()) : focus_) + delta, 0, RowCount() - 1);
    SetFocusRow(target, extend, false);
}

void JobListView::SetFocusRow(int row, bool extend, bool toggle) {
    if (row < 0 || row >= RowCount()) return;
    const std::wstring& key = rows_[static_cast<size_t>(row)].key;
    if (toggle) {
        if (selected_.count(key)) selected_.erase(key);
        else selected_.insert(key);
        anchor_ = row;
    } else if (extend && anchor_ >= 0 && anchor_ < RowCount()) {
        selected_.clear();
        for (int i = std::min(anchor_, row); i <= std::max(anchor_, row); ++i)
            selected_.insert(rows_[static_cast<size_t>(i)].key);
    } else {
        selected_.clear();
        selected_.insert(key);
        anchor_ = row;
    }
    focus_ = row;
    EnsureVisible(row);
    Invalidate();
    NotifySelection();
}

void JobListView::NotifySelection() {
    if (callbacks_.selectionChanged) callbacks_.selectionChanged();
}

void JobListView::UpdateScrollBar() {
    RECT client;
    GetClientRect(hwnd_, &client);
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = std::max(0, ContentHeight() - 1);
    si.nPage = static_cast<UINT>(std::max(1, gfx::Height(client)));
    int maxScroll = std::max(0, ContentHeight() - gfx::Height(client));
    scrollY_ = std::clamp(scrollY_, 0, maxScroll);
    si.nPos = scrollY_;
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
    ShowScrollBar(hwnd_, SB_VERT, ContentHeight() > gfx::Height(client));
}

void JobListView::ScrollTo(int y) {
    RECT client;
    GetClientRect(hwnd_, &client);
    int maxScroll = std::max(0, ContentHeight() - gfx::Height(client));
    y = std::clamp(y, 0, maxScroll);
    if (y == scrollY_) return;
    scrollY_ = y;
    SetScrollPos(hwnd_, SB_VERT, scrollY_, TRUE);
    Invalidate();
}

void JobListView::EnsureVisible(int row) {
    RECT client;
    GetClientRect(hwnd_, &client);
    int top = row * RowHeight();
    int bottom = top + RowHeight();
    if (top < scrollY_) ScrollTo(top);
    else if (bottom > scrollY_ + gfx::Height(client)) ScrollTo(bottom - gfx::Height(client));
}

void JobListView::Paint(HDC dc, const RECT& client) {
    const auto& pal = Style::Get().palette();
    gfx::Fill(dc, client, pal.surface);
    if (rows_.empty()) {
        PaintEmpty(dc, client);
        return;
    }
    int first = scrollY_ / RowHeight();
    for (int i = first; i < RowCount(); ++i) {
        int top = i * RowHeight() - scrollY_;
        if (top > client.bottom) break;
        RECT r{client.left, top, client.right, top + RowHeight()};
        PaintRow(dc, i, r);
    }
}

void JobListView::PaintRow(HDC dc, int index, const RECT& r) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const auto& fonts = style.fonts();
    const ListRow& row = rows_[static_cast<size_t>(index)];
    const bool selected = selected_.count(row.key) > 0;
    const bool focused = index == focus_ && GetFocus() == hwnd_;
    const int pad = style.Px(12);

    RECT card = gfx::Inset(r, style.Px(6), style.Px(2));
    if (selected) gfx::FillRounded(dc, card, style.Px(6), pal.selection);
    else if (index == hover_) gfx::FillRounded(dc, card, style.Px(6), pal.hover);
    if (focused && !selected) gfx::StrokeRounded(dc, card, style.Px(6), pal.accent);
    if (!selected && index != hover_ && index + 1 < RowCount() && !selected_.count(rows_[static_cast<size_t>(index + 1)].key))
        gfx::HLine(dc, r.left + pad + style.Px(28), r.right - pad, r.bottom - 1, pal.border);

    const COLORREF textColor = selected ? pal.selectionText : pal.text;
    const COLORREF mutedColor = selected ? Blend(pal.selectionText, pal.selection, 0.3) : pal.textMuted;
    const COLORREF toneColor = pal.ToneColor(row.tone);

    // Indicador de estado.
    int x = r.left + pad + style.Px(4);
    int cy = (r.top + r.bottom) / 2;
    RECT iconRect{x, cy - style.Px(10), x + style.Px(20), cy + style.Px(10)};
    if (row.glyph) gfx::Glyph(dc, row.glyph, iconRect, fonts.icon, toneColor);
    else gfx::FillCircle(dc, x + style.Px(10), cy, style.Px(4), toneColor);
    x += style.Px(32);

    // Columnas de la derecha: estado (insignia), tamaño, páginas.
    int right = r.right - pad - style.Px(6);
    SIZE statusSize = gfx::MeasureText(dc, row.status, fonts.small);
    int pillW = statusSize.cx + style.Px(20);
    RECT pill{right - pillW, cy - style.Px(11), right, cy + style.Px(11)};
    if (!row.status.empty()) {
        gfx::FillRounded(dc, pill, style.Px(11), selected ? pal.surface : pal.ToneBackground(row.tone));
        gfx::Text(dc, row.status, pill, fonts.small, toneColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    right = pill.left - style.Px(16);

    const int sizeW = style.Px(72);
    const int pagesW = style.Px(64);
    RECT client;
    GetClientRect(hwnd_, &client);
    const bool compact = gfx::Width(client) < style.Px(520);  // en ventanas angostas se ocultan columnas
    if (!compact) {
        RECT sizeRect{right - sizeW, r.top, right, r.bottom};
        gfx::Text(dc, row.size, sizeRect, fonts.normal, mutedColor, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        right -= sizeW + style.Px(8);
        RECT pagesRect{right - pagesW, r.top, right, r.bottom};
        gfx::Text(dc, row.pages, pagesRect, fonts.normal, mutedColor, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        right -= pagesW + style.Px(12);
    }

    // Título y subtítulo.
    RECT titleRect{x, r.top + style.Px(8), right, cy + style.Px(1)};
    gfx::Text(dc, row.title, titleRect, fonts.semibold, textColor,
              DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    std::wstring subtitle = row.subtitle;
    if (compact) subtitle = row.pages + L" · " + row.size + (subtitle.empty() ? L"" : L" · " + subtitle);
    RECT subRect{x, cy + style.Px(3), right, r.bottom - style.Px(6)};
    gfx::Text(dc, subtitle, subRect, fonts.small, mutedColor,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void JobListView::PaintEmpty(HDC dc, const RECT& client) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    int cy = (client.top + client.bottom) / 2;
    if (emptyGlyph_) {
        RECT iconRect{client.left, cy - style.Px(70), client.right, cy - style.Px(18)};
        gfx::Glyph(dc, emptyGlyph_, iconRect, style.fonts().iconLarge, Blend(pal.textMuted, pal.surface, 0.35));
    }
    RECT titleRect{client.left + style.Px(20), cy - style.Px(8), client.right - style.Px(20), cy + style.Px(16)};
    gfx::Text(dc, emptyTitle_, titleRect, style.fonts().semibold, pal.text,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    RECT hintRect{client.left + style.Px(20), cy + style.Px(18), client.right - style.Px(20), cy + style.Px(60)};
    gfx::Text(dc, emptyHint_, hintRect, style.fonts().small, pal.textMuted, DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
}

LRESULT JobListView::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            gfx::BufferedPaint paint(hwnd_);
            Paint(paint.dc(), paint.rect());
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            UpdateScrollBar();
            Invalidate();
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS: Invalidate(); return 0;
        case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_VSCROLL: {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd_, SB_VERT, &si);
            int y = scrollY_;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: y -= RowHeight(); break;
                case SB_LINEDOWN: y += RowHeight(); break;
                case SB_PAGEUP: y -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN: y += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: y = si.nTrackPos; break;
                case SB_TOP: y = 0; break;
                case SB_BOTTOM: y = ContentHeight(); break;
            }
            ScrollTo(y);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            ScrollTo(scrollY_ - MulDiv(delta, RowHeight() * 3, WHEEL_DELTA));
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!trackingMouse_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                trackingMouse_ = true;
            }
            int row = RowAt(GET_Y_LPARAM(lParam));
            if (row != hover_) {
                hover_ = row;
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            if (hover_ != -1) {
                hover_ = -1;
                Invalidate();
            }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN: {
            SetFocus(hwnd_);
            int row = RowAt(GET_Y_LPARAM(lParam));
            if (row >= 0) {
                bool ctrl = (wParam & MK_CONTROL) != 0;
                bool shift = (wParam & MK_SHIFT) != 0;
                // Clic derecho sobre una fila ya seleccionada conserva la selección múltiple.
                if (msg == WM_RBUTTONDOWN && selected_.count(rows_[static_cast<size_t>(row)].key)) {
                    focus_ = row;
                    Invalidate();
                } else {
                    SetFocusRow(row, shift, ctrl);
                }
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK:
            if (RowAt(GET_Y_LPARAM(lParam)) >= 0 && callbacks_.activate) callbacks_.activate();
            return 0;
        case WM_CONTEXTMENU: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (pt.x == -1 && pt.y == -1) {  // desde el teclado (tecla Menú / Shift+F10)
                RECT client;
                GetClientRect(hwnd_, &client);
                pt = {client.left + Style::Get().Px(40),
                      std::max(0, (focus_ + 1) * RowHeight() - scrollY_)};
                ClientToScreen(hwnd_, &pt);
            }
            if (callbacks_.contextMenu) callbacks_.contextMenu(pt);
            return 0;
        }
    }
    return Window::HandleMessage(msg, wParam, lParam);
}

}  // namespace cola::ui
