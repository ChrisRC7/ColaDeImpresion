#pragma once
// Clase base para ventanas Win32 orientadas a objetos: registra la clase de
// ventana una vez y despacha los mensajes al método virtual HandleMessage.
//
// Es la base de todos los controles propios de `ui/` (D8: la interfaz se pinta
// a mano con GDI/GDI+ para controlar los colores del modo oscuro). Cada control
// deriva de Window, llama a CreateWin en su Create() y sobrescribe
// HandleMessage para atender WM_PAINT, el mouse, el teclado, etc. (ver
// docs/GUIA_IA.md, "Agregar un control de UI"). El puntero al objeto C++ viaja
// en GWLP_USERDATA de la ventana, así un único procedimiento estático
// (StaticProc) sirve a todas las clases.

#include <windows.h>

namespace cola::ui {

// Envoltorio mínimo de un HWND con propiedad: el objeto crea la ventana y la
// destruye en su destructor. No es copiable porque dos objetos no pueden ser
// dueños de la misma ventana (y el HWND guarda un puntero a `this`).
class Window {
public:
    Window() = default;
    // Destruye la ventana si todavía existe (antes desengancha el puntero a
    // `this` para que los mensajes de destrucción no lleguen a un objeto a
    // medio destruir).
    virtual ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Ventana nativa (nullptr si todavía no se creó o ya se destruyó).
    HWND hwnd() const { return hwnd_; }
    // Pide repintar toda la ventana. bErase = FALSE: los controles pintan todo
    // su fondo con doble búfer, borrar antes solo produciría parpadeo.
    void Invalidate() const {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }
    // Muestra u oculta la ventana. SW_SHOWNA muestra sin activarla, para no
    // quitarle el foco del teclado a la ventana que lo tenga (p. ej. la lista).
    void Show(bool visible) const {
        if (hwnd_) ShowWindow(hwnd_, visible ? SW_SHOWNA : SW_HIDE);
    }
    // true si la ventana existe y está visible (también exige que sus padres lo estén).
    bool IsVisible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    // Mueve/redimensiona la ventana al rectángulo r (en coordenadas del padre)
    // y la repinta; lo usa el layout de MainWindow.
    void Move(const RECT& r) const {
        if (hwnd_) MoveWindow(hwnd_, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
    }

protected:
    // Crea la ventana; className se registra automáticamente la primera vez.
    // Parámetros: los de CreateWindowExW (title, style, exStyle, posición y
    // tamaño en píxeles, ventana padre, menú o id de control) más el ícono de
    // la clase (solo se usa al registrarla). Devuelve false si falla el
    // registro o la creación. Se pasa `this` como lpCreateParams para que
    // StaticProc lo asocie al HWND en WM_NCCREATE.
    bool CreateWin(const wchar_t* className, const wchar_t* title, DWORD style, DWORD exStyle, int x, int y, int w,
                   int h, HWND parent, HMENU menu = nullptr, HICON icon = nullptr);

    // Procesa un mensaje de la ventana. Las clases derivadas la sobrescriben y
    // llaman a DefaultProc para lo que no atienden. Devuelve el LRESULT del mensaje.
    virtual LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    // Procesamiento predeterminado de Windows (DefWindowProcW) para esta ventana.
    LRESULT DefaultProc(UINT msg, WPARAM wParam, LPARAM lParam) { return DefWindowProcW(hwnd_, msg, wParam, lParam); }

    // Ventana nativa. Se asigna en WM_NCCREATE (antes de que CreateWindowExW
    // vuelva) y se pone en nullptr en WM_NCDESTROY.
    HWND hwnd_ = nullptr;

private:
    // Procedimiento de ventana común a todas las clases: recupera el objeto
    // Window desde GWLP_USERDATA y le reenvía el mensaje.
    static LRESULT CALLBACK StaticProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

}  // namespace cola::ui
