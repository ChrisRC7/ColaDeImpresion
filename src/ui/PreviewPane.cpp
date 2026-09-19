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
    emptyMessage_ = message;
    title_.clear();
    details_.clear();
    Invalidate();
}

void PreviewPane::ShowLoading(const std::wstring& title, const std::vector<std::wstring>& details) {
    if (mode_ != Mode::Loading || title != title_) {
        page_ = 0;
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

int PreviewPane::PageCount() const { return doc_ ? static_cast<int>(doc_->pages.size()) : 0; }

void PreviewPane::NextPage() {
    if (page_ + 1 < PageCount()) {
        ++page_;
        pan_ = {0, 0};
        Invalidate();
    }
}

void PreviewPane::PrevPage() {
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
    y += style.Px(10);
    gfx::HLine(dc, client.left, client.right, y - 1, pal.border);
    return y - client.top;
}

void PreviewPane::PaintMessage(HDC dc, const RECT& area, wchar_t glyph, const std::wstring& text) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    int cy = (area.top + area.bottom) / 2;
    RECT iconRect{area.left, cy - style.Px(64), area.right, cy - style.Px(12)};
    gfx::Glyph(dc, glyph, iconRect, style.fonts().iconLarge, Blend(pal.textMuted, pal.previewBackdrop, 0.3));
    RECT textRect{area.left + style.Px(28), cy, area.right - style.Px(28), area.bottom - style.Px(8)};
    gfx::Text(dc, text, textRect, style.fonts().normal, pal.textMuted, DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
}

void PreviewPane::PaintFooter(HDC dc, const RECT& footer) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    gfx::Fill(dc, footer, pal.surface);
    gfx::HLine(dc, footer.left, footer.right, footer.top, pal.border);
    prevButton_ = nextButton_ = RECT{};
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
