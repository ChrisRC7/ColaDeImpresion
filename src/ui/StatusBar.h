#pragma once
// Barra de estado inferior: total de páginas de la selección y resumen de la
// cola a la izquierda e indicadores (problemas de la impresora, permisos, modo
// en vivo/sondeo) a la derecha.

#include <string>
#include <vector>

#include "ui/Theme.h"
#include "ui/Window.h"

namespace cola::ui {

struct StatusItem {
    wchar_t glyph = 0;
    std::wstring text;
    Tone tone = Tone::Neutral;
};

class StatusBar : public Window {
public:
    bool Create(HWND parent);
    int PreferredHeight() const;

    void SetSummary(const std::wstring& text);
    // Total de la selección: se muestra primero y destacado (vacío = sin selección).
    void SetSelection(const std::wstring& text);
    void SetItems(std::vector<StatusItem> items);
    void OnStyleChanged() { Invalidate(); }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    void Paint(HDC dc, const RECT& client);

    std::wstring summary_;
    std::wstring selection_;
    std::vector<StatusItem> items_;
};

}  // namespace cola::ui
