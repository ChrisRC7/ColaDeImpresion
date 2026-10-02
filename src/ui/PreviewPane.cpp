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
constexpr double kMinZoom = 0.5;
constexpr double kMaxZoom = 6.0;
constexpr double kZoomStep = 1.25;
}  // namespace

bool PreviewPane::Create(HWND parent) {
    return CreateWin(L"ColaPreviewPane", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, 0, parent);
}

void PreviewPane::ShowEmpty(const std::wstring& message) {
    mode_ = Mode::Empty;
    doc_.reset();
    sheet_.clear();
    sheetScroll_ = 0;
    emptyMessage_ = message;
    title_.clear();
    details_.clear();
    Invalidate();
}

void PreviewPane::ShowLoading(const std::wstring& title, const std::vector<std::wstring>& details) {
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
    if (tab_ == Tab::Sheet) Invalidate();
}

void PreviewPane::SetTab(Tab tab) {
    if (tab_ == tab) return;
    tab_ = tab;
    dragging_ = false;
    Invalidate();
}

void PreviewPane::ScrollSheet(int delta) {
    const int visible = gfx::Height(PageArea());
    const int maxScroll = std::max(0, sheetContent_ - visible);
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
    return {client.left, client.top + headerHeight_, client.right, client.bottom - style.Px(44)};
}

RECT PreviewPane::PageRect(const RECT& area) const {
    const auto& style = Style::Get();
    if (PageCount() == 0) return area;
    RECT inner = gfx::Inset(area, style.Px(18), style.Px(14));
    RECT fit = preview::FitPage(doc_->pages[static_cast<size_t>(page_)]->SizeHimetric(), inner);
    int w = static_cast<int>(std::lround(gfx::Width(fit) * zoom_));
    int h = static_cast<int>(std::lround(gfx::Height(fit) * zoom_));
    int cx = (area.left + area.right) / 2 + pan_.x;
    int cy = (area.top + area.bottom) / 2 + pan_.y;
    return {cx - w / 2, cy - h / 2, cx - w / 2 + w, cy - h / 2 + h};
}

