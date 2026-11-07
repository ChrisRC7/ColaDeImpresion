// Implementación de la clase base ui::Window: registro perezoso de la clase de
// ventana, creación con `this` como parámetro y despacho de mensajes desde el
// procedimiento estático al método virtual HandleMessage del objeto.

#include "ui/Window.h"

namespace cola::ui {

Window::~Window() {
    if (hwnd_) {
        // Primero se borra el puntero al objeto: durante DestroyWindow llegan
        // WM_DESTROY/WM_NCDESTROY y, como el destructor de la clase derivada ya
        // corrió, llamar a su HandleMessage sería usar un objeto destruido.
        // Con el puntero en 0, StaticProc los manda a DefWindowProcW.
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool Window::CreateWin(const wchar_t* className, const wchar_t* title, DWORD style, DWORD exStyle, int x, int y,
                       int w, int h, HWND parent, HMENU menu, HICON icon) {
    // Todas las clases se registran en el módulo del .exe.
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    // Registro perezoso: si la clase ya existe (otra instancia del mismo
    // control, o una ventana anterior) se reutiliza tal cual.
    if (!GetClassInfoExW(instance, className, &wc)) {
        wc = {};
        wc.cbSize = sizeof(wc);
        // CS_DBLCLKS: sin este estilo Windows no envía WM_LBUTTONDBLCLK (lo usan
        // la lista para abrir la ficha y el título de columna para autoajustar).
        // No se usa CS_HREDRAW/CS_VREDRAW: cada control repinta lo necesario.
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
    // `this` viaja como lpCreateParams: StaticProc lo recibe en WM_NCCREATE.
    // hwnd_ ya queda asignado allí, antes de que CreateWindowExW vuelva, así
    // los mensajes que llegan durante la creación (WM_CREATE, WM_SIZE...) ya
    // pueden usar hwnd_.
    hwnd_ = CreateWindowExW(exStyle, className, title, style, x, y, w, h, parent, menu, instance, this);
    return hwnd_ != nullptr;
}

// Implementación base: todo al procesamiento predeterminado de Windows.
LRESULT Window::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) { return DefaultProc(msg, wParam, lParam); }

LRESULT CALLBACK Window::StaticProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        // Primer mensaje con el que se puede asociar el objeto: se toma `this`
        // de CREATESTRUCTW y se guarda en los datos de usuario de la ventana.
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Window*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    // Mensajes previos a WM_NCCREATE (p. ej. WM_GETMINMAXINFO) o posteriores al
    // destructor no tienen objeto: van al procesamiento predeterminado.
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    LRESULT result = self->HandleMessage(msg, wParam, lParam);
    // WM_NCDESTROY es el último mensaje de la ventana: se desengancha el objeto
    // para que hwnd() devuelva nullptr si la ventana la destruyó Windows (p. ej.
    // al cerrarse el padre) y el destructor no intente destruirla otra vez.
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
    }
    return result;
}

}  // namespace cola::ui
