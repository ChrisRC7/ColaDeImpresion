// Tabla de trabajos (ui::JobListView): filas, selección (clic, Ctrl, Shift,
// teclado y rectángulo de arrastre), desplazamiento vertical y horizontal,
// estado vacío y el despacho de los mensajes del mouse, incluidos los del
// encabezado (cambio de ancho y arrastre de columnas, cuyo dibujo y cálculos
// están en JobListHeader.cpp).

#include "ui/JobListView.h"

// GET_X_LPARAM / GET_Y_LPARAM: coordenadas con signo (con varios monitores o
// durante una captura del mouse pueden ser negativas; LOWORD las rompería).
#include <windowsx.h>

#include <algorithm>
#include <climits>
#include <cstdlib>

// config::kMinColumnWidth / kMaxColumnWidth (límites del ancho de columna en DIP).
#include "config/Settings.h"

#include "ui/DarkMode.h"
#include "ui/Gfx.h"
#include "ui/Style.h"

namespace cola::ui {

bool JobListView::Create(HWND parent, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    // Barras de desplazamiento nativas (WS_VSCROLL/WS_HSCROLL): se ocultan con
    // ShowScrollBar cuando no hacen falta y se oscurecen con el tema (D9).
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
    // De coordenadas cliente a contenido (sin encabezado, con el desplazamiento)
    // y de ahí a índice de fila; todas las filas tienen el mismo alto.
    int row = (clientY - HeaderHeight() + scrollY_) / RowHeight();
    return (row >= 0 && row < RowCount()) ? row : -1;
}

void JobListView::SetColumns(std::vector<ListColumn> columns) {
    // Mismas columnas (cambia en cada refresco de la cola): no repintar de más.
    // Se comparan título y sortId: bastan para saber si cambió la vista o el idioma.
    bool same = columns.size() == columns_.size();
    for (size_t i = 0; same && i < columns.size(); ++i)
        same = columns[i].label == columns_[i].label && columns[i].sortId == columns_[i].sortId;
    columns_ = std::move(columns);
    // El orden de presentación se recalcula siempre: la cantidad de columnas
    // pudo cambiar (p. ej. Impresora aparece solo en "Todas").
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
    // El ancho total no cambia, pero sí qué queda dentro de la parte visible.
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
    // Borde derecho de la última columna más el mismo margen derecho de 16 DIP
    // que usa LayoutColumns: así al desplazarse hasta el final queda aire.
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
    // LayoutColumns devuelve índices dentro de `ordered`; se traducen de vuelta
    // a índices de columns_ (que es como se indexan las celdas de cada fila).
    for (auto& slot : slots) slot.column = display_[slot.column];
    return slots;
}

std::vector<JobListView::Slot> JobListView::Layout(int width) const {
    auto slots = LayoutUnscrolled(width);
    // Se resta el desplazamiento horizontal: las columnas de la izquierda
    // quedan con x negativas (fuera de la vista) y no se ven.
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
    // Máximo: lo que sobresale el contenido del ancho visible (0 si cabe todo).
    const int maxScroll = std::max(0, ContentWidth(ClientWidth()) - ClientWidth());
    x = std::clamp(x, 0, maxScroll);
    if (x == scrollX_) return;
    scrollX_ = x;
    SetScrollPos(hwnd_, SB_HORZ, scrollX_, TRUE);
    Invalidate();
}

void JobListView::SetRows(std::vector<ListRow> rows) {
    // Se recuerda la clave enfocada antes de reemplazar: la misma fila puede
    // cambiar de índice (otro trabajo entró arriba o cambió el orden).
    std::wstring focusKey = FocusedKey();
    rows_ = std::move(rows);

    // Limpia de la selección las claves que ya no existen.
    std::set<std::wstring> alive;
    for (const auto& r : rows_) alive.insert(r.key);
    for (auto it = selected_.begin(); it != selected_.end();)
        it = alive.count(*it) ? std::next(it) : selected_.erase(it);

    // Busca el índice nuevo de la fila enfocada.
    int oldFocus = focus_;
    focus_ = -1;
    for (int i = 0; i < RowCount(); ++i)
        if (rows_[static_cast<size_t>(i)].key == focusKey) focus_ = i;
    bool selectionChanged = false;
    if (focus_ < 0 && !rows_.empty()) {
        // El trabajo enfocado desapareció: enfoca el que quedó en su lugar.
        // (Así, tras cancelar o imprimir un trabajo, el operador puede seguir
        // con el siguiente sin volver a hacer clic.)
        focus_ = std::clamp(oldFocus, 0, RowCount() - 1);
        if (selected_.empty()) {
            selected_.insert(rows_[static_cast<size_t>(focus_)].key);
            selectionChanged = true;
        }
    }
    // Índices que quedaron fuera de rango con menos filas.
    if (anchor_ >= RowCount()) anchor_ = focus_;
    if (hover_ >= RowCount()) hover_ = -1;
    if (focusKey != FocusedKey()) selectionChanged = true;

    // Con otra cantidad de filas cambia el rango vertical (y la posición se
    // limita si quedó más allá del final).
    UpdateScrollBar();
    Invalidate();
    // Solo se avisa si la selección o el foco cambiaron de verdad: el monitor
    // llama a SetRows con cada instantánea y la vista previa no debe recargarse.
    // (Las claves quitadas de selected_ más arriba no se avisan aparte.)
    if (selectionChanged) NotifySelection();
}

void JobListView::SetEmptyText(std::wstring title, std::wstring hint, wchar_t glyph) {
    emptyTitle_ = std::move(title);
    emptyHint_ = std::move(hint);
    emptyGlyph_ = glyph;
    // Solo se ve con la lista vacía; si hay filas no hace falta repintar.
    if (rows_.empty()) Invalidate();
}

std::vector<std::wstring> JobListView::SelectedKeys() const {
    // Se recorre rows_ (no el set, que está ordenado por texto) para devolver
    // las claves en el orden visible: las acciones se aplican en ese orden.
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
    // De coordenadas de contenido a cliente: se suma el encabezado y se resta
    // el desplazamiento. min/max normalizan el rectángulo sin importar hacia
    // dónde se arrastró.
    const int offset = HeaderHeight() - scrollY_;
    RECT r{std::min(bandStart_.x, bandEnd_.x), std::min(bandStart_.y, bandEnd_.y) + offset,
           std::max(bandStart_.x, bandEnd_.x), std::max(bandStart_.y, bandEnd_.y) + offset};
    return r;
}

void JobListView::UpdateBandSelection() {
    // Solo importa el rango vertical: una fila entra si se cruza con la banda
    // aunque la x quede fuera de las columnas (como el Explorador en detalles).
    const int top = std::min(bandStart_.y, bandEnd_.y);
    const int bottom = std::max(bandStart_.y, bandEnd_.y);
    // Se parte de la selección previa (vacía, o la que había si se empezó con
    // Ctrl) para que achicar el rectángulo deseleccione lo que deja de cubrir.
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
    // INT_MIN/INT_MAX son valores especiales (Inicio/Fin) para no sumar y desbordar.
    if (delta == INT_MIN) target = 0;
    else if (delta == INT_MAX) target = RowCount() - 1;
    // Sin foco previo, ↓ empieza en la primera fila (-1 + 1) y ↑ en la última
    // (RowCount - 1). Las páginas (±n) se limitan a los extremos.
    else target = std::clamp((focus_ < 0 ? (delta > 0 ? -1 : RowCount()) : focus_) + delta, 0, RowCount() - 1);
    SetFocusRow(target, extend, false);
}

void JobListView::SetFocusRow(int row, bool extend, bool toggle) {
    if (row < 0 || row >= RowCount()) return;
    const std::wstring& key = rows_[static_cast<size_t>(row)].key;
    if (toggle) {
        // Ctrl + clic: alterna esta fila y la vuelve el ancla de futuros rangos.
        if (selected_.count(key)) selected_.erase(key);
        else selected_.insert(key);
        anchor_ = row;
    } else if (extend && anchor_ >= 0 && anchor_ < RowCount()) {
        // Shift: rango continuo entre el ancla y esta fila (reemplaza la
        // selección; el ancla no se mueve, como en Windows).
        selected_.clear();
        for (int i = std::min(anchor_, row); i <= std::max(anchor_, row); ++i)
            selected_.insert(rows_[static_cast<size_t>(i)].key);
    } else {
        // Clic o flecha simple: solo esta fila.
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
    // Vertical: el rango es el alto del contenido en px y la "página" el alto
    // visible, así el tamaño del cursor de la barra es proporcional.
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = std::max(0, ContentHeight() - 1);  // nMax es inclusivo
    si.nPage = static_cast<UINT>(std::max(1, ViewHeight()));
    // Si la lista se achicó (menos filas o ventana más alta), la posición se
    // corrige para no dejar espacio vacío abajo.
    int maxScroll = std::max(0, ContentHeight() - ViewHeight());
    scrollY_ = std::clamp(scrollY_, 0, maxScroll);
    si.nPos = scrollY_;
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
    // Solo se muestra si hace falta (con SIF_DISABLENOSCROLL quedaría deshabilitada a la vista).
    ShowScrollBar(hwnd_, SB_VERT, ContentHeight() > ViewHeight());

    // Horizontal: solo si las columnas no caben (el operador las agrandó).
    // (O si la ventana es angosta o el panel de detalles está abierto, D40.)
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
    // Se repinta todo (no ScrollWindowEx): con doble búfer es barato y evita
    // problemas con el encabezado fijo y la fila resaltada.
    Invalidate();
}

void JobListView::EnsureVisible(int row) {
    // Límites de la fila en coordenadas de contenido.
    int top = row * RowHeight();
    int bottom = top + RowHeight();
    // Por arriba: alinear la fila con el borde superior; por abajo: con el
    // inferior. Si ya está entera a la vista no se mueve nada.
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
        // Recorte al área de filas: una fila parcialmente desplazada bajo el
        // encabezado no debe dibujarse encima de él. SaveDC/RestoreDC deshacen
        // el recorte antes de pintar el encabezado.
        int saved = SaveDC(dc);
        IntersectClipRect(dc, client.left, client.top + header, client.right, client.bottom);
        // Solo se pintan las filas visibles: desde la primera que asoma hasta
        // la que pasa del borde inferior (con miles de filas en el historial
        // pintar todas sería lento).
        int first = scrollY_ / RowHeight();
        for (int i = first; i < RowCount(); ++i) {
            int top = header + i * RowHeight() - scrollY_;
            if (top > client.bottom) break;
            RECT r{client.left, top, client.right, top + RowHeight()};
            PaintRow(dc, i, r, slots);
        }
        // Rectángulo de selección translúcido encima de las filas (más opaco
        // en oscuro para que se distinga).
        if (bandActive_) {
            RECT band = BandRectClient();
            const int radius = Style::Get().Px(3);
            gfx::FillRoundedAlpha(dc, band, radius, pal.accent, pal.dark ? 60 : 45);
            gfx::StrokeRounded(dc, band, radius, pal.accent);
        }
        RestoreDC(dc, saved);
    }
    // El encabezado va al final: queda siempre encima (es "fijo" al desplazar).
    if (header) PaintHeader(dc, client);
    // Y sobre todo, la columna que se está arrastrando.
    if (dragging_) PaintColumnDrag(dc, client);
}

void JobListView::PaintRow(HDC dc, int index, const RECT& r, const std::vector<Slot>& slots) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const auto& fonts = style.fonts();
    const ListRow& row = rows_[static_cast<size_t>(index)];
    const bool selected = selected_.count(row.key) > 0;
    // El contorno de foco solo se ve si la tabla tiene el foco del teclado
    // (WM_SETFOCUS/WM_KILLFOCUS repintan).
    const bool focused = index == focus_ && GetFocus() == hwnd_;
    const int pad = style.Px(12);

