#pragma once
// Barra de acceso rápido a impresoras: una fila de botones ("chips") con las
// impresoras que el operador fijó, más "Todas". Cada chip muestra cuántos
// trabajos tiene la impresora y si está en pausa o con problemas.
//
// No conoce el spooler: recibe los chips ya preparados (app/RowBuilder) y avisa
// por callbacks qué impresora se eligió.

#include <functional>
#include <string>
#include <vector>

#include "ui/Theme.h"
#include "ui/Window.h"

namespace cola::ui {

struct PrinterChip {
    std::wstring name;     // nombre completo de la impresora ("*" = todas)
    std::wstring label;    // texto visible
    unsigned jobs = 0;     // trabajos en la cola de esa impresora
    unsigned held = 0;     // de esos, retenidos por la app
    Tone tone = Tone::Neutral;  // color del punto de estado (problema de la impresora)
};

class PrinterBar : public Window {
public:
    struct Callbacks {
        std::function<void(const std::wstring& printer)> selected;
        std::function<void(const std::wstring& printer, POINT screen)> contextMenu;
        std::function<void(POINT screen)> configure;  // botón "+" (elegir impresoras fijadas)
    };

    bool Create(HWND parent, Callbacks callbacks);
    int PreferredHeight() const;

    // Reemplaza los chips y marca cuál está activo.
    void SetChips(std::vector<PrinterChip> chips, const std::wstring& active);
    bool Empty() const { return chips_.empty(); }
    void OnStyleChanged();

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    void Layout();
    void Paint(HDC dc, const RECT& client);
    int HitTest(POINT pt) const;  // índice de chip, -2 = botón "+", -1 = nada
    void ScrollBy(int delta);

    Callbacks callbacks_;
    std::vector<PrinterChip> chips_;
    std::vector<RECT> rects_;  // posiciones ya desplazadas por scrollX_
    RECT configButton_{};
    std::wstring active_;
    int hover_ = -1;
    int pressed_ = -1;
    int scrollX_ = 0;
    int contentWidth_ = 0;
    bool trackingMouse_ = false;
};

}  // namespace cola::ui
