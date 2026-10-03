// Encabezado de la tabla de trabajos (parte de JobListView): títulos,
// bordes que se arrastran para cambiar el ancho y arrastre de títulos para
// reordenar las columnas. Separado de JobListView.cpp solo por tamaño.

#include <algorithm>
#include <cstdlib>

#include "config/Settings.h"
#include "ui/Gfx.h"
#include "ui/JobListView.h"
#include "ui/Style.h"

namespace cola::ui {

namespace {
constexpr wchar_t kGlyphSortUp = 0xE70E;   // ChevronUp
constexpr wchar_t kGlyphSortDown = 0xE70D; // ChevronDown
}  // namespace

int JobListView::HeaderColumnAt(POINT pt) const {
    if (pt.y < 0 || pt.y >= HeaderHeight()) return -1;
    const int half = Style::Get().Px(6);  // la mitad del espacio entre columnas también cuenta
    for (const auto& slot : Layout(ClientWidth()))
        if (pt.x >= slot.left - half && pt.x < slot.right + half) return static_cast<int>(slot.column);
    return -1;
}

int JobListView::DividerAt(POINT pt) const {
    if (pt.y < 0 || pt.y >= HeaderHeight()) return -1;
    const auto& style = Style::Get();
    const int grip = style.Px(5);
    for (const auto& slot : Layout(ClientWidth())) {
        const int divider = slot.right + style.Px(6);  // mitad del espacio entre columnas
        if (std::abs(pt.x - divider) <= grip) return static_cast<int>(slot.column);
    }
    return -1;
}

int JobListView::FitWidth(size_t column) const {
    const auto& style = Style::Get();
    const auto& fonts = style.fonts();
    const ListColumn& c = columns_[column];
    HDC dc = GetDC(hwnd_);
    // Título en seminegrita y con lugar para la flecha de orden.
    int widest = gfx::MeasureText(dc, c.label, fonts.semibold).cx + style.Px(16);
    for (const auto& row : rows_) {
        if (column >= row.cells.size() || row.cells[column].empty()) continue;
        HFONT font = c.pill ? fonts.small : c.emphasis ? fonts.semibold : fonts.normal;
        int w = gfx::MeasureText(dc, row.cells[column], font).cx + (c.pill ? style.Px(20) : 0);
        widest = std::max(widest, w);
    }
    ReleaseDC(hwnd_, dc);
    const int dip = MulDiv(widest + style.Px(4), 96, static_cast<int>(style.dpi()));
    return std::clamp(dip, config::kMinColumnWidth, config::kMaxColumnWidth);
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
        // Separador (borde que se arrastra para cambiar el ancho), como en la cola de Windows.
        const int divider = slot.right + style.Px(6);
        const bool grabbing = static_cast<int>(slot.column) == (resizing_ >= 0 ? resizing_ : hoverDivider_);
        gfx::VLine(dc, divider, bar.top + style.Px(grabbing ? 4 : 8), bar.bottom - style.Px(grabbing ? 4 : 8),
                   grabbing ? pal.accent : pal.border);
        const bool sorted = column.sortId >= 0 && column.sortId == sortId_;
        const bool hovered = column.sortId >= 0 && static_cast<int>(slot.column) == hoverHeader_;
        if (hovered) {
            RECT h{slot.left - style.Px(6), bar.top + style.Px(3), slot.right + style.Px(6), bar.bottom - style.Px(3)};
            gfx::FillRounded(dc, h, style.Px(4), pal.hover);
        }
        const COLORREF color = sorted ? pal.text : pal.textMuted;
        HFONT font = sorted ? fonts.semibold : fonts.small;
        // Texto + flecha (si está ordenada) como un bloque alineado igual que los valores.
        const int arrow = sorted ? style.Px(14) : 0;
        const int textW = std::min<int>(gfx::MeasureText(dc, column.label, font).cx, slot.right - slot.left - arrow);
        int textLeft = slot.left;
        if (column.align == Align::Right) textLeft = slot.right - arrow - textW;
        else if (column.align == Align::Center) textLeft = (slot.left + slot.right - textW - arrow) / 2;
        textLeft = std::max(textLeft, static_cast<int>(slot.left));
        RECT text{textLeft, bar.top, textLeft + textW, bar.bottom};
        gfx::Text(dc, column.label, text, font, color,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (sorted) {
            RECT a{text.right + style.Px(1), bar.top, text.right + style.Px(1) + arrow, bar.bottom};
            gfx::Glyph(dc, sortDescending_ ? kGlyphSortDown : kGlyphSortUp, a, fonts.iconSmall, pal.accent);
        }
    }
}

bool JobListView::DropTarget(int x, size_t& anchor, bool& after, int& lineX) const {
    const auto slots = Layout(ClientWidth());
    if (slots.empty()) return false;
    const int half = Style::Get().Px(6);  // mitad del espacio entre columnas
    for (const auto& slot : slots) {
        if (x < (slot.left + slot.right) / 2) {
            anchor = slot.column;
            after = false;
            lineX = slot.left - half;
            return true;
        }
    }
    anchor = slots.back().column;
    after = true;
    lineX = slots.back().right + half;
    return true;
}

void JobListView::FinishColumnDrag(bool apply) {
    const int column = headerPressed_;
    const bool moved = dragging_;
    headerPressed_ = -1;  // antes de soltar la captura (WM_CAPTURECHANGED no debe repetir esto)
    dragging_ = false;
    if (GetCapture() == hwnd_) ReleaseCapture();
    Invalidate();
    if (!apply || !moved || column < 0) return;
    size_t anchor = 0;
    bool after = false;
    int lineX = 0;
    if (!DropTarget(dragX_, anchor, after, lineX)) return;
    auto display = MoveDisplayColumn(display_, static_cast<size_t>(column), anchor, after);
    if (display == display_) return;
    display_ = std::move(display);
    UpdateScrollBar();
    std::vector<std::wstring> ids;
    for (size_t index : display_)
        if (!columns_[index].id.empty()) ids.push_back(columns_[index].id);
    if (callbacks_.columnMoved) callbacks_.columnMoved(ids);
}

void JobListView::PaintColumnDrag(HDC dc, const RECT& client) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const size_t column = static_cast<size_t>(headerPressed_);
    const int header = HeaderHeight();
    int width = style.Px(80);
    for (const auto& slot : Layout(gfx::Width(client))) {
        if (slot.column != column) continue;
        width = slot.right - slot.left;
        // El lugar de donde sale la columna queda marcado.
        RECT origin{slot.left - style.Px(6), client.top + style.Px(3), slot.right + style.Px(6),
                    client.top + header - style.Px(3)};
        gfx::FillRounded(dc, origin, style.Px(4), pal.hover);
    }
    // Línea donde va a quedar (si de verdad cambia de lugar).
    size_t anchor = 0;
    bool after = false;
    int lineX = 0;
    if (DropTarget(dragX_, anchor, after, lineX) && MoveDisplayColumn(display_, column, anchor, after) != display_) {
        RECT line{lineX - style.Px(1), client.top, lineX + style.Px(1), client.bottom};
        gfx::Fill(dc, line, pal.accent);
    }
    // El título sigue al mouse.
    const int left = dragX_ - dragGrabOffset_;
    RECT ghost{left - style.Px(6), client.top + style.Px(3), left + width + style.Px(6),
               client.top + header - style.Px(3)};
    gfx::FillRoundedAlpha(dc, ghost, style.Px(4), pal.accent, pal.dark ? 70 : 50);
    gfx::StrokeRounded(dc, ghost, style.Px(4), pal.accent);
    gfx::Text(dc, columns_[column].label, gfx::Inset(ghost, style.Px(6), 0), style.fonts().semibold, pal.text,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

}  // namespace cola::ui