    // "Tarjeta": la fila con margen de 6 DIP a los lados y 2 arriba/abajo, de
    // esquinas redondeadas. Prioridad del fondo: seleccionada > bajo el mouse.
    RECT card = gfx::Inset(r, style.Px(6), style.Px(2));
    if (selected) gfx::FillRounded(dc, card, style.Px(6), pal.selection);
    else if (index == hover_) gfx::FillRounded(dc, card, style.Px(6), pal.hover);
    // Foco sin selección (p. ej. tras Ctrl + clic que deseleccionó): contorno de acento.
    if (focused && !selected) gfx::StrokeRounded(dc, card, style.Px(6), pal.accent);
    // Línea divisoria inferior, salvo junto a tarjetas con fondo (se vería
    // cortada contra el redondeado): no si esta o la siguiente están
    // seleccionadas, ni bajo la fila resaltada, ni tras la última.
    if (!selected && index != hover_ && index + 1 < RowCount() && !selected_.count(rows_[static_cast<size_t>(index + 1)].key))
        gfx::HLine(dc, r.left + pad, r.right - pad, r.bottom - 1, pal.border);

    // Colores del texto: sobre la selección se usa selectionText y una
    // versión atenuada de él (mezcla con el fondo) para los secundarios.
    const COLORREF textColor = selected ? pal.selectionText : pal.text;
    const COLORREF mutedColor = selected ? Blend(pal.selectionText, pal.selection, 0.3) : pal.textMuted;
    const COLORREF toneColor = pal.ToneColor(row.tone);
    const int cy = (r.top + r.bottom) / 2;

