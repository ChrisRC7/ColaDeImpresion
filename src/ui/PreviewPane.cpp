// Panel de vista previa y ficha "Configuración": estados (vacío, cargando,
// documento), navegación por páginas, zoom y desplazamiento de la página
// ampliada, ficha con desplazamiento propio, y dibujo de todo con doble búfer.

#include "ui/PreviewPane.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "i18n/Strings.h"
#include "ui/Gfx.h"
#include "ui/Style.h"
#include "util/Strings.h"

namespace cola::ui {

using i18n::S;
using i18n::Tr;

namespace {
// Límites y paso del zoom (1.0 = la página entera ajustada al área).
constexpr double kMinZoom = 0.5;
constexpr double kMaxZoom = 6.0;
constexpr double kZoomStep = 1.25;  // cada paso multiplica o divide por 1,25
}  // namespace

bool PreviewPane::Create(HWND parent) {
    return CreateWin(L"ColaPreviewPane", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, 0, parent);
}

void PreviewPane::ShowEmpty(const std::wstring& message) {
    mode_ = Mode::Empty;
    // Se suelta el documento (puede ocupar memoria: páginas EMF o imágenes).
    doc_.reset();
    sheet_.clear();
    sheetScroll_ = 0;
    emptyMessage_ = message;
    title_.clear();
    details_.clear();
    Invalidate();
}

void PreviewPane::ShowLoading(const std::wstring& title, const std::vector<std::wstring>& details) {
    // Solo se reinicia la vista si cambió de trabajo: ShowLoading puede
    // llamarse varias veces para el mismo mientras se prepara.
    if (mode_ != Mode::Loading || title != title_) {
        page_ = 0;
        sheetScroll_ = 0;
        ZoomReset();
    }
    mode_ = Mode::Loading;
    doc_.reset();
    title_ = title;
    details_ = details;
    Invalidate();
}

void PreviewPane::ShowDocument(const std::wstring& title, const std::vector<std::wstring>& details,
                               preview::DocumentPtr doc) {
    // "Mismo documento" = ya se mostraba uno con igual título y formato (p. ej.
    // la vista previa se regeneró al refrescar la cola).
    bool sameDocument = mode_ == Mode::Document && title == title_ && doc_ && doc &&
                        doc_->format == doc->format;
    if (title != title_) sheetScroll_ = 0;
    mode_ = Mode::Document;
    doc_ = std::move(doc);
    title_ = title;
    details_ = details;
    if (!sameDocument) {
        page_ = 0;
        zoom_ = 1.0;
        pan_ = {0, 0};
    }
    // El documento nuevo puede tener menos páginas: la actual se limita.
    page_ = std::clamp(page_, 0, std::max(0, PageCount() - 1));
    Invalidate();
}

void PreviewPane::SetInfo(const std::wstring& title, const std::vector<std::wstring>& details) {
    title_ = title;
    details_ = details;
    Invalidate();
}

void PreviewPane::SetSheet(std::vector<InfoRow> rows) {
    sheet_ = std::move(rows);
    // Solo hace falta repintar si la ficha está a la vista.
    if (tab_ == Tab::Sheet) Invalidate();
}

void PreviewPane::SetTab(Tab tab) {
    if (tab_ == tab) return;
    tab_ = tab;
    // Un arrastre de la página no tiene sentido en la ficha. (La captura del
    // mouse, si la había, se libera al soltar el botón.)
    dragging_ = false;
    Invalidate();
}

void PreviewPane::ScrollSheet(int delta) {
    // sheetContent_ se calcula al pintar la ficha (el alto depende del ancho
    // y del texto); el máximo es lo que sobresale del área visible.
    const int visible = gfx::Height(PageArea());
    const int maxScroll = std::max(0, sheetContent_ - visible);
    // delta positivo (rueda hacia arriba) = subir = menos desplazamiento.
    sheetScroll_ = std::clamp(sheetScroll_ - delta, 0, maxScroll);
    Invalidate();
}

int PreviewPane::PageCount() const { return doc_ ? static_cast<int>(doc_->pages.size()) : 0; }

void PreviewPane::NextPage() {
    if (tab_ == Tab::Sheet) {  // en la ficha, avanza una pantalla
        ScrollSheet(-gfx::Height(PageArea()) * 4 / 5);
        return;
    }
    if (page_ + 1 < PageCount()) {
        ++page_;
        // Página nueva: se vuelve a centrar (el zoom se conserva).
        pan_ = {0, 0};
        Invalidate();
    }
}

void PreviewPane::PrevPage() {
    if (tab_ == Tab::Sheet) {
        ScrollSheet(gfx::Height(PageArea()) * 4 / 5);
        return;
    }
    if (page_ > 0) {
        --page_;
        pan_ = {0, 0};
        Invalidate();
    }
}

void PreviewPane::SetZoom(double zoom) {
    zoom_ = std::clamp(zoom, kMinZoom, kMaxZoom);
    // Tras varios ×1,25 y ÷1,25 el valor queda cerca de 1 pero no exacto
    // (redondeo de punto flotante): se ajusta a 1 y se centra, para que
    // "100 %" vuelva a ser exactamente "ajustar a la ventana".
    if (std::fabs(zoom_ - 1.0) < 0.01) {
        zoom_ = 1.0;
        pan_ = {0, 0};
    }
    ClampPan();
    Invalidate();
}

void PreviewPane::ZoomIn() { SetZoom(zoom_ * kZoomStep); }
void PreviewPane::ZoomOut() { SetZoom(zoom_ / kZoomStep); }
void PreviewPane::ZoomReset() {
    zoom_ = 1.0;
    pan_ = {0, 0};
    Invalidate();
}

void PreviewPane::SetQuickLook(bool quickLook) {
    quickLook_ = quickLook;
    Invalidate();
}

RECT PreviewPane::PageArea() const {
    RECT client;
    GetClientRect(hwnd_, &client);
    const auto& style = Style::Get();
    // headerHeight_ es el del último pintado (el encabezado mide su alto al
    // dibujarse, porque las líneas de detalle pueden ocupar varias líneas).
    return {client.left, client.top + headerHeight_, client.right, client.bottom - style.Px(44)};
}

RECT PreviewPane::PageRect(const RECT& area) const {
    const auto& style = Style::Get();
    if (PageCount() == 0) return area;
    // 1) Rectángulo de la página ajustada al área con márgenes (18 DIP a los
    //    lados, 14 arriba/abajo), respetando su proporción (tamaño en HIMETRIC).
    RECT inner = gfx::Inset(area, style.Px(18), style.Px(14));
    RECT fit = preview::FitPage(doc_->pages[static_cast<size_t>(page_)]->SizeHimetric(), inner);
    // 2) Escalado por el zoom.
    int w = static_cast<int>(std::lround(gfx::Width(fit) * zoom_));
    int h = static_cast<int>(std::lround(gfx::Height(fit) * zoom_));
    // 3) Centrado en el área y corrido por el desplazamiento del operador.
    int cx = (area.left + area.right) / 2 + pan_.x;
    int cy = (area.top + area.bottom) / 2 + pan_.y;
    return {cx - w / 2, cy - h / 2, cx - w / 2 + w, cy - h / 2 + h};
}

void PreviewPane::ClampPan() {
    if (PageCount() == 0) return;
    RECT area = PageArea();
    // Se mide la página sin desplazamiento (solo importa su tamaño).
    POINT saved = pan_;
    pan_ = {0, 0};
    RECT r = PageRect(area);
    pan_ = saved;
    // Cuánto sobresale la página del área (si no sobresale, queda centrada).
    // Se suman 24 DIP de holgura para poder ver el borde de la página con un
    // poco de fondo alrededor.
    int overX = std::max(0, (gfx::Width(r) - gfx::Width(area)) / 2 + Style::Get().Px(24));
    int overY = std::max(0, (gfx::Height(r) - gfx::Height(area)) / 2 + Style::Get().Px(24));
    pan_.x = std::clamp<LONG>(pan_.x, -overX, overX);
    pan_.y = std::clamp<LONG>(pan_.y, -overY, overY);
}

int PreviewPane::PaintHeader(HDC dc, const RECT& client) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const int pad = style.Px(18);
    int y = client.top + style.Px(14);
    int width = gfx::Width(client) - pad * 2;
    // Sin título no hay encabezado (ni pestañas).
    if (title_.empty()) return 0;

