#pragma once
// Primitivas de dibujo reutilizadas por los controles propios.
// Texto con GDI (ClearType); formas redondeadas con GDI+ (antialias).
//
// Todas las funciones reciben medidas ya en píxeles reales (los controles
// convierten antes con Style::Px) y colores COLORREF de la paleta. Ninguna
// deja objetos seleccionados en el DC: restauran lo que cambian.

#include <windows.h>

#include <string>

namespace cola::ui::gfx {

// Pintado con doble búfer: evita parpadeo. Uso típico en WM_PAINT:
//   gfx::BufferedPaint paint(hwnd);
//   Dibujar(paint.dc(), paint.rect());
// El constructor llama a BeginPaint y crea un mapa de bits del tamaño del área
// cliente; se dibuja todo en ese búfer y el destructor lo copia de una vez a
// la ventana (BitBlt) y llama a EndPaint. Así la pantalla nunca muestra un
// estado intermedio (fondo borrado sin el contenido encima).
class BufferedPaint {
public:
    explicit BufferedPaint(HWND hwnd);
    ~BufferedPaint();
    BufferedPaint(const BufferedPaint&) = delete;
    BufferedPaint& operator=(const BufferedPaint&) = delete;

    // DC de memoria donde se dibuja (ya en modo de fondo TRANSPARENT).
    HDC dc() const { return memDc_; }
    // Área cliente completa (origen 0,0), no solo la parte inválida.
    const RECT& rect() const { return client_; }

private:
    HWND hwnd_;                    // ventana que se pinta
    PAINTSTRUCT ps_{};             // datos de BeginPaint/EndPaint
    HDC windowDc_ = nullptr;       // DC real de la ventana (destino del BitBlt)
    HDC memDc_ = nullptr;          // DC de memoria (búfer)
    HBITMAP bitmap_ = nullptr;     // mapa de bits del búfer
    HGDIOBJ oldBitmap_ = nullptr;  // mapa de bits original de memDc_, para restaurarlo
    RECT client_{};                // área cliente al empezar a pintar
};

// Inicializa GDI+ una vez para todo el proceso (lo llama Application al
// arrancar, antes de crear ventanas).
void StartGdiPlus();
// Cierra GDI+ al salir (después de destruir las ventanas).
void StopGdiPlus();

// Rellena un rectángulo con un color sólido (sin crear pinceles).
void Fill(HDC dc, const RECT& rect, COLORREF color);
// Rellena un rectángulo de esquinas redondeadas (radio en píxeles) con antialias.
void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF color);
// Igual, pero translúcido (alpha 0..255): p. ej. el rectángulo de selección.
void FillRoundedAlpha(HDC dc, const RECT& rect, int radius, COLORREF color, int alpha);
// Dibuja el contorno de un rectángulo redondeado de `width` píxeles de grosor.
void StrokeRounded(HDC dc, const RECT& rect, int radius, COLORREF color, float width = 1.0f);
// Rellena un círculo de centro (cx, cy) y radio en píxeles (puntos de estado).
void FillCircle(HDC dc, int cx, int cy, int radius, COLORREF color);
// Línea horizontal de 1 píxel de alto entre x1 (incluido) y x2 (excluido).
void HLine(HDC dc, int x1, int x2, int y, COLORREF color);
// Línea vertical de 1 píxel de ancho entre y1 (incluido) y y2 (excluido).
void VLine(HDC dc, int x, int y1, int y2, COLORREF color);

// Dibuja texto con la fuente y color dados. 'format' son flags DT_*.
// El formato predeterminado es una línea centrada verticalmente, alineada a la
// izquierda, con "…" si no cabe y sin interpretar '&' como acelerador.
void Text(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color,
          UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
// Ancho y alto en píxeles de `text` en una sola línea con la fuente dada.
SIZE MeasureText(HDC dc, const std::wstring& text, HFONT font);
// Alto de un bloque de texto con ajuste de línea en el ancho dado.
int MeasureWrapped(HDC dc, const std::wstring& text, HFONT font, int width);

// Dibuja un glifo de la fuente de íconos centrado en rect.
void Glyph(HDC dc, wchar_t glyph, const RECT& rect, HFONT iconFont, COLORREF color);

// Ayudas geométricas sobre RECT.
inline int Width(const RECT& r) { return r.right - r.left; }
inline int Height(const RECT& r) { return r.bottom - r.top; }
// true si p está dentro de r (borde derecho e inferior excluidos, como PtInRect).
inline bool Contains(const RECT& r, POINT p) { return PtInRect(&r, p) != FALSE; }
// Rectángulo reducido dx píxeles a cada lado y dy arriba y abajo (negativos agrandan).
RECT Inset(const RECT& r, int dx, int dy);

}  // namespace cola::ui::gfx
