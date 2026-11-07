#pragma once
// Panel de ayuda de atajos (F1). Lista todos los comandos con su combinación y
// permite reasignarlas: ↑/↓ o clic para elegir; Enter, un segundo clic, doble
// clic o clic sobre la tecla para reasignar (luego se presiona la nueva
// combinación); Supr para quitar, R para restaurar los predeterminados, Esc o
// clic fuera del panel para cerrar.
//
// Es una ventana hija que MainWindow coloca sobre el cuerpo (lista y panel de
// detalles): oscurece lo de atrás y dibuja una tarjeta centrada. No modifica
// los atajos por su cuenta: avisa con Callbacks y MainWindow los cambia en su
// ShortcutMap, los guarda en config.json y la lista se repinta leyendo el
// mapa (que recibe por puntero y no le pertenece).

#include <functional>
#include <string>

#include "input/Shortcuts.h"
#include "ui/Window.h"

namespace cola::ui {

class HelpOverlay : public Window {
public:
    // Acciones que el panel delega en MainWindow (la dueña de los atajos).
    struct Callbacks {
        // Asigna la combinación al comando.
        // Devuelve un mensaje (p. ej. conflicto) o vacío.
        std::function<std::wstring(input::Command, const input::KeyChord&)> assign;
        // Quita todas las combinaciones del comando (Supr / Retroceso).
        std::function<void(input::Command)> clear;
        // Vuelve todos los atajos a los predeterminados (tecla R).
        std::function<void()> resetDefaults;
        // Pide cerrar el panel (Esc, F1 o clic fuera de la tarjeta); MainWindow
        // lo oculta y devuelve el foco a la lista.
        std::function<void()> close;
    };

    // Crea la ventana hija, oculta y sin tamaño (MainWindow::Layout la ubica).
    // shortcuts debe vivir mientras exista el panel.
    bool Create(HWND parent, const input::ShortcutMap* shortcuts, Callbacks callbacks);

    // Muestra el panel encima de sus hermanas y le da el foco del teclado;
    // conserva la fila elegida de la vez anterior.
    void Open();
    // Lo oculta (y cancela una reasignación a medias).
    void Close();
    bool IsOpen() const { return IsVisible(); }
    // Procesa una tecla; devuelve true si la consumió.
    // Mientras está abierto el panel es modal: consume todas las teclas para
    // que ningún atajo de la app se dispare por detrás.
    bool HandleKey(UINT vk);
    // Cambió el tema, el idioma o el DPI: solo hace falta repintar.
    void OnStyleChanged() { Invalidate(); }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    // Fondo atenuado, tarjeta, título, filas de comandos, pie y barra de desplazamiento.
    void Paint(HDC dc, const RECT& client);
    // Rectángulo de la tarjeta: centrada, de hasta 620×640 px lógicos y
    // con al menos 16 px de margen alrededor.
    RECT CardRect(const RECT& client) const;
    int RowHeight() const;  // alto de una fila de comando (28 px lógicos)
    // Ajusta scroll_ para que la fila elegida quede a la vista.
    void EnsureVisible();
    int RowAt(POINT pt) const;  // índice de comando bajo el punto, o -1
    // Entra en modo "presione la nueva combinación" para la fila elegida.
    void StartCapture();
    RECT ScrollThumbRect() const;  // vacío si no hace falta desplazarse
    // Desplaza la lista para que firstRow sea la primera visible (acotado).
    void ScrollTo(int firstRow);
    bool OnScrollBar(POINT pt) const;  // el punto cae en la zona de la barra (más ancha que el pulgar)

    const input::ShortcutMap* shortcuts_ = nullptr;  // atajos actuales (de MainWindow)
    Callbacks callbacks_;
    int selected_ = 0;  // comando elegido (índice de input::Command)
    int scroll_ = 0;  // primera fila visible
    int visibleRows_ = 10;  // filas que caben (se recalcula al pintar)
    bool capturing_ = false;  // true = esperando la nueva combinación para selected_
    int hover_ = -1;  // fila bajo el mouse (-1 = ninguna)
    bool trackingMouse_ = false;  // ya se pidió WM_MOUSELEAVE con TrackMouseEvent
    RECT listArea_{};  // área de la lista (se calcula al pintar; se usa para el clic)
    bool draggingThumb_ = false;  // arrastrando el pulgar de la barra (con captura del mouse)
    int thumbGrabOffset_ = 0;  // distancia del clic al borde superior del pulgar
    int wheelRemainder_ = 0;  // giro de la rueda acumulado (ruedas finas y touchpads mandan menos de 120)
    // Mensaje del pie en lugar de la ayuda: la pregunta mientras se captura o
    // el resultado de la última asignación (p. ej. un conflicto).
    std::wstring message_;
};

}  // namespace cola::ui
