#include "ui/Window.h"

namespace cola::ui {

Window::~Window() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool Window::CreateWin(const wchar_t* className, const wchar_t* title, DWORD style, DWORD exStyle, int x, int y,
                       int w, int h, HWND parent, HMENU menu, HICON icon) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    if (!GetClassInfoExW(instance, className, &wc)) {
        wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = StaticProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;  // pintamos todo nosotros (evita parpadeo)
        wc.lpszClassName = className;
        wc.hIcon = icon;
        wc.hIconSm = icon;
        if (!RegisterClassExW(&wc)) return false;
    }
    hwnd_ = CreateWindowExW(exStyle, className, title, style, x, y, w, h, parent, menu, instance, this);
    return hwnd_ != nullptr;
}

LRESULT Window::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) { return DefaultProc(msg, wParam, lParam); }

LRESULT CALLBACK Window::StaticProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Window*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    LRESULT result = self->HandleMessage(msg, wParam, lParam);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
    }
    return result;
}

}  // namespace cola::ui
