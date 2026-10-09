#include "ui/ExportDialog.h"

#include <windowsx.h>

#include <algorithm>

#include "i18n/Strings.h"
#include "ui/DarkMode.h"
#include "ui/Gfx.h"
#include "ui/Style.h"
#include "ui/Window.h"
#include "util/Strings.h"

namespace cola::ui {

using i18n::S;
using i18n::Tr;

void NormalizeExportChoice(ExportChoice& choice, size_t dayCount, bool fromChanged) {
    if (dayCount == 0) {
        choice = {};
        return;
    }
    choice.from = std::min(choice.from, dayCount - 1);
    choice.to = std::min(choice.to, dayCount - 1);
    // El extremo que se acaba de cambiar manda: arrastra al otro.
    if (choice.from > choice.to) {
        if (fromChanged) choice.to = choice.from;
        else choice.from = choice.to;
    }
}

std::uint32_t ExportChoiceRecords(const std::vector<ExportDay>& days, const ExportChoice& choice) {
    std::uint32_t total = 0;
    for (size_t i = 0; i < days.size(); ++i)
        if (choice.all || (i >= choice.from && i <= choice.to)) total += days[i].records;
    return total;
}

namespace {

enum Item { OptAll, OptRange, From, To, Cancel, Export, ItemCount };

class ExportDialog : public Window {
public:
    ExportDialog(const std::vector<ExportDay>& days, ExportChoice choice) : days_(days), choice_(choice) {}

    bool Run(HWND owner) {
        const Style& style = Style::Get();
        const RECT client{0, 0, style.Px(480), Layout()};
        RECT frame = client;
        const DWORD ws = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        const DWORD ex = WS_EX_DLGMODALFRAME;
        AdjustWindowRectEx(&frame, ws, FALSE, ex);
        const int w = frame.right - frame.left, h = frame.bottom - frame.top;
        RECT ownerRect{};
        GetWindowRect(owner, &ownerRect);
        const int x = ownerRect.left + (ownerRect.right - ownerRect.left - w) / 2;
        const int y = ownerRect.top + std::max(0L, (ownerRect.bottom - ownerRect.top - h) / 3);
        if (!CreateWin(L"ColaExportDialog", Tr(S::ExportDialogTitle), ws, ex, x, y, w, h, owner)) return false;
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
        return result_;
    }

    const ExportChoice& choice() const { return choice_; }

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
            case WM_LBUTTONDOWN: {
                const int hit = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                if (hit >= 0) {
                    focus_ = hit;
                    Activate(hit);
                }
                return 0;
            }
            case WM_KEYDOWN: OnKey(static_cast<UINT>(wParam)); return 0;
            case WM_SYSKEYDOWN:
                // Alt+↓ abre la lista de días (como un cuadro combinado).
                if (wParam == VK_DOWN && (focus_ == From || focus_ == To)) {
                    Activate(focus_);
                    return 0;
                }
                break;
            case WM_CLOSE: Close(false); return 0;
            default: break;
        }
        return DefaultProc(msg, wParam, lParam);
    }

private:
    // Calcula los rectángulos y devuelve el alto del área cliente.
    int Layout() {
        const Style& style = Style::Get();
        const int pad = style.Px(20);
        const int right = style.Px(480) - pad;
        int y = pad;
        prompt_ = {pad, y, right, y + style.Px(22)};
        y = prompt_.bottom + style.Px(10);
        rects_[OptAll] = {pad, y, right, y + style.Px(50)};
        y = rects_[OptAll].bottom + style.Px(4);
        rects_[OptRange] = {pad, y, right, y + style.Px(36)};
        y = rects_[OptRange].bottom + style.Px(6);

        const int indent = pad + style.Px(30);
        const int label = style.Px(48), field = style.Px(150), gap = style.Px(14);
        fromLabel_ = {indent, y, indent + label, y + style.Px(32)};
        rects_[From] = {fromLabel_.right, y, fromLabel_.right + field, fromLabel_.bottom};
        toLabel_ = {rects_[From].right + gap, y, rects_[From].right + gap + label, fromLabel_.bottom};
        rects_[To] = {toLabel_.right, y, toLabel_.right + field, fromLabel_.bottom};
        y = rects_[To].bottom + style.Px(8);
        count_ = {indent, y, right, y + style.Px(20)};
        y = count_.bottom + style.Px(20);

        const int buttonH = style.Px(34);
        rects_[Export] = {right - style.Px(110), y, right, y + buttonH};
        rects_[Cancel] = {rects_[Export].left - style.Px(8) - style.Px(100), y, rects_[Export].left - style.Px(8),
                          y + buttonH};
        return y + buttonH + pad;
    }

