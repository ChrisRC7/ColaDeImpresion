#include "ui/JobListView.h"

#include <windowsx.h>

#include <algorithm>
#include <climits>
#include <cstdlib>

#include "config/Settings.h"

#include "ui/DarkMode.h"
#include "ui/Gfx.h"
#include "ui/Style.h"

namespace cola::ui {

bool JobListView::Create(HWND parent, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    if (!CreateWin(L"ColaJobList", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | WS_CLIPSIBLINGS, 0, 0, 0, 0,
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
    display_ = ColumnDisplayOrder(columns_, order_);
    if (!same) {
        hoverHeader_ = -1;
        if (headerPressed_ >= 0) FinishColumnDrag(false);  // cambió la vista a mitad del arrastre
        UpdateScrollBar();
        Invalidate();
    }
}

void JobListView::SetColumnOrder(std::vector<std::wstring> order) {
    if (order == order_) return;
    order_ = std::move(order);
    display_ = ColumnDisplayOrder(columns_, order_);
    UpdateScrollBar();
    Invalidate();
}

void JobListView::SetSortIndicator(int sortId, bool descending) {
    if (sortId == sortId_ && descending == sortDescending_) return;
    sortId_ = sortId;
    sortDescending_ = descending;
    Invalidate();
}

int JobListView::ClientWidth() const {
    RECT client;
    GetClientRect(hwnd_, &client);
    return gfx::Width(client);
}

int JobListView::ContentWidth(int width) const {
    auto slots = LayoutUnscrolled(width);
    return slots.empty() ? 0 : slots.back().right + Style::Get().Px(16);
}

std::vector<JobListView::Slot> JobListView::LayoutUnscrolled(int width) const {
    // Se distribuyen en el orden de presentación; slot.column sigue siendo el
    // índice en columns_ (y en las celdas de las filas).
    std::vector<ListColumn> ordered;
    ordered.reserve(display_.size());
    for (size_t index : display_) ordered.push_back(columns_[index]);
    auto slots = LayoutColumns(ordered, widths_, width);
    for (auto& slot : slots) slot.column = display_[slot.column];
    return slots;
}

std::vector<JobListView::Slot> JobListView::Layout(int width) const {
    auto slots = LayoutUnscrolled(width);
    for (auto& slot : slots) {
        slot.left -= scrollX_;
        slot.right -= scrollX_;
    }
    return slots;
}

void JobListView::SetColumnWidths(std::map<std::wstring, int> widths) {
    if (widths == widths_) return;
    widths_ = std::move(widths);
    UpdateScrollBar();
    Invalidate();
}

void JobListView::ScrollToX(int x) {
    const int maxScroll = std::max(0, ContentWidth(ClientWidth()) - ClientWidth());
    x = std::clamp(x, 0, maxScroll);
    if (x == scrollX_) return;
    scrollX_ = x;
    SetScrollPos(hwnd_, SB_HORZ, scrollX_, TRUE);
    Invalidate();
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

    // Horizontal: solo si las columnas no caben (el operador las agrandó).
    const int width = ClientWidth();
    const int content = ContentWidth(width);
    SCROLLINFO sh{};
    sh.cbSize = sizeof(sh);
    sh.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    sh.nMin = 0;
    sh.nMax = std::max(0, content - 1);
    sh.nPage = static_cast<UINT>(std::max(1, width));
    scrollX_ = std::clamp(scrollX_, 0, std::max(0, content - width));
    sh.nPos = scrollX_;
    SetScrollInfo(hwnd_, SB_HORZ, &sh, TRUE);
    ShowScrollBar(hwnd_, SB_HORZ, content > width);
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
    if (dragging_) PaintColumnDrag(dc, client);
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
        gfx::HLine(dc, r.left + pad, r.right - pad, r.bottom - 1, pal.border);

    const COLORREF textColor = selected ? pal.selectionText : pal.text;
    const COLORREF mutedColor = selected ? Blend(pal.selectionText, pal.selection, 0.3) : pal.textMuted;
    const COLORREF toneColor = pal.ToneColor(row.tone);
    const int cy = (r.top + r.bottom) / 2;

    for (const auto& slot : slots) {
        const ListColumn& column = columns_[slot.column];
        if (slot.column >= row.cells.size()) continue;
        const std::wstring& cell = row.cells[slot.column];
        if (cell.empty()) continue;
        if (column.pill) {
            SIZE size = gfx::MeasureText(dc, cell, fonts.small);
            int w = std::min<int>(size.cx + style.Px(20), slot.right - slot.left);
            int left = slot.left;
            if (column.align == Align::Center) left = (slot.left + slot.right - w) / 2;
            else if (column.align == Align::Right) left = slot.right - w;
            RECT pill{left, cy - style.Px(11), left + w, cy + style.Px(11)};
            gfx::FillRounded(dc, pill, style.Px(11), selected ? pal.surface : pal.ToneBackground(row.tone));
            gfx::Text(dc, cell, gfx::Inset(pill, style.Px(6), 0), fonts.small, toneColor,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            continue;
        }
        RECT cellRect{slot.left, r.top, slot.right, r.bottom};
        gfx::Text(dc, cell, cellRect, column.emphasis ? fonts.semibold : fonts.normal,
                  column.emphasis ? textColor : mutedColor,
                  (column.align == Align::Right ? DT_RIGHT : column.align == Align::Center ? DT_CENTER : DT_LEFT) |
                      DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
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
        case WM_HSCROLL: {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd_, SB_HORZ, &si);
            const int step = Style::Get().Px(40);
            int x = scrollX_;
            switch (LOWORD(wParam)) {
                case SB_LINELEFT: x -= step; break;
                case SB_LINERIGHT: x += step; break;
                case SB_PAGELEFT: x -= static_cast<int>(si.nPage); break;
                case SB_PAGERIGHT: x += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: x = si.nTrackPos; break;
                case SB_LEFT: x = 0; break;
                case SB_RIGHT: x = INT_MAX / 2; break;
            }
            ScrollToX(x);
            return 0;
        }
        case WM_MOUSEHWHEEL:
            ScrollToX(scrollX_ + MulDiv(GET_WHEEL_DELTA_WPARAM(wParam), Style::Get().Px(40), WHEEL_DELTA));
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            // Shift + rueda desplaza horizontalmente (si las columnas no caben).
            if ((GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) && ContentWidth(ClientWidth()) > ClientWidth())
                ScrollToX(scrollX_ - MulDiv(delta, Style::Get().Px(40) * 3, WHEEL_DELTA));
            else
                ScrollTo(scrollY_ - MulDiv(delta, RowHeight() * 3, WHEEL_DELTA));
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (resizing_ >= 0) {
                // Arrastrando el borde de una columna: el ancho sigue al mouse.
                const auto& style = Style::Get();
                const int px = std::max(style.Px(config::kMinColumnWidth),
                                        resizeStartWidth_ + static_cast<int>(pt.x) - resizeStartX_);
                const int dip = std::clamp(MulDiv(px, 96, static_cast<int>(style.dpi())), config::kMinColumnWidth,
                                           config::kMaxColumnWidth);
                const std::wstring& id = columns_[static_cast<size_t>(resizing_)].id;
                if (auto it = widths_.find(id); it == widths_.end() || it->second != dip) {
                    widths_[id] = dip;
                    UpdateScrollBar();
                    Invalidate();
                }
                return 0;
            }
            if (headerPressed_ >= 0) {
                const auto& style = Style::Get();
                if (!dragging_ && std::abs(pt.x - dragStartX_) > style.Px(5) &&
                    !columns_[static_cast<size_t>(headerPressed_)].id.empty())
                    dragging_ = true;
                if (dragging_) {
                    dragX_ = pt.x;
                    // Cerca de los bordes desplaza la tabla para llegar a las columnas ocultas.
                    const int edge = style.Px(24);
                    if (pt.x < edge) ScrollToX(scrollX_ - style.Px(20));
                    else if (pt.x > ClientWidth() - edge) ScrollToX(scrollX_ + style.Px(20));
                    Invalidate();
                }
                return 0;
            }
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
            int divider = DividerAt(pt);
            if (divider >= 0 && columns_[static_cast<size_t>(divider)].id.empty()) divider = -1;
            int column = divider >= 0 ? -1 : HeaderColumnAt(pt);
            if (column >= 0 && columns_[static_cast<size_t>(column)].sortId < 0) column = -1;
            if (row != hover_ || column != hoverHeader_ || divider != hoverDivider_) {
                hover_ = row;
                hoverHeader_ = column;
                hoverDivider_ = divider;
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            if (hover_ != -1 || hoverHeader_ != -1 || hoverDivider_ != -1) {
                hover_ = -1;
                hoverHeader_ = -1;
                hoverDivider_ = -1;
                Invalidate();
            }
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && (hoverDivider_ >= 0 || resizing_ >= 0)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
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
                // Clic en el borde de una columna: empezar a cambiar su ancho.
                const int divider = DividerAt(pt);
                if (msg == WM_LBUTTONDOWN && divider >= 0 && !columns_[static_cast<size_t>(divider)].id.empty()) {
                    for (const auto& slot : Layout(ClientWidth()))
                        if (static_cast<int>(slot.column) == divider) resizeStartWidth_ = slot.right - slot.left;
                    resizing_ = divider;
                    resizeStartX_ = pt.x;
                    SetCapture(hwnd_);
                    Invalidate();
                    return 0;
                }
                if (divider >= 0) return 0;
                // Título de una columna: al soltar sin mover se ordena por ella;
                // si se arrastra, se mueve la columna (como en la cola de Windows).
                int column = HeaderColumnAt(pt);
                if (msg == WM_LBUTTONDOWN && column >= 0) {
                    headerPressed_ = column;
                    dragging_ = false;
                    dragStartX_ = dragX_ = pt.x;
                    for (const auto& slot : Layout(ClientWidth()))
                        if (static_cast<int>(slot.column) == column) dragGrabOffset_ = pt.x - slot.left;
                    SetCapture(hwnd_);
                }
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
            if (headerPressed_ >= 0) {
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const int column = headerPressed_;
                const bool click = !dragging_;
                FinishColumnDrag(true);
                if (click && HeaderColumnAt(pt) == column && columns_[static_cast<size_t>(column)].sortId >= 0 &&
                    callbacks_.sortClicked)
                    callbacks_.sortClicked(columns_[static_cast<size_t>(column)].sortId);
                return 0;
            }
            if (resizing_ >= 0) {
                const std::wstring id = columns_[static_cast<size_t>(resizing_)].id;
                resizing_ = -1;
                ReleaseCapture();
                Invalidate();
                // Sin mover el mouse no cambió nada (y la columna sigue con su ancho predeterminado).
                if (auto it = widths_.find(id); it != widths_.end() && callbacks_.columnResized)
                    callbacks_.columnResized(id, it->second);
                return 0;
            }
            if (bandPressed_) {
                bandPressed_ = false;
                bandActive_ = false;
                bandBase_.clear();
                ReleaseCapture();
                Invalidate();
            }
            return 0;
        case WM_CAPTURECHANGED:
            if (headerPressed_ >= 0) FinishColumnDrag(false);  // se perdió el mouse: no se mueve nada
            if (resizing_ >= 0) {
                // Se perdió el mouse a mitad del arrastre: se guarda lo que haya quedado.
                const std::wstring id = columns_[static_cast<size_t>(resizing_)].id;
                resizing_ = -1;
                if (auto it = widths_.find(id); it != widths_.end() && callbacks_.columnResized)
                    callbacks_.columnResized(id, it->second);
            }
            bandPressed_ = bandActive_ = false;
            Invalidate();
            return 0;
        case WM_LBUTTONDBLCLK: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            // Doble clic en el borde: ajustar el ancho al contenido (como en Windows).
            const int divider = DividerAt(pt);
            if (divider >= 0 && !columns_[static_cast<size_t>(divider)].id.empty()) {
                const std::wstring& id = columns_[static_cast<size_t>(divider)].id;
                widths_[id] = FitWidth(static_cast<size_t>(divider));
                UpdateScrollBar();
                Invalidate();
                if (callbacks_.columnResized) callbacks_.columnResized(id, widths_[id]);
                return 0;
            }
            if (RowAt(pt.y) >= 0 && callbacks_.activate) callbacks_.activate();
            return 0;
        }
        case WM_CONTEXTMENU: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (pt.x != -1 || pt.y != -1) {
                POINT local = pt;
                ScreenToClient(hwnd_, &local);
                if (local.y >= 0 && local.y < HeaderHeight()) {
                    if (callbacks_.headerMenu) callbacks_.headerMenu(pt);
                    return 0;
                }
            }
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