    // Título (nombre del documento) en una línea con "…".
    RECT titleRect{client.left + pad, y, client.right - pad, y + style.Px(24)};
    gfx::Text(dc, title_, titleRect, style.fonts().title, pal.text);
    y = titleRect.bottom + style.Px(4);
    // Líneas de detalle (usuario, equipo, estado...): cada una puede partirse
    // en varias líneas, pero como máximo 48 DIP de alto (unas 3 líneas) para
    // que un texto largo no se coma el área de la página.
    for (const auto& line : details_) {
        if (line.empty()) continue;
        int h = std::min(gfx::MeasureWrapped(dc, line, style.fonts().small, width), style.Px(48));
        RECT r{client.left + pad, y, client.right - pad, y + h};
        gfx::Text(dc, line, r, style.fonts().small, pal.textMuted, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        y += h + style.Px(2);
    }
    y += style.Px(4);
    // Pestañas y línea divisoria al pie del encabezado.
    y += PaintTabs(dc, client, y);
    gfx::HLine(dc, client.left, client.right, y - 1, pal.border);
    return y - client.top;
}

int PreviewPane::PaintTabs(HDC dc, const RECT& client, int top) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const int height = style.Px(30);
    int x = client.left + style.Px(14);
    // Dibuja una pestaña en x (ancho = texto + 16 DIP), guarda su rectángulo
    // en `out` (para los clics) y avanza x. La seleccionada lleva texto normal
    // y una barrita de acento debajo; la otra, texto atenuado.
    auto tab = [&](const wchar_t* text, bool selected, RECT& out) {
        SIZE size = gfx::MeasureText(dc, text, style.fonts().semibold);
        out = {x, top, x + size.cx + style.Px(16), top + height};
        gfx::Text(dc, text, out, style.fonts().semibold, selected ? pal.text : pal.textMuted,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (selected) {
            RECT bar{out.left + style.Px(4), out.bottom - style.Px(3), out.right - style.Px(4), out.bottom - style.Px(1)};
            gfx::FillRounded(dc, bar, style.Px(1), pal.accent);
        }
        x = out.right + style.Px(4);
    };
    tab(Tr(S::PreviewTabPreview), tab_ == Tab::Preview, tabPreview_);
    tab(Tr(S::PreviewTabSheet), tab_ == Tab::Sheet, tabSheet_);
    return height;
}

void PreviewPane::PaintSheet(HDC dc, const RECT& area) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    gfx::Fill(dc, area, pal.surface);
    if (sheet_.empty()) {
        sheetContent_ = 0;
        PaintMessage(dc, area, glyph::Document, Tr(S::SheetEmpty));
        return;
    }
    const int pad = style.Px(18);
    const int width = gfx::Width(area) - pad * 2;
    // Columna de etiquetas: 170 DIP o el 38 % del ancho si el panel es angosto.
    const int labelWidth = std::min(style.Px(170), width * 38 / 100);
    const UINT wrap = DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX;
    // Recorte al área: las filas desplazadas no deben invadir encabezado ni pie.
    int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    // y = borde superior de la fila actual en coordenadas cliente (ya con el
    // desplazamiento restado). Se recorren TODAS las filas para medir el alto
    // total, aunque solo se dibujan las visibles.
    int y = area.top + style.Px(8) - sheetScroll_;
    bool first = true;
    for (const auto& row : sheet_) {
        if (row.kind == InfoRow::Kind::Section) {
            // Título de sección con línea debajo; 10 DIP de aire antes de
            // cada sección salvo la primera.
            if (!first) y += style.Px(10);
            RECT r{area.left + pad, y, area.right - pad, y + style.Px(24)};
            gfx::Text(dc, row.label, r, style.fonts().semibold, pal.text);
            gfx::HLine(dc, area.left + pad, area.right - pad, r.bottom, pal.border);
            y = r.bottom + style.Px(6);
            first = false;
            continue;
        }
        first = false;
        // Campo: [etiqueta] [valor ............ (insignia)]
        //                   [detalle: origen / motivo, a todo el ancho]
        const int left = area.left + pad;
        const int valueX = left + labelWidth + style.Px(10);
        int badgeWidth = 0;
        if (!row.badge.empty()) badgeWidth = gfx::MeasureText(dc, row.badge, style.fonts().small).cx + style.Px(16);
        // El valor deja lugar a la insignia (a la derecha); el detalle va
        // debajo y usa todo el ancho. Mínimo 40 DIP para paneles muy angostos.
        const int valueWidth = std::max<int>(style.Px(40), area.right - pad - valueX - (badgeWidth ? badgeWidth + style.Px(8) : 0));
        const int detailWidth = std::max<int>(style.Px(40), area.right - pad - valueX);
        // Altos con ajuste de línea; la fila mide lo más alto de etiqueta,
        // valor e insignia, más el detalle y 8 DIP de separación.
        int labelHeight = gfx::MeasureWrapped(dc, row.label, style.fonts().small, labelWidth);
        int valueHeight = gfx::MeasureWrapped(dc, row.value, style.fonts().normal, valueWidth);
        int detailHeight = row.detail.empty() ? 0 : gfx::MeasureWrapped(dc, row.detail, style.fonts().small, detailWidth);
        int rowHeight = std::max({labelHeight, valueHeight, badgeWidth ? style.Px(20) : 0}) +
                        (detailHeight ? detailHeight + style.Px(2) : 0) + style.Px(8);
        // Solo se dibuja si asoma en el área (las demás solo suman su alto).
        if (y + rowHeight >= area.top && y <= area.bottom) {
            // La etiqueta baja 1 px para alinear su línea base con la del valor
            // (fuente más chica).
            gfx::Text(dc, row.label, RECT{left, y + style.Px(1), left + labelWidth, y + labelHeight + style.Px(1)},
                      style.fonts().small, pal.textMuted, wrap);
            gfx::Text(dc, row.value, RECT{valueX, y, valueX + valueWidth, y + valueHeight}, style.fonts().normal,
                      pal.text, wrap);
            if (badgeWidth) {
                RECT pill{area.right - pad - badgeWidth, y, area.right - pad, y + style.Px(20)};
                gfx::FillRounded(dc, pill, style.Px(10), pal.ToneBackground(row.badgeTone));
                gfx::Text(dc, row.badge, pill, style.fonts().small, pal.ToneColor(row.badgeTone),
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            if (detailHeight) {
                int top = y + std::max({labelHeight, valueHeight, badgeWidth ? style.Px(20) : 0}) + style.Px(2);
                gfx::Text(dc, row.detail, RECT{valueX, top, valueX + detailWidth, top + detailHeight},
                          style.fonts().small, pal.textMuted, wrap);
            }
        }
        y += rowHeight;
    }
    RestoreDC(dc, saved);
    // Alto total del contenido (sin el desplazamiento) + 12 DIP de margen
    // inferior; lo usa ScrollSheet para limitar el desplazamiento.
    sheetContent_ = y + sheetScroll_ - area.top + style.Px(12);
    // Indicador de desplazamiento si la ficha no cabe.
    // Es solo visual (una barrita fina a la derecha, no una barra nativa): la
    // ficha se desplaza con la rueda o con las teclas de página.
    const int visible = gfx::Height(area);
    if (sheetContent_ > visible) {
        // Recorrido = alto visible menos 4 DIP arriba y abajo; el cursor es
        // proporcional a la parte visible (mínimo 24 DIP) y su posición, a
        // la fracción desplazada.
        int trackHeight = visible - style.Px(8);
        int thumb = std::max(style.Px(24), trackHeight * visible / sheetContent_);
        int maxScroll = sheetContent_ - visible;
        int thumbTop = area.top + style.Px(4) + (trackHeight - thumb) * std::min(sheetScroll_, maxScroll) / maxScroll;
        RECT bar{area.right - style.Px(6), thumbTop, area.right - style.Px(3), thumbTop + thumb};
        gfx::FillRounded(dc, bar, style.Px(1), pal.border);
    }
}

void PreviewPane::PaintMessage(HDC dc, const RECT& area, wchar_t glyph, const std::wstring& text) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const UINT flags = DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX;
    // Mide el texto para centrar el bloque: los mensajes de diagnóstico ocupan
    // varias líneas y antes se salían del panel.
    RECT measure{area.left + style.Px(28), 0, area.right - style.Px(28), 0};
    HGDIOBJ old = SelectObject(dc, style.fonts().normal);
    DrawTextW(dc, text.c_str(), -1, &measure, flags | DT_CALCRECT);
    SelectObject(dc, old);
    // El texto nunca ocupa más que el área (menos un margen); si es más largo
    // se corta abajo.
    const int textHeight = std::min<int>(measure.bottom - measure.top, gfx::Height(area) - style.Px(24));
    const int iconHeight = style.Px(52);
    // El glifo es decorativo: se omite si no cabe junto al texto.
    const bool showIcon = gfx::Height(area) > textHeight + iconHeight + style.Px(32);
    // Bloque (glifo + texto) centrado verticalmente.
    int top = (area.top + area.bottom - textHeight - (showIcon ? iconHeight : 0)) / 2;
    if (showIcon) {
        RECT iconRect{area.left, top, area.right, top + iconHeight};
        gfx::Glyph(dc, glyph, iconRect, style.fonts().iconLarge, Blend(pal.textMuted, pal.previewBackdrop, 0.3));
        top += iconHeight + style.Px(8);
    }
    RECT textRect{area.left + style.Px(28), top, area.right - style.Px(28), area.bottom - style.Px(8)};
    gfx::Text(dc, text, textRect, style.fonts().normal, pal.textMuted, flags);
}

void PreviewPane::PaintFooter(HDC dc, const RECT& footer) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    gfx::Fill(dc, footer, pal.surface);
    gfx::HLine(dc, footer.left, footer.right, footer.top, pal.border);
    // Los botones se vacían: si no se dibujan en este pintado, tampoco deben
    // responder a clics (WM_LBUTTONDOWN prueba estos rectángulos).
    prevButton_ = nextButton_ = RECT{};
    if (tab_ == Tab::Sheet) {
        // En la ficha: leyenda que explica las insignias de fiabilidad.
        RECT hint{footer.left + style.Px(14), footer.top, footer.right - style.Px(14), footer.bottom};
        gfx::Text(dc, Tr(S::SheetLegend), hint, style.fonts().small, pal.textMuted);
        return;
    }
    if (mode_ != Mode::Document || PageCount() == 0) return;