    int HitTest(POINT p) const {
        for (int i = 0; i < ItemCount; ++i)
            if (gfx::Contains(rects_[i], p)) return i;
        return -1;
    }

    void Close(bool result) {
        result_ = result;
        done_ = true;
    }

    void Activate(int item) {
        switch (item) {
            case OptAll: choice_.all = true; break;
            case OptRange: choice_.all = false; break;
            case From:
            case To:
                choice_.all = false;
                Invalidate();
                ChooseDay(item == From);
                break;
            case Cancel: Close(false); return;
            case Export: Close(true); return;
            default: break;
        }
        Invalidate();
    }

    void StepDay(bool from, int delta) {
        choice_.all = false;
        size_t& index = from ? choice_.from : choice_.to;
        if (delta < 0 && index > 0) --index;
        if (delta > 0 && index + 1 < days_.size()) ++index;
        NormalizeExportChoice(choice_, days_.size(), from);
        Invalidate();
    }

    // Lista de días con registros, del más reciente al más antiguo.
    void ChooseDay(bool from) {
        HMENU menu = CreatePopupMenu();
        const size_t current = from ? choice_.from : choice_.to;
        for (size_t i = days_.size(); i-- > 0;) {
            const std::wstring text = util::Format(Tr(S::ExportDayItem), days_[i].label.c_str(), days_[i].records);
            AppendMenuW(menu, MF_STRING | (i == current ? MF_CHECKED : 0), static_cast<UINT_PTR>(i + 1), text.c_str());
        }
        const RECT& r = rects_[from ? From : To];
        POINT pt{r.left, r.bottom + Style::Get().Px(2)};
        ClientToScreen(hwnd_, &pt);
        const UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
        if (id == 0) return;
        (from ? choice_.from : choice_.to) = id - 1;
        NormalizeExportChoice(choice_, days_.size(), from);
        Invalidate();
    }

    void OnKey(UINT key) {
        const bool shift = GetKeyState(VK_SHIFT) < 0;
        switch (key) {
            case VK_ESCAPE: Close(false); return;
            case VK_RETURN: Close(focus_ != Cancel); return;
            case VK_TAB: focus_ = (focus_ + (shift ? ItemCount - 1 : 1)) % ItemCount; break;
            case VK_SPACE: Activate(focus_); return;
            case VK_F4:
                if (focus_ == From || focus_ == To) Activate(focus_);
                return;
            case VK_UP:
            case VK_DOWN:
            case VK_LEFT:
            case VK_RIGHT: {
                const bool forward = key == VK_DOWN || key == VK_RIGHT;
                if (focus_ == OptAll || focus_ == OptRange) {
                    focus_ = forward ? OptRange : OptAll;
                    choice_.all = focus_ == OptAll;
                } else if (focus_ == From || focus_ == To) {
                    StepDay(focus_ == From, forward ? 1 : -1);
                } else if (focus_ == Cancel || focus_ == Export) {
                    focus_ = forward ? Export : Cancel;
                }
                break;
            }
            default: return;
        }
        Invalidate();
    }