void PreviewPane::ClampPan() {
    if (PageCount() == 0) return;
    RECT area = PageArea();
    POINT saved = pan_;
    pan_ = {0, 0};
    RECT r = PageRect(area);
    pan_ = saved;
    // Cuánto sobresale la página del área (si no sobresale, queda centrada).
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
    if (title_.empty()) return 0;

    RECT titleRect{client.left + pad, y, client.right - pad, y + style.Px(24)};
    gfx::Text(dc, title_, titleRect, style.fonts().title, pal.text);
    y = titleRect.bottom + style.Px(4);
    for (const auto& line : details_) {
        if (line.empty()) continue;
        int h = std::min(gfx::MeasureWrapped(dc, line, style.fonts().small, width), style.Px(48));
        RECT r{client.left + pad, y, client.right - pad, y + h};
        gfx::Text(dc, line, r, style.fonts().small, pal.textMuted, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        y += h + style.Px(2);
    }
    y += style.Px(4);
    y += PaintTabs(dc, client, y);
    gfx::HLine(dc, client.left, client.right, y - 1, pal.border);
    return y - client.top;
}

int PreviewPane::PaintTabs(HDC dc, const RECT& client, int top) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const int height = style.Px(30);
    int x = client.left + style.Px(14);
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
    const int labelWidth = std::min(style.Px(170), width * 38 / 100);
    const UINT wrap = DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX;
    int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    int y = area.top + style.Px(8) - sheetScroll_;
    bool first = true;
    for (const auto& row : sheet_) {
        if (row.kind == InfoRow::Kind::Section) {
            if (!first) y += style.Px(10);
            RECT r{area.left + pad, y, area.right - pad, y + style.Px(24)};
            gfx::Text(dc, row.label, r, style.fonts().semibold, pal.text);
            gfx::HLine(dc, area.left + pad, area.right - pad, r.bottom, pal.border);
            y = r.bottom + style.Px(6);
            first = false;
            continue;
        }
        first = false;
        const int left = area.left + pad;
        const int valueX = left + labelWidth + style.Px(10);
        int badgeWidth = 0;
        if (!row.badge.empty()) badgeWidth = gfx::MeasureText(dc, row.badge, style.fonts().small).cx + style.Px(16);
        const int valueWidth = std::max<int>(style.Px(40), area.right - pad - valueX - (badgeWidth ? badgeWidth + style.Px(8) : 0));
        const int detailWidth = std::max<int>(style.Px(40), area.right - pad - valueX);
        int labelHeight = gfx::MeasureWrapped(dc, row.label, style.fonts().small, labelWidth);
        int valueHeight = gfx::MeasureWrapped(dc, row.value, style.fonts().normal, valueWidth);
        int detailHeight = row.detail.empty() ? 0 : gfx::MeasureWrapped(dc, row.detail, style.fonts().small, detailWidth);
        int rowHeight = std::max({labelHeight, valueHeight, badgeWidth ? style.Px(20) : 0}) +
                        (detailHeight ? detailHeight + style.Px(2) : 0) + style.Px(8);
        if (y + rowHeight >= area.top && y <= area.bottom) {
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
    sheetContent_ = y + sheetScroll_ - area.top + style.Px(12);
    // Indicador de desplazamiento si la ficha no cabe.
    const int visible = gfx::Height(area);
    if (sheetContent_ > visible) {
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
    const int textHeight = std::min<int>(measure.bottom - measure.top, gfx::Height(area) - style.Px(24));
    const int iconHeight = style.Px(52);
    const bool showIcon = gfx::Height(area) > textHeight + iconHeight + style.Px(32);
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
    prevButton_ = nextButton_ = RECT{};
    if (tab_ == Tab::Sheet) {
        RECT hint{footer.left + style.Px(14), footer.top, footer.right - style.Px(14), footer.bottom};
        gfx::Text(dc, Tr(S::SheetLegend), hint, style.fonts().small, pal.textMuted);
        return;
    }
    if (mode_ != Mode::Document || PageCount() == 0) return;

    const int btn = style.Px(30);
    int cy = (footer.top + footer.bottom) / 2;
    prevButton_ = {footer.left + style.Px(10), cy - btn / 2, footer.left + style.Px(10) + btn, cy + btn / 2};
    nextButton_ = {prevButton_.right + style.Px(4), prevButton_.top, prevButton_.right + style.Px(4) + btn, prevButton_.bottom};
    gfx::Glyph(dc, glyph::ChevronLeft, prevButton_, style.fonts().iconSmall, page_ > 0 ? pal.text : pal.border);
    gfx::Glyph(dc, glyph::ChevronRight, nextButton_, style.fonts().iconSmall,
               page_ + 1 < PageCount() ? pal.text : pal.border);

    std::uint32_t total = std::max<std::uint32_t>(doc_->totalPages, static_cast<std::uint32_t>(PageCount()));
    std::wstring pageText =
        total > static_cast<std::uint32_t>(PageCount())
            ? util::Format(Tr(S::PreviewPagePartial), page_ + 1, total, PageCount())
            : util::Format(Tr(S::PreviewPage), page_ + 1, total);
    RECT textRect{nextButton_.right + style.Px(8), footer.top, footer.right - style.Px(80), footer.bottom};
    gfx::Text(dc, pageText, textRect, style.fonts().small, pal.textMuted);

    RECT zoomRect{footer.right - style.Px(80), footer.top, footer.right - style.Px(14), footer.bottom};
    gfx::Text(dc, util::Format(Tr(S::PreviewZoom), static_cast<int>(std::lround(zoom_ * 100))), zoomRect,
              style.fonts().small, pal.textMuted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void PreviewPane::Paint(HDC dc, const RECT& client) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    gfx::Fill(dc, client, pal.surface);

    if (mode_ == Mode::Empty) {
        headerHeight_ = 0;
        RECT area = client;
        gfx::Fill(dc, area, pal.previewBackdrop);
        PaintMessage(dc, area, glyph::Document, emptyMessage_);
        return;
    }

    headerHeight_ = PaintHeader(dc, client);
    RECT area = PageArea();
    gfx::Fill(dc, area, pal.previewBackdrop);
    RECT footer{client.left, area.bottom, client.right, client.bottom};
    PaintFooter(dc, footer);

    if (tab_ == Tab::Sheet) {
        PaintSheet(dc, area);
        return;
    }
    if (mode_ == Mode::Loading) {
        PaintMessage(dc, area, glyph::Clock, Tr(S::PreviewLoading));
        return;
    }
    if (PageCount() == 0) {
        std::wstring message = doc_ && !doc_->message.empty() ? doc_->message : Tr(S::PreviewUnsupported);
        PaintMessage(dc, area, glyph::Document, message);
        return;
    }

    // Página con sombra.
    RECT page = PageRect(area);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    RECT shadow = page;
    OffsetRect(&shadow, style.Px(2), style.Px(3));
    gfx::FillRounded(dc, gfx::Inset(shadow, -style.Px(1), -style.Px(1)), style.Px(3), pal.pageShadow);
    gfx::Fill(dc, page, RGB(255, 255, 255));
    doc_->pages[static_cast<size_t>(page_)]->Render(dc, page);
    RestoreDC(dc, saved);

    // Nota (vista previa parcial / miniatura).
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
            ClampPan();
            Invalidate();
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (tab_ == Tab::Sheet) {
                ScrollSheet(MulDiv(delta, Style::Get().Px(60), WHEEL_DELTA));
                return 0;
            }
            if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {
                SetZoom(zoom_ * (delta > 0 ? kZoomStep : 1.0 / kZoomStep));
            } else if (zoom_ > 1.0) {
                pan_.y += MulDiv(delta, Style::Get().Px(60), WHEEL_DELTA);
                ClampPan();
                Invalidate();
            } else {
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
            if (tab_ == Tab::Sheet) return 0;
            if (zoom_ != 1.0) ZoomReset();
            else SetZoom(2.0);
            return 0;
        case WM_SETCURSOR:
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
