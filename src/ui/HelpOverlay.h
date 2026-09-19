#pragma once
// Panel de ayuda de atajos (F1). Lista todos los comandos con su combinación y
// permite reasignarlas: ↑/↓ o clic para elegir; Enter, un segundo clic, doble
// clic o clic sobre la tecla para reasignar (luego se presiona la nueva
// combinación); Supr para quitar, R para restaurar los predeterminados, Esc o
// clic fuera del panel para cerrar.

#include <functional>
#include <string>

#include "input/Shortcuts.h"
#include "ui/Window.h"

namespace cola::ui {

class HelpOverlay : public Window {
public:
    struct Callbacks {
        // Devuelve un mensaje (p. ej. conflicto) o vacío.
        std::function<std::wstring(input::Command, const input::KeyChord&)> assign;
        std::function<void(input::Command)> clear;
        std::function<void()> resetDefaults;
        std::function<void()> close;
    };

    bool Create(HWND parent, const input::ShortcutMap* shortcuts, Callbacks callbacks);

    void Open();
    void Close();
    bool IsOpen() const { return IsVisible(); }
    // Procesa una tecla; devuelve true si la consumió.
    bool HandleKey(UINT vk);
    void OnStyleChanged() { Invalidate(); }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    void Paint(HDC dc, const RECT& client);
    RECT CardRect(const RECT& client) const;
    int RowHeight() const;
    void EnsureVisible();
    int RowAt(POINT pt) const;  // índice de comando bajo el punto, o -1
    void StartCapture();

    const input::ShortcutMap* shortcuts_ = nullptr;
    Callbacks callbacks_;
    int selected_ = 0;
    int scroll_ = 0;  // primera fila visible
    int visibleRows_ = 10;
    bool capturing_ = false;
    int hover_ = -1;
    bool trackingMouse_ = false;
    RECT listArea_{};  // área de la lista (se calcula al pintar; se usa para el clic)
    std::wstring message_;
};

}  // namespace cola::ui