    void Paint() {
        gfx::BufferedPaint paint(hwnd_);
        HDC dc = paint.dc();
        const Style& style = Style::Get();
        const Palette& pal = style.palette();
        const Fonts& fonts = style.fonts();
        const int radius = style.Px(8);
        gfx::Fill(dc, paint.rect(), pal.surface);

        gfx::Text(dc, Tr(S::ExportDialogPrompt), prompt_, fonts.semibold, pal.text);

        // Opciones.
        auto option = [&](int item, bool selected, const std::wstring& title, const std::wstring& detail) {
            const RECT& r = rects_[item];
            if (hover_ == item) gfx::FillRounded(dc, r, radius, pal.hover);
            if (focus_ == item) gfx::StrokeRounded(dc, r, radius, pal.accent, 1.5f);
            const int cx = r.left + style.Px(18), cy = detail.empty() ? (r.top + r.bottom) / 2 : r.top + style.Px(17);
            const int outer = style.Px(8);
            const RECT ring{cx - outer, cy - outer, cx + outer, cy + outer};
            gfx::StrokeRounded(dc, ring, outer, selected ? pal.accent : pal.textMuted, 1.5f);
            if (selected) gfx::FillCircle(dc, cx, cy, style.Px(4), pal.accent);
            const int textLeft = cx + outer + style.Px(12);
            if (detail.empty()) {
                gfx::Text(dc, title, {textLeft, r.top, r.right - style.Px(8), r.bottom}, fonts.normal, pal.text);
            } else {
                gfx::Text(dc, title, {textLeft, r.top + style.Px(6), r.right - style.Px(8), r.top + style.Px(28)},
                          fonts.normal, pal.text);
                gfx::Text(dc, detail, {textLeft, r.top + style.Px(26), r.right - style.Px(8), r.bottom - style.Px(4)},
                          fonts.small, pal.textMuted);
            }
        };
        std::wstring allDetail;
        if (!days_.empty())
            allDetail = util::Format(Tr(S::ExportAllDetail), ExportChoiceRecords(days_, ExportChoice{}),
                                     days_.front().label.c_str(), days_.back().label.c_str());
        option(OptAll, choice_.all, Tr(S::ExportAll), allDetail);
        option(OptRange, !choice_.all, Tr(S::ExportRangeOption), L"");

        // Campos de fecha (atenuados mientras está elegido "todo").
        const COLORREF fieldText = choice_.all ? pal.textMuted : pal.text;
        gfx::Text(dc, Tr(S::ExportFrom), fromLabel_, fonts.normal, fieldText);
        gfx::Text(dc, Tr(S::ExportTo), toLabel_, fonts.normal, fieldText);
        auto field = [&](int item, size_t index) {
            const RECT& r = rects_[item];
            gfx::FillRounded(dc, r, radius, hover_ == item ? pal.hover : pal.surfaceAlt);
            const bool focused = focus_ == item;
            gfx::StrokeRounded(dc, r, radius, focused ? pal.accent : pal.border, focused ? 1.5f : 1.0f);
            const std::wstring text = index < days_.size() ? days_[index].label : L"—";
            gfx::Text(dc, text, {r.left + style.Px(10), r.top, r.right - style.Px(28), r.bottom}, fonts.normal,
                      fieldText);
            gfx::Glyph(dc, glyph::ChevronDown, {r.right - style.Px(28), r.top, r.right - style.Px(6), r.bottom},
                       fonts.iconSmall, pal.textMuted);
        };
        field(From, choice_.from);
        field(To, choice_.to);
        const std::wstring count = choice_.all ? std::wstring(Tr(S::ExportRangeHint))
                                               : util::Format(Tr(S::ExportRangeCount),
                                                              ExportChoiceRecords(days_, choice_));
        gfx::Text(dc, count, count_, fonts.small, pal.textMuted);

        // Botones.
        auto button = [&](int item, const std::wstring& text, bool primary) {
            const RECT& r = rects_[item];
            COLORREF fill = primary ? pal.accent : (hover_ == item ? pal.hover : pal.surfaceAlt);
            gfx::FillRounded(dc, r, radius, fill);
            if (!primary) gfx::StrokeRounded(dc, r, radius, pal.border);
            if (focus_ == item) {
                const RECT ring = gfx::Inset(r, -style.Px(3), -style.Px(3));
                gfx::StrokeRounded(dc, ring, radius + style.Px(3), pal.accent, 1.5f);
            }
            gfx::Text(dc, text, r, primary ? fonts.semibold : fonts.normal, primary ? pal.accentText : pal.text,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        };
        button(Cancel, Tr(S::ExportCancel), false);
        button(Export, Tr(S::ExportButton), true);
    }

    const std::vector<ExportDay>& days_;
    ExportChoice choice_;
    RECT rects_[ItemCount]{};
    RECT prompt_{}, fromLabel_{}, toLabel_{}, count_{};
    int focus_ = Export;
    int hover_ = -1;
    bool done_ = false;
    bool result_ = false;
};

}  // namespace

bool ShowExportDialog(HWND owner, const std::vector<ExportDay>& days, ExportChoice& choice) {
    ExportChoice start = choice;
    NormalizeExportChoice(start, days.size(), true);
    ExportDialog dialog(days, start);
    if (!dialog.Run(owner)) return false;
    choice = dialog.choice();
    return true;
}

}  // namespace cola::ui
