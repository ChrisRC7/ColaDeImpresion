// Barra de estado inferior: dibujo de la selección, el resumen y los
// indicadores, con el reparto de espacio entre ellos.

#include "ui/StatusBar.h"

#include <algorithm>

#include "ui/Gfx.h"
#include "ui/Style.h"

namespace cola::ui {

bool StatusBar::Create(HWND parent) {
    // WS_CLIPSIBLINGS: no pinta encima de ventanas hermanas que se superpongan
    // (p. ej. el panel de ayuda, que cubre toda la ventana principal).
    return CreateWin(L"ColaStatusBar", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, 0, parent);
}

int StatusBar::PreferredHeight() const { return Style::Get().Px(30); }

void StatusBar::SetSummary(const std::wstring& text) {
    // Se evita repintar si nada cambió: el monitor manda instantáneas cada
    // pocos segundos aunque la cola esté igual.
    if (text == summary_) return;
    summary_ = text;
    Invalidate();
}

void StatusBar::SetSelection(const std::wstring& text) {
    if (text == selection_) return;
    selection_ = text;
    Invalidate();
}

void StatusBar::SetItems(std::vector<StatusItem> items) {
    items_ = std::move(items);
    Invalidate();
}

void StatusBar::Paint(HDC dc, const RECT& client) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    // Fondo y línea divisoria superior que la separa de la lista.
    gfx::Fill(dc, client, pal.surfaceAlt);
    gfx::HLine(dc, client.left, client.right, client.top, pal.border);

    const int pad = style.Px(14);
    // La selección va primero a la izquierda y tiene prioridad sobre los indicadores.
    // leftEdge = primer x libre para lo que sigue (resumen e indicadores).
    int leftEdge = client.left + pad;
    if (!selection_.empty()) {
        // Ancho natural = ícono (16 DIP + 4 de separación) + texto, sin pasar
        // del borde derecho de la barra.
        const SIZE sz = gfx::MeasureText(dc, selection_, style.fonts().semibold);
        const int right = std::min<int>(client.right - pad, leftEdge + style.Px(20) + sz.cx);
        gfx::Glyph(dc, glyph::Document, RECT{leftEdge, client.top, leftEdge + style.Px(16), client.bottom},
                   style.fonts().iconSmall, pal.accent);
        gfx::Text(dc, selection_, RECT{leftEdge + style.Px(20), client.top, right, client.bottom}, style.fonts().semibold,
                  pal.text);
        leftEdge = right + style.Px(18);
    }
    // `right` = borde derecho libre; cada indicador lo va corriendo a la izquierda.
    int right = client.right - pad;
    // Indicadores de derecha a izquierda.
    // Se recorren al revés para que el último quede pegado al borde derecho y
    // los demás a su izquierda, en el mismo orden en que se pasaron.
    for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
        SIZE sz = gfx::MeasureText(dc, it->text, style.fonts().small);
        int w = sz.cx + (it->glyph ? style.Px(20) : 0);
        // Ningún indicador empieza antes de leftEdge + 200 DIP: así el resumen
        // (o al menos su comienzo) siempre tiene lugar; un indicador que no
        // cabe entero se corta con "…" (DT_END_ELLIPSIS de gfx::Text).
        int left = std::max<int>(leftEdge + style.Px(200), right - w);
        // Sin espacio: este y los indicadores restantes (más a la izquierda) se omiten.
        if (left >= right) break;
        COLORREF color = it->tone == Tone::Neutral ? pal.textMuted : pal.ToneColor(it->tone);
        int textLeft = left;
        if (it->glyph) {
            gfx::Glyph(dc, it->glyph, RECT{left, client.top, left + style.Px(16), client.bottom}, style.fonts().iconSmall, color);
            textLeft += style.Px(20);
        }
        gfx::Text(dc, it->text, RECT{textLeft, client.top, right, client.bottom}, style.fonts().small, color);
        // 18 DIP de separación con el siguiente indicador.
        right = left - style.Px(18);
    }
    // El resumen ocupa lo que quede entre la selección y los indicadores.
    if (right > leftEdge)
        gfx::Text(dc, summary_, RECT{leftEdge, client.top, right, client.bottom}, style.fonts().small, pal.textMuted);
}

LRESULT StatusBar::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            // Doble búfer: el destructor de `paint` copia el resultado a la ventana.
            gfx::BufferedPaint paint(hwnd_);
            Paint(paint.dc(), paint.rect());
            return 0;
        }
        // El fondo se pinta en WM_PAINT; devolver 1 ("ya borrado") evita que
        // Windows lo borre antes y se vea un parpadeo.
        case WM_ERASEBKGND: return 1;
        // El reparto de espacio depende del ancho: repintar todo al cambiar de tamaño.
        case WM_SIZE: Invalidate(); return 0;
    }
    return Window::HandleMessage(msg, wParam, lParam);
}

}  // namespace cola::ui
