#include "ui/JobListView.h"

#include <windowsx.h>

#include <algorithm>
#include <climits>

#include "ui/DarkMode.h"
#include "ui/Gfx.h"
#include "ui/Style.h"

namespace cola::ui {

namespace {
constexpr wchar_t kGlyphSortUp = 0xE70E;   // ChevronUp
constexpr wchar_t kGlyphSortDown = 0xE70D; // ChevronDown
}  // namespace

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

int JobListView::HeaderHeight() const { return columns_.empty() ? 0 : Style::Get().Px(30); }
int JobListView::RowHeight() const { return Style::Get().Px(40); }

int JobListView::ViewHeight() const {
    RECT client;
    GetClientRect(hwnd_, &client);
    return std::max(0, gfx::Height(client) - HeaderHeight());
}

int JobListView::RowAt(int clientY) const {
    if (rows_.empty() || clientY < HeaderHeight()) return -1;
    int row = (clientY - HeaderHeight() + scrollY_) / RowHeight();
    return (row >= 0 && row < RowCount()) ? row : -1;
}

void JobListView::SetColumns(std::vector<ListColumn> columns) {
    // Mismas columnas (cambia en cada refresco de la cola): no repintar de más.
    bool same = columns.size() == columns_.size();
    for (size_t i = 0; same && i < columns.size(); ++i)
        same = columns[i].label == columns_[i].label && columns[i].sortId == columns_[i].sortId;
    columns_ = std::move(columns);
    if (!same) {
        hoverHeader_ = -1;
        UpdateScrollBar();
        Invalidate();
    }
}

void JobListView::SetSortIndicator(int sortId, bool descending) {
    if (sortId == sortId_ && descending == sortDescending_) return;
    sortId_ = sortId;
    sortDescending_ = descending;
    Invalidate();
}

std::vector<JobListView::Slot> JobListView::Layout(int width) const {
    const auto& style = Style::Get();
    const int gap = style.Px(12);
    const int left = style.Px(12) + style.Px(32);  // margen + ícono de estado
    const int right = width - style.Px(16);
    const int flexMin = style.Px(140);

    std::vector<bool> visible(columns_.size(), true);
    auto required = [&] {
        int total = 0;
        for (size_t i = 0; i < columns_.size(); ++i)
            if (visible[i]) total += (columns_[i].width ? style.Px(columns_[i].width) : flexMin) + gap;
        return total;
    };
    // Oculta primero las columnas de mayor prioridad de ocultamiento hasta que quepa.
    while (required() > right - left) {
        int victim = -1;
        for (size_t i = 0; i < columns_.size(); ++i)
            if (visible[i] && columns_[i].hidePriority > 0 &&
                (victim < 0 || columns_[i].hidePriority > columns_[static_cast<size_t>(victim)].hidePriority))
                victim = static_cast<int>(i);
        if (victim < 0) break;
        visible[static_cast<size_t>(victim)] = false;
    }
    int fixed = 0;
    int flexCount = 0;
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (!visible[i]) continue;
        if (columns_[i].width) fixed += style.Px(columns_[i].width) + gap;
        else {
            fixed += gap;
            ++flexCount;
        }
    }
    const int flexWidth = flexCount ? std::max(flexMin, (right - left - fixed) / flexCount) : 0;

    std::vector<Slot> slots;
    int x = left;
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (!visible[i]) continue;
        const int w = columns_[i].width ? style.Px(columns_[i].width) : flexWidth;
        slots.push_back({i, x, x + w});
        x += w + gap;
    }
    return slots;
}

