#pragma once
// Clase base para ventanas Win32 orientadas a objetos: registra la clase de
// ventana una vez y despacha los mensajes al método virtual HandleMessage.

#include <windows.h>

namespace cola::ui {

class Window {
public:
    Window() = default;
    virtual ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    HWND hwnd() const { return hwnd_; }
    void Invalidate() const {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void Show(bool visible) const {
        if (hwnd_) ShowWindow(hwnd_, visible ? SW_SHOWNA : SW_HIDE);
    }
    bool IsVisible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    void Move(const RECT& r) const {
        if (hwnd_) MoveWindow(hwnd_, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
    }

protected:
    // Crea la ventana; className se registra automáticamente la primera vez.
    bool CreateWin(const wchar_t* className, const wchar_t* title, DWORD style, DWORD exStyle, int x, int y, int w,
                   int h, HWND parent, HMENU menu = nullptr, HICON icon = nullptr);

    virtual LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT DefaultProc(UINT msg, WPARAM wParam, LPARAM lParam) { return DefWindowProcW(hwnd_, msg, wParam, lParam); }

    HWND hwnd_ = nullptr;

private:
    static LRESULT CALLBACK StaticProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

}  // namespace cola::ui
