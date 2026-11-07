#pragma once
// Barra de acceso rápido a impresoras: una fila de botones ("chips") con las
// impresoras que el operador fijó, más "Todas". Cada chip muestra cuántos
// trabajos tiene la impresora y si está en pausa o con problemas.
//
// No conoce el spooler: recibe los chips ya preparados (app/RowBuilder) y avisa
// por callbacks qué impresora se eligió.
//
// Decisión D19 de docs/CONTEXTO.md: las impresoras fijadas se guardan en
// config.json (`favoritePrinters`) y Ctrl+1…Ctrl+9 eligen el chip por posición
// (eso lo resuelve MainWindow, no esta barra). Si los chips no caben, la barra
// se desplaza en horizontal con la rueda del mouse; el botón "+" queda fijo.

#include <functional>
#include <string>
#include <vector>

#include "ui/Theme.h"
#include "ui/Window.h"

namespace cola::ui {

// Un chip de la barra (una impresora fijada o "Todas").
struct PrinterChip {
    std::wstring name;     // nombre completo de la impresora ("*" = todas)
    std::wstring label;    // texto visible
    unsigned jobs = 0;     // trabajos en la cola de esa impresora
    unsigned held = 0;     // de esos, retenidos por la app
    Tone tone = Tone::Neutral;  // color del punto de estado (problema de la impresora)
};

// Barra de chips de impresoras (40 DIP de alto, bajo la cabecera). Se crea
// oculta: MainWindow la muestra según `showPrinterBar`.
class PrinterBar : public Window {
public:
    // Eventos hacia MainWindow.
    struct Callbacks {
        // Clic en un chip: `printer` = PrinterChip::name ("*" = todas).
        std::function<void(const std::wstring& printer)> selected;
        // Clic derecho en un chip (p. ej. para desfijarlo); `screen` = posición del mouse.
        std::function<void(const std::wstring& printer, POINT screen)> contextMenu;
        std::function<void(POINT screen)> configure;  // botón "+" (elegir impresoras fijadas)
    };

    // Crea la ventana hija (sin WS_VISIBLE). Devuelve false si falla.
    bool Create(HWND parent, Callbacks callbacks);
    // Alto que debe darle el layout (40 DIP escalados).
    int PreferredHeight() const;

    // Reemplaza los chips y marca cuál está activo.
    // `active` = nombre de la impresora que se está viendo ("*" para "Todas").
    void SetChips(std::vector<PrinterChip> chips, const std::wstring& active);
    // true si no hay chips (MainWindow puede ocultar la barra).
    bool Empty() const { return chips_.empty(); }
    // Cambió el tema o el DPI: recalcula tamaños (dependen de la fuente) y repinta.
    void OnStyleChanged();

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    // Calcula el rectángulo de cada chip (con el desplazamiento aplicado) y el del botón "+".
    void Layout();
    // Dibuja la barra en el DC del doble búfer.
    void Paint(HDC dc, const RECT& client);
    int HitTest(POINT pt) const;  // índice de chip, -2 = botón "+", -1 = nada
    // Desplaza los chips `delta` px (positivo = hacia la derecha del contenido).
    void ScrollBy(int delta);

    Callbacks callbacks_;
    std::vector<PrinterChip> chips_;  // chips en el orden en que se muestran
    std::vector<RECT> rects_;  // posiciones ya desplazadas por scrollX_
    RECT configButton_{};      // botón "+" (fijo a la derecha)
    std::wstring active_;      // nombre de la impresora activa
    int hover_ = -1;           // chip (o kConfigButton) bajo el mouse; -1 = nada
    int pressed_ = -1;         // dónde se presionó; el clic vale si se suelta en el mismo
    int scrollX_ = 0;          // desplazamiento horizontal de los chips (px)
    int contentWidth_ = 0;     // ancho total de los chips con márgenes (px)
    bool trackingMouse_ = false;  // TrackMouseEvent pedido (para WM_MOUSELEAVE)
};

}  // namespace cola::ui