int JobListView::HeaderColumnAt(POINT pt) const {
    if (pt.y < 0 || pt.y >= HeaderHeight()) return -1;
    RECT client;
    GetClientRect(hwnd_, &client);
    const int half = Style::Get().Px(6);  // la mitad del espacio entre columnas también cuenta
    for (const auto& slot : Layout(gfx::Width(client)))
        if (pt.x >= slot.left - half && pt.x < slot.right + half) return static_cast<int>(slot.column);
    return -1;
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

void JobListView::ClearSelection() {
    if (selected_.empty()) return;
    selected_.clear();
    Invalidate();
    NotifySelection();
}

RECT JobListView::BandRectClient() const {
    const int offset = HeaderHeight() - scrollY_;
    RECT r{std::min(bandStart_.x, bandEnd_.x), std::min(bandStart_.y, bandEnd_.y) + offset,
           std::max(bandStart_.x, bandEnd_.x), std::max(bandStart_.y, bandEnd_.y) + offset};
    return r;
}

void JobListView::UpdateBandSelection() {
    const int top = std::min(bandStart_.y, bandEnd_.y);
    const int bottom = std::max(bandStart_.y, bandEnd_.y);
    selected_ = bandBase_;
    const int height = RowHeight();
    for (int i = 0; i < RowCount(); ++i) {
        const int rowTop = i * height;
        if (rowTop + height > top && rowTop < bottom) selected_.insert(rows_[static_cast<size_t>(i)].key);
    }
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
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = std::max(0, ContentHeight() - 1);
    si.nPage = static_cast<UINT>(std::max(1, ViewHeight()));
    int maxScroll = std::max(0, ContentHeight() - ViewHeight());
    scrollY_ = std::clamp(scrollY_, 0, maxScroll);
    si.nPos = scrollY_;
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
    ShowScrollBar(hwnd_, SB_VERT, ContentHeight() > ViewHeight());
}

void JobListView::ScrollTo(int y) {
    int maxScroll = std::max(0, ContentHeight() - ViewHeight());
    y = std::clamp(y, 0, maxScroll);
    if (y == scrollY_) return;
    scrollY_ = y;
    SetScrollPos(hwnd_, SB_VERT, scrollY_, TRUE);
    Invalidate();
}

void JobListView::EnsureVisible(int row) {
    int top = row * RowHeight();
    int bottom = top + RowHeight();
    if (top < scrollY_) ScrollTo(top);
    else if (bottom > scrollY_ + ViewHeight()) ScrollTo(bottom - ViewHeight());
}

void JobListView::Paint(HDC dc, const RECT& client) {
    const auto& pal = Style::Get().palette();
    gfx::Fill(dc, client, pal.surface);
    const int header = HeaderHeight();
    if (rows_.empty()) {
        RECT body{client.left, client.top + header, client.right, client.bottom};
        PaintEmpty(dc, body);
    } else {
        const auto slots = Layout(gfx::Width(client));
        int saved = SaveDC(dc);
        IntersectClipRect(dc, client.left, client.top + header, client.right, client.bottom);
        int first = scrollY_ / RowHeight();
        for (int i = first; i < RowCount(); ++i) {
            int top = header + i * RowHeight() - scrollY_;
            if (top > client.bottom) break;
            RECT r{client.left, top, client.right, top + RowHeight()};
            PaintRow(dc, i, r, slots);
        }
        if (bandActive_) {
            RECT band = BandRectClient();
            const int radius = Style::Get().Px(3);
            gfx::FillRoundedAlpha(dc, band, radius, pal.accent, pal.dark ? 60 : 45);
            gfx::StrokeRounded(dc, band, radius, pal.accent);
        }
        RestoreDC(dc, saved);
    }
    if (header) PaintHeader(dc, client);
}

void JobListView::PaintHeader(HDC dc, const RECT& client) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const auto& fonts = style.fonts();
    RECT bar{client.left, client.top, client.right, client.top + HeaderHeight()};
    gfx::Fill(dc, bar, pal.surface);
    gfx::HLine(dc, bar.left, bar.right, bar.bottom - 1, pal.border);
    for (const auto& slot : Layout(gfx::Width(client))) {
        const ListColumn& column = columns_[slot.column];
        const bool sorted = column.sortId >= 0 && column.sortId == sortId_;
        const bool hovered = column.sortId >= 0 && static_cast<int>(slot.column) == hoverHeader_;
        if (hovered) {
            RECT h{slot.left - style.Px(6), bar.top + style.Px(3), slot.right + style.Px(6), bar.bottom - style.Px(3)};
            gfx::FillRounded(dc, h, style.Px(4), pal.hover);
        }
        const COLORREF color = sorted ? pal.text : pal.textMuted;
        const int arrow = sorted ? style.Px(14) : 0;
        RECT text{slot.left, bar.top, slot.right - arrow, bar.bottom};
        if (column.alignRight) text = {slot.left, bar.top, slot.right - arrow, bar.bottom};
        gfx::Text(dc, column.label, text, sorted ? fonts.semibold : fonts.small, color,
                  (column.alignRight ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (sorted) {
            // Flecha junto al texto (a su derecha).
            SIZE size = gfx::MeasureText(dc, column.label, fonts.semibold);
            int ax = column.alignRight ? slot.right - arrow : std::min<int>(slot.left + size.cx + style.Px(2), slot.right - arrow);
            RECT a{ax, bar.top, ax + arrow, bar.bottom};
            gfx::Glyph(dc, sortDescending_ ? kGlyphSortDown : kGlyphSortUp, a, fonts.iconSmall, pal.accent);
        }
    }
}

void JobListView::PaintRow(HDC dc, int index, const RECT& r, const std::vector<Slot>& slots) {
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

    for (const auto& slot : slots) {
        const ListColumn& column = columns_[slot.column];
        if (slot.column >= row.cells.size()) continue;
        const std::wstring& cell = row.cells[slot.column];
        if (cell.empty()) continue;
        if (column.pill) {
            SIZE size = gfx::MeasureText(dc, cell, fonts.small);
            int w = std::min<int>(size.cx + style.Px(20), slot.right - slot.left);
            RECT pill{slot.left, cy - style.Px(11), slot.left + w, cy + style.Px(11)};
            gfx::FillRounded(dc, pill, style.Px(11), selected ? pal.surface : pal.ToneBackground(row.tone));
            gfx::Text(dc, cell, gfx::Inset(pill, style.Px(6), 0), fonts.small, toneColor,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            continue;
        }
        RECT cellRect{slot.left, r.top, slot.right, r.bottom};
        gfx::Text(dc, cell, cellRect, column.emphasis ? fonts.semibold : fonts.normal,
                  column.emphasis ? textColor : mutedColor,
                  (column.alignRight ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
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
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (bandPressed_) {
                if (!bandActive_) {
                    const int threshold = Style::Get().Px(5);
                    if (std::abs(pt.x - bandStart_.x) > threshold ||
                        std::abs(pt.y - HeaderHeight() + scrollY_ - bandStart_.y) > threshold)
                        bandActive_ = true;
                }
                if (bandActive_) {
                    RECT client;
                    GetClientRect(hwnd_, &client);
                    // Desplaza la lista si se arrastra más allá del borde.
                    if (pt.y < HeaderHeight()) ScrollTo(scrollY_ - RowHeight());
                    else if (pt.y > client.bottom) ScrollTo(scrollY_ + RowHeight());
                    bandEnd_.x = pt.x;
                    bandEnd_.y = std::clamp<LONG>(pt.y, HeaderHeight(), client.bottom) - HeaderHeight() + scrollY_;
                    UpdateBandSelection();
                }
                return 0;
            }
            if (!trackingMouse_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                trackingMouse_ = true;
            }
            int row = RowAt(pt.y);
            int column = HeaderColumnAt(pt);
            if (column >= 0 && columns_[static_cast<size_t>(column)].sortId < 0) column = -1;
            if (row != hover_ || column != hoverHeader_) {
                hover_ = row;
                hoverHeader_ = column;
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            if (hover_ != -1 || hoverHeader_ != -1) {
                hover_ = -1;
                hoverHeader_ = -1;
                Invalidate();
            }
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && hoverHeader_ >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN: {
            SetFocus(hwnd_);
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (pt.y < HeaderHeight()) {
                // Clic en el encabezado: ordenar por esa columna.
                int column = HeaderColumnAt(pt);
                if (msg == WM_LBUTTONDOWN && column >= 0 && columns_[static_cast<size_t>(column)].sortId >= 0 &&
                    callbacks_.sortClicked)
                    callbacks_.sortClicked(columns_[static_cast<size_t>(column)].sortId);
                return 0;
            }
            const bool ctrl = (wParam & MK_CONTROL) != 0;
            const bool shift = (wParam & MK_SHIFT) != 0;
            int row = RowAt(pt.y);
            if (row >= 0) {
                // Clic derecho sobre una fila ya seleccionada conserva la selección múltiple.
                if (msg == WM_RBUTTONDOWN && selected_.count(rows_[static_cast<size_t>(row)].key)) {
                    focus_ = row;
                    Invalidate();
                } else {
                    SetFocusRow(row, shift, ctrl);
                }
            } else if (msg == WM_LBUTTONDOWN) {
                ClearSelection();  // clic en el vacío: sin nada seleccionado
            }
            if (msg == WM_LBUTTONDOWN) {
                // Prepara el arrastre de selección (se activa al mover el mouse).
                bandPressed_ = true;
                bandStart_ = {pt.x, pt.y - HeaderHeight() + scrollY_};
                bandEnd_ = bandStart_;
                bandBase_ = ctrl ? selected_ : std::set<std::wstring>{};
                SetCapture(hwnd_);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (bandPressed_) {
                bandPressed_ = false;
                bandActive_ = false;
                bandBase_.clear();
                ReleaseCapture();
                Invalidate();
            }
            return 0;
        case WM_CAPTURECHANGED:
            bandPressed_ = bandActive_ = false;
            Invalidate();
            return 0;
        case WM_LBUTTONDBLCLK:
            if (RowAt(GET_Y_LPARAM(lParam)) >= 0 && callbacks_.activate) callbacks_.activate();
            return 0;
        case WM_CONTEXTMENU: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (pt.x == -1 && pt.y == -1) {  // desde el teclado (tecla Menú / Shift+F10)
                RECT client;
                GetClientRect(hwnd_, &client);
                pt = {client.left + Style::Get().Px(40),
                      std::max(HeaderHeight(), HeaderHeight() + (focus_ + 1) * RowHeight() - scrollY_)};
                ClientToScreen(hwnd_, &pt);
            }
            if (callbacks_.contextMenu) callbacks_.contextMenu(pt);
            return 0;
        }
    }
    return Window::HandleMessage(msg, wParam, lParam);
}

}  // namespace cola::ui