    // Celdas en el orden de presentación (slots); slot.column indexa las celdas.
    for (const auto& slot : slots) {
        const ListColumn& column = columns_[slot.column];
        // Fila con menos celdas que columnas (no debería pasar): se omite.
        if (slot.column >= row.cells.size()) continue;
        const std::wstring& cell = row.cells[slot.column];
        if (cell.empty()) continue;
        if (column.pill) {
            // Insignia de estado: píldora de 22 DIP de alto con el fondo suave
            // del tono y el texto en el color del tono; ancho = texto + 20 DIP
            // de relleno, sin pasar del ancho de la columna.
            SIZE size = gfx::MeasureText(dc, cell, fonts.small);
            int w = std::min<int>(size.cx + style.Px(20), slot.right - slot.left);
            int left = slot.left;
            if (column.align == Align::Center) left = (slot.left + slot.right - w) / 2;
            else if (column.align == Align::Right) left = slot.right - w;
            RECT pill{left, cy - style.Px(11), left + w, cy + style.Px(11)};
            // Sobre la selección el fondo del tono no contrastaría: se usa surface.
            gfx::FillRounded(dc, pill, style.Px(11), selected ? pal.surface : pal.ToneBackground(row.tone));
            gfx::Text(dc, cell, gfx::Inset(pill, style.Px(6), 0), fonts.small, toneColor,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            continue;
        }
        // Celda de texto: la columna principal (emphasis, Documento) en
        // seminegrita y color normal; las demás atenuadas. "…" si no cabe.
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
    // Bloque centrado verticalmente: glifo arriba del centro, título en el
    // centro y explicación debajo (varias líneas si hace falta).
    int cy = (client.top + client.bottom) / 2;
    if (emptyGlyph_) {
        RECT iconRect{client.left, cy - style.Px(70), client.right, cy - style.Px(18)};
        // Glifo muy atenuado (35 % hacia el fondo): decorativo, no compite con el texto.
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
            // Todo se dibuja en el búfer y se copia al final (sin parpadeo).
            gfx::BufferedPaint paint(hwnd_);
            Paint(paint.dc(), paint.rect());
            return 0;
        }
        // Fondo pintado en WM_PAINT: no dejar que Windows lo borre antes.
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            // Otro tamaño cambia el ancho de la columna flexible, el rango de
            // las barras y cuántas filas entran.
            UpdateScrollBar();
            Invalidate();
            return 0;
        // El contorno de la fila con foco solo se dibuja con el foco del teclado.
        case WM_SETFOCUS:
        case WM_KILLFOCUS: Invalidate(); return 0;
        // Pide recibir flechas y caracteres aunque esté dentro de un contexto
        // de diálogo. Las teclas en sí las procesa el bucle de mensajes de la
        // app (atajos configurables), que llama a MoveFocus, SelectAll, etc.
        case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_VSCROLL: {
            // Interacción con la barra vertical nativa.
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd_, SB_VERT, &si);
            int y = scrollY_;
            switch (LOWORD(wParam)) {
                // Flechas de la barra: una fila; zona vacía: una página.
                case SB_LINEUP: y -= RowHeight(); break;
                case SB_LINEDOWN: y += RowHeight(); break;
                case SB_PAGEUP: y -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN: y += static_cast<int>(si.nPage); break;
                // Arrastre del cursor de la barra: nTrackPos tiene 32 bits
                // (HIWORD(wParam) se cortaría a 16 bits con historiales largos).
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: y = si.nTrackPos; break;
                case SB_TOP: y = 0; break;
                case SB_BOTTOM: y = ContentHeight(); break;  // ScrollTo lo limita al máximo
            }
            ScrollTo(y);
            return 0;
        }
        case WM_HSCROLL: {
            // Barra horizontal: igual que la vertical, con pasos de 40 DIP.
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
                // Un valor enorme (sin desbordar al sumar) que ScrollToX limita al máximo.
                case SB_RIGHT: x = INT_MAX / 2; break;
            }
            ScrollToX(x);
            return 0;
        }
        case WM_MOUSEHWHEEL:
            // Rueda horizontal (inclinación o panel táctil): 40 DIP por muesca
            // (WHEEL_DELTA = 120); delta positivo = hacia la derecha.
            ScrollToX(scrollX_ + MulDiv(GET_WHEEL_DELTA_WPARAM(wParam), Style::Get().Px(40), WHEEL_DELTA));
            return 0;
        case WM_MOUSEWHEEL: {
            // delta positivo = rueda hacia arriba. 3 filas por muesca (lo
            // habitual en Windows); MulDiv admite deltas parciales de ratones
            // de alta resolución.
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
            // Hay cuatro modos excluyentes, por prioridad: cambiar el ancho de
            // una columna, clic/arrastre de un título, rectángulo de selección
            // y, si no hay botón presionado, el resaltado bajo el mouse.
            if (resizing_ >= 0) {
                // Arrastrando el borde de una columna: el ancho sigue al mouse.
                // Ancho nuevo = el inicial + lo que se movió el mouse, con el
                // mínimo de config; se guarda en DIP (independiente de la
                // escala) y se limita al rango permitido.
                const auto& style = Style::Get();
                const int px = std::max(style.Px(config::kMinColumnWidth),
                                        resizeStartWidth_ + static_cast<int>(pt.x) - resizeStartX_);
                const int dip = std::clamp(MulDiv(px, 96, static_cast<int>(style.dpi())), config::kMinColumnWidth,
                                           config::kMaxColumnWidth);
                const std::wstring& id = columns_[static_cast<size_t>(resizing_)].id;
                // Solo se recalcula y repinta si el ancho en DIP cambió de
                // verdad. El aviso para guardarlo va al soltar (WM_LBUTTONUP),
                // no en cada movimiento.
                if (auto it = widths_.find(id); it == widths_.end() || it->second != dip) {
                    widths_[id] = dip;
                    UpdateScrollBar();
                    Invalidate();
                }
                return 0;
            }
            if (headerPressed_ >= 0) {
                const auto& style = Style::Get();
                // Umbral de 5 DIP: por debajo es un clic (ordenar); por encima,
                // un arrastre (mover la columna). Las columnas sin id no se mueven.
                if (!dragging_ && std::abs(pt.x - dragStartX_) > style.Px(5) &&
                    !columns_[static_cast<size_t>(headerPressed_)].id.empty())
                    dragging_ = true;
                if (dragging_) {
                    dragX_ = pt.x;
                    // Cerca de los bordes desplaza la tabla para llegar a las columnas ocultas.
                    // (Mientras haya captura, cada movimiento cerca del borde
                    // avanza 20 DIP; mover el mouse sigue desplazando.)
                    const int edge = style.Px(24);
                    if (pt.x < edge) ScrollToX(scrollX_ - style.Px(20));
                    else if (pt.x > ClientWidth() - edge) ScrollToX(scrollX_ + style.Px(20));
                    Invalidate();
                }
                return 0;
            }
            if (bandPressed_) {
                if (!bandActive_) {
                    // El rectángulo aparece recién al mover más de 5 DIP (en
                    // cualquier eje) para que un clic con un leve temblor no
                    // se convierta en selección por arrastre. La y del mouse
                    // se pasa a coordenadas de contenido para comparar.
                    const int threshold = Style::Get().Px(5);
                    if (std::abs(pt.x - bandStart_.x) > threshold ||
                        std::abs(pt.y - HeaderHeight() + scrollY_ - bandStart_.y) > threshold)
                        bandActive_ = true;
                }
                if (bandActive_) {
                    RECT client;
                    GetClientRect(hwnd_, &client);
                    // Desplaza la lista si se arrastra más allá del borde.
                    // (Una fila por movimiento: hay captura, así que llegan
                    // WM_MOUSEMOVE aunque el mouse esté fuera de la ventana.)
                    if (pt.y < HeaderHeight()) ScrollTo(scrollY_ - RowHeight());
                    else if (pt.y > client.bottom) ScrollTo(scrollY_ + RowHeight());
                    // La esquina móvil se limita al área de filas visible y se
                    // pasa a coordenadas de contenido (con el desplazamiento
                    // nuevo), así el rectángulo crece al desplazarse.
                    bandEnd_.x = pt.x;
                    bandEnd_.y = std::clamp<LONG>(pt.y, HeaderHeight(), client.bottom) - HeaderHeight() + scrollY_;
                    UpdateBandSelection();
                }
                return 0;
            }
            // Sin botón presionado: resaltado de fila, título y borde bajo el mouse.
            if (!trackingMouse_) {
                // Pide WM_MOUSELEAVE para quitar el resaltado cuando el mouse
                // salga de la ventana (Windows no lo envía si no se pide).
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                trackingMouse_ = true;
            }
            int row = RowAt(pt.y);
            // Bordes de columnas sin id no se pueden arrastrar (no hay dónde
            // guardar su ancho): no se resaltan.
            int divider = DividerAt(pt);
            if (divider >= 0 && columns_[static_cast<size_t>(divider)].id.empty()) divider = -1;
            // El borde tiene prioridad sobre el título; y solo se resaltan los
            // títulos de columnas ordenables (son los que responden al clic).
            int column = divider >= 0 ? -1 : HeaderColumnAt(pt);
            if (column >= 0 && columns_[static_cast<size_t>(column)].sortId < 0) column = -1;
            // Repintar solo si algo cambió (WM_MOUSEMOVE llega muy seguido).
            if (row != hover_ || column != hoverHeader_ || divider != hoverDivider_) {
                hover_ = row;
                hoverHeader_ = column;
                hoverDivider_ = divider;
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            // El mouse salió de la ventana: hay que volver a pedir el aviso la
            // próxima vez que entre, y se quita todo resaltado.
            trackingMouse_ = false;
            if (hover_ != -1 || hoverHeader_ != -1 || hoverDivider_ != -1) {
                hover_ = -1;
                hoverHeader_ = -1;
                hoverDivider_ = -1;
                Invalidate();
            }
            return 0;
        case WM_SETCURSOR:
            // Cursor según lo que hay bajo el mouse (solo en el área cliente;
            // sobre las barras de desplazamiento decide Windows).
            // Flecha doble horizontal sobre un borde de columna o mientras se arrastra.
            if (LOWORD(lParam) == HTCLIENT && (hoverDivider_ >= 0 || resizing_ >= 0)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            // Mano sobre un título ordenable (indica que se puede hacer clic).
            if (LOWORD(lParam) == HTCLIENT && hoverHeader_ >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            // Si no, el cursor de la clase (flecha) vía DefWindowProc.
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN: {
            // Cualquier clic da el foco del teclado a la tabla (para las flechas y atajos).
            SetFocus(hwnd_);
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (pt.y < HeaderHeight()) {
                // --- Clic en el encabezado ---
                // Clic en el borde de una columna: empezar a cambiar su ancho.
                const int divider = DividerAt(pt);
                if (msg == WM_LBUTTONDOWN && divider >= 0 && !columns_[static_cast<size_t>(divider)].id.empty()) {
                    // Se toma el ancho actual en px (puede ser el flexible o el
                    // predeterminado, no necesariamente uno guardado).
                    for (const auto& slot : Layout(ClientWidth()))
                        if (static_cast<int>(slot.column) == divider) resizeStartWidth_ = slot.right - slot.left;
                    resizing_ = divider;
                    resizeStartX_ = pt.x;
                    // Captura: los movimientos y el soltar llegan aquí aunque
                    // el mouse salga de la ventana.
                    SetCapture(hwnd_);
                    Invalidate();
                    return 0;
                }
                // Clic derecho (o en un borde no redimensionable): nada; el
                // menú del encabezado lo abre WM_CONTEXTMENU.
                if (divider >= 0) return 0;
                // Título de una columna: al soltar sin mover se ordena por ella;
                // si se arrastra, se mueve la columna (como en la cola de Windows).
                int column = HeaderColumnAt(pt);
                if (msg == WM_LBUTTONDOWN && column >= 0) {
                    headerPressed_ = column;
                    dragging_ = false;
                    dragStartX_ = dragX_ = pt.x;
                    // Dónde se "agarró" el título, para que el fantasma no salte
                    // al empezar a moverlo.
                    for (const auto& slot : Layout(ClientWidth()))
                        if (static_cast<int>(slot.column) == column) dragGrabOffset_ = pt.x - slot.left;
                    SetCapture(hwnd_);
                }
                return 0;
            }
            // --- Clic en el área de filas ---
            // Los modificadores vienen en wParam (estado al momento del clic).
            const bool ctrl = (wParam & MK_CONTROL) != 0;
            const bool shift = (wParam & MK_SHIFT) != 0;
            int row = RowAt(pt.y);
            if (row >= 0) {
                // Clic derecho sobre una fila ya seleccionada conserva la selección múltiple.
                // (Así el menú contextual actúa sobre todas, como en el Explorador.)
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
                // También si el clic fue sobre una fila: arrastrar desde ella
                // selecciona un rango, como en el Explorador.
                bandPressed_ = true;
                bandStart_ = {pt.x, pt.y - HeaderHeight() + scrollY_};
                bandEnd_ = bandStart_;
                // Con Ctrl el rectángulo se suma a la selección que había.
                bandBase_ = ctrl ? selected_ : std::set<std::wstring>{};
                SetCapture(hwnd_);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (headerPressed_ >= 0) {
                // Se soltó un título: si no hubo arrastre es un clic (ordenar);
                // si lo hubo, FinishColumnDrag aplica el nuevo orden.
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const int column = headerPressed_;
                const bool click = !dragging_;
                FinishColumnDrag(true);
                // El clic ordena solo si se soltó sobre el mismo título (como
                // un botón: soltar fuera cancela) y la columna es ordenable.
                if (click && HeaderColumnAt(pt) == column && columns_[static_cast<size_t>(column)].sortId >= 0 &&
                    callbacks_.sortClicked)
                    callbacks_.sortClicked(columns_[static_cast<size_t>(column)].sortId);
                return 0;
            }
            if (resizing_ >= 0) {
                // Fin del cambio de ancho. Se copia el id (no referencia)
                // porque el callback puede llamar a SetColumns y reemplazar columns_.
                const std::wstring id = columns_[static_cast<size_t>(resizing_)].id;
                // resizing_ = -1 antes de ReleaseCapture: así WM_CAPTURECHANGED
                // (que llega dentro de ReleaseCapture) no avisa por segunda vez.
                resizing_ = -1;
                ReleaseCapture();
                Invalidate();
                // Sin mover el mouse no cambió nada (y la columna sigue con su ancho predeterminado).
                if (auto it = widths_.find(id); it != widths_.end() && callbacks_.columnResized)
                    callbacks_.columnResized(id, it->second);
                return 0;
            }
            if (bandPressed_) {
                // Fin del rectángulo de selección (o de un clic simple): la
                // selección ya quedó aplicada en cada movimiento.
                bandPressed_ = false;
                bandActive_ = false;
                bandBase_.clear();
                ReleaseCapture();
                Invalidate();
            }
            return 0;
        case WM_CAPTURECHANGED:
            // Otra ventana tomó el mouse (Alt+Tab, un diálogo, un menú...) o se
            // liberó la captura: se cierran los arrastres en curso.
            if (headerPressed_ >= 0) FinishColumnDrag(false);  // se perdió el mouse: no se mueve nada
            if (resizing_ >= 0) {
                // Se perdió el mouse a mitad del arrastre: se guarda lo que haya quedado.
                const std::wstring id = columns_[static_cast<size_t>(resizing_)].id;
                resizing_ = -1;
                if (auto it = widths_.find(id); it != widths_.end() && callbacks_.columnResized)
                    callbacks_.columnResized(id, it->second);
            }
            // La selección hecha con el rectángulo se conserva; solo se termina el arrastre.
            bandPressed_ = bandActive_ = false;
            Invalidate();
            return 0;
        case WM_LBUTTONDBLCLK: {
            // (Llega gracias a CS_DBLCLKS en la clase de ventana; antes llegó
            // el WM_LBUTTONDOWN del primer clic, que ya seleccionó la fila.)
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
            // Doble clic en una fila: "activar" (MainWindow abre la ficha del trabajo).
            if (RowAt(pt.y) >= 0 && callbacks_.activate) callbacks_.activate();
            return 0;
        }
        case WM_CONTEXTMENU: {
            // lParam trae la posición en coordenadas de PANTALLA, o (-1, -1)
            // si el menú se pidió con el teclado.
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (pt.x != -1 || pt.y != -1) {
                // Con el mouse: si fue sobre el encabezado, menú de columnas
                // (restablecer anchos y orden), no el de trabajos.
                POINT local = pt;
                ScreenToClient(hwnd_, &local);
                if (local.y >= 0 && local.y < HeaderHeight()) {
                    if (callbacks_.headerMenu) callbacks_.headerMenu(pt);
                    return 0;
                }
            }
            if (pt.x == -1 && pt.y == -1) {  // desde el teclado (tecla Menú / Shift+F10)
                // Se ubica el menú bajo la fila con foco (en su borde inferior,
                // a 40 DIP del margen), sin subir más allá del encabezado; si
                // no hay foco (focus_ = -1) queda justo debajo del encabezado.
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
