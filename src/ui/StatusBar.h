#pragma once
// Barra de estado inferior: total de páginas de la selección y resumen de la
// cola a la izquierda e indicadores (problemas de la impresora, permisos, modo
// en vivo/sondeo) a la derecha.
// Es solo de lectura (no responde al mouse ni al teclado): MainWindow le pasa
// los textos ya armados. El total de la selección es la decisión D44; el
// indicador "En vivo"/"Actualización por sondeo" refleja D5, y los errores de
// acciones sobre impresoras se informan aquí en lugar de en diálogos (D18).

#include <string>
#include <vector>

#include "ui/Theme.h"
#include "ui/Window.h"

namespace cola::ui {

// Un indicador de la parte derecha de la barra.
struct StatusItem {
    wchar_t glyph = 0;          // ícono (ui::glyph); 0 = sin ícono
    std::wstring text;          // texto del indicador
    Tone tone = Tone::Neutral;  // color: Neutral usa el texto atenuado, los demás su tono
};

// Barra de estado pintada a mano (D8), de alto fijo, en la parte inferior de
// la ventana principal.
class StatusBar : public Window {
public:
    // Crea la ventana hija (tamaño 0; MainWindow la ubica en su layout).
    bool Create(HWND parent);
    // Alto que debe darle el layout (30 DIP escalados al DPI actual).
    int PreferredHeight() const;

    // Texto de resumen de la cola (p. ej. "12 trabajos · 3 en pausa"). Solo
    // repinta si cambió (se llama con cada instantánea del monitor).
    void SetSummary(const std::wstring& text);
    // Total de la selección: se muestra primero y destacado (vacío = sin selección).
    void SetSelection(const std::wstring& text);
    // Reemplaza todos los indicadores de la derecha (en orden de izquierda a derecha).
    void SetItems(std::vector<StatusItem> items);
    // El tema o el DPI cambiaron: basta con repintar (todo sale de Style).
    void OnStyleChanged() { Invalidate(); }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    // Dibuja toda la barra en el DC del doble búfer.
    void Paint(HDC dc, const RECT& client);

    std::wstring summary_;           // resumen de la cola (izquierda, atenuado)
    std::wstring selection_;         // total de páginas de la selección (vacío = no se muestra)
    std::vector<StatusItem> items_;  // indicadores de la derecha
};

}  // namespace cola::ui