    // Botones ‹ › (30 DIP); el que no puede avanzar se pinta del color del
    // borde (aspecto deshabilitado).
    const int btn = style.Px(30);
    int cy = (footer.top + footer.bottom) / 2;
    prevButton_ = {footer.left + style.Px(10), cy - btn / 2, footer.left + style.Px(10) + btn, cy + btn / 2};
    nextButton_ = {prevButton_.right + style.Px(4), prevButton_.top, prevButton_.right + style.Px(4) + btn, prevButton_.bottom};
    gfx::Glyph(dc, glyph::ChevronLeft, prevButton_, style.fonts().iconSmall, page_ > 0 ? pal.text : pal.border);
    gfx::Glyph(dc, glyph::ChevronRight, nextButton_, style.fonts().iconSmall,
               page_ + 1 < PageCount() ? pal.text : pal.border);

    // Total = páginas del documento según el análisis; puede ser mayor que las
    // renderizadas (vista previa parcial por el límite de páginas o de
    // tiempo), y entonces el texto dice cuántas se pueden ver.
    std::uint32_t total = std::max<std::uint32_t>(doc_->totalPages, static_cast<std::uint32_t>(PageCount()));
    std::wstring pageText =
        total > static_cast<std::uint32_t>(PageCount())
            ? util::Format(Tr(S::PreviewPagePartial), page_ + 1, total, PageCount())
            : util::Format(Tr(S::PreviewPage), page_ + 1, total);
    RECT textRect{nextButton_.right + style.Px(8), footer.top, footer.right - style.Px(80), footer.bottom};
    gfx::Text(dc, pageText, textRect, style.fonts().small, pal.textMuted);

    // Porcentaje de zoom a la derecha (100 % = página ajustada al área).
    RECT zoomRect{footer.right - style.Px(80), footer.top, footer.right - style.Px(14), footer.bottom};
    gfx::Text(dc, util::Format(Tr(S::PreviewZoom), static_cast<int>(std::lround(zoom_ * 100))), zoomRect,
              style.fonts().small, pal.textMuted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void PreviewPane::Paint(HDC dc, const RECT& client) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    gfx::Fill(dc, client, pal.surface);

    // Sin trabajo: solo el mensaje, sobre el fondo de la vista previa, en todo el panel.
    if (mode_ == Mode::Empty) {
        headerHeight_ = 0;
        RECT area = client;
        gfx::Fill(dc, area, pal.previewBackdrop);
        PaintMessage(dc, area, glyph::Document, emptyMessage_);
        return;
    }

    // El encabezado se pinta primero porque su alto define dónde empieza el
    // área de la página (PageArea usa headerHeight_).
    headerHeight_ = PaintHeader(dc, client);
    RECT area = PageArea();
    gfx::Fill(dc, area, pal.previewBackdrop);
    RECT footer{client.left, area.bottom, client.right, client.bottom};
    PaintFooter(dc, footer);

    // La ficha está disponible también mientras carga la vista previa.
    if (tab_ == Tab::Sheet) {
        PaintSheet(dc, area);
        return;
    }
    if (mode_ == Mode::Loading) {
        PaintMessage(dc, area, glyph::Clock, Tr(S::PreviewLoading));
        return;
    }
    if (PageCount() == 0) {
        // Documento sin páginas: el motivo que dio la vista previa (p. ej.
        // ESC/Page sin intérprete, D26; datos incompletos, D48; falta
        // Ghostscript, D7) o uno genérico.
        std::wstring message = doc_ && !doc_->message.empty() ? doc_->message : Tr(S::PreviewUnsupported);
        PaintMessage(dc, area, glyph::Document, message);
        return;
    }

    // Página con sombra.
    RECT page = PageRect(area);
    // Con zoom la página puede salirse del área: se recorta para no pisar el
    // encabezado ni el pie.
    int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    // Sombra: la página corrida 2 DIP a la derecha y 3 abajo, 1 DIP más grande.
    RECT shadow = page;
    OffsetRect(&shadow, style.Px(2), style.Px(3));
    gfx::FillRounded(dc, gfx::Inset(shadow, -style.Px(1), -style.Px(1)), style.Px(3), pal.pageShadow);
    // El papel es blanco también en modo oscuro: así se ve como saldrá impreso.
    gfx::Fill(dc, page, RGB(255, 255, 255));
    // La página se dibuja a sí misma (EMF, imagen...) escalada al rectángulo.
    doc_->pages[static_cast<size_t>(page_)]->Render(dc, page);
    RestoreDC(dc, saved);

    // Nota (vista previa parcial / miniatura).
    // Píldora centrada al pie del área, encima de la página.
    std::wstring note;
    if (doc_->truncated) note = util::Format(Tr(S::PreviewTruncated), PageCount());
    else if (!doc_->message.empty()) note = doc_->message;
    if (!note.empty()) {
        RECT noteRect{area.left + style.Px(12), area.bottom - style.Px(30), area.right - style.Px(12), area.bottom - style.Px(6)};
        SIZE sz = gfx::MeasureText(dc, note, style.fonts().small);
        int w = std::min<int>(gfx::Width(noteRect), sz.cx + style.Px(20));
        RECT pill{(area.left + area.right - w) / 2, noteRect.top, (area.left + area.right + w) / 2, noteRect.bottom};
        gfx::FillRounded(dc, pill, style.Px(12), pal.surface);
        gfx::Text(dc, note, gfx::Inset(pill, style.Px(10), 0), style.fonts().small, pal.textMuted,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

LRESULT PreviewPane::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            gfx::BufferedPaint paint(hwnd_);
            Paint(paint.dc(), paint.rect());
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            // Con otro tamaño cambia cuánto sobresale la página ampliada.
            ClampPan();
            Invalidate();
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            // En la ficha la rueda desplaza (60 DIP por muesca).
            if (tab_ == Tab::Sheet) {
                ScrollSheet(MulDiv(delta, Style::Get().Px(60), WHEEL_DELTA));
                return 0;
            }
            if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {
                // Ctrl + rueda: zoom (un paso por mensaje, sin importar el delta).
                SetZoom(zoom_ * (delta > 0 ? kZoomStep : 1.0 / kZoomStep));
            } else if (zoom_ > 1.0) {
                // Página ampliada: la rueda la mueve en vertical (como en un visor de PDF).
                pan_.y += MulDiv(delta, Style::Get().Px(60), WHEEL_DELTA);
                ClampPan();
                Invalidate();
            } else {
                // Sin zoom: una página por muesca completa. Se acumula para que
                // los deltas parciales (paneles táctiles) no cambien de página
                // en cada pequeño movimiento.
                wheelAccumulator_ += delta;
                while (wheelAccumulator_ >= WHEEL_DELTA) {
                    PrevPage();
                    wheelAccumulator_ -= WHEEL_DELTA;
                }
                while (wheelAccumulator_ <= -WHEEL_DELTA) {
                    NextPage();
                    wheelAccumulator_ += WHEEL_DELTA;
                }
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            // Pestañas (solo existen fuera del modo vacío; en ese modo sus
            // rectángulos pueden haber quedado de un pintado anterior).
            if (mode_ != Mode::Empty && gfx::Contains(tabPreview_, pt)) {
                SetTab(Tab::Preview);
                return 0;
            }
            if (mode_ != Mode::Empty && gfx::Contains(tabSheet_, pt)) {
                SetTab(Tab::Sheet);
                return 0;
            }
            if (tab_ == Tab::Sheet) return 0;
            if (gfx::Contains(prevButton_, pt)) {
                PrevPage();
                return 0;
            }
            if (gfx::Contains(nextButton_, pt)) {
                NextPage();
                return 0;
            }
            // Con zoom, arrastrar la página la mueve ("mano"). Se captura el
            // mouse para seguirlo aunque salga del panel.
            if (zoom_ > 1.0 && gfx::Contains(PageArea(), pt)) {
                dragging_ = true;
                dragStart_ = pt;
                panStart_ = pan_;
                SetCapture(hwnd_);
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (dragging_) {
                // Desplazamiento = el del inicio + cuánto se movió el mouse
                // (la página sigue al cursor), limitado por ClampPan.
                pan_ = {panStart_.x + GET_X_LPARAM(lParam) - dragStart_.x, panStart_.y + GET_Y_LPARAM(lParam) - dragStart_.y};
                ClampPan();
                Invalidate();
            }
            return 0;
        case WM_LBUTTONUP:
            if (dragging_) {
                dragging_ = false;
                ReleaseCapture();
            }
            return 0;
        case WM_LBUTTONDBLCLK:
            // Doble clic: alterna entre "ajustar" y 200 %.
            if (tab_ == Tab::Sheet) return 0;
            if (zoom_ != 1.0) ZoomReset();
            else SetZoom(2.0);
            return 0;
        case WM_SETCURSOR:
            // Mano sobre la página ampliada (indica que se puede arrastrar).
            // La posición se toma de GetCursorPos porque este mensaje no la trae.
            if (LOWORD(lParam) == HTCLIENT && zoom_ > 1.0) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd_, &pt);
                if (gfx::Contains(PageArea(), pt)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            break;
    }
    return Window::HandleMessage(msg, wParam, lParam);
}

}  // namespace cola::ui
