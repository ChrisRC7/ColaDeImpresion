#pragma once
// Panel de vista previa: datos del trabajo arriba, la página en el centro y
// navegación / zoom abajo. Zoom con Ctrl+rueda o atajos, arrastre para mover
// la página ampliada, rueda sin Ctrl para cambiar de página.
//
// Es el "panel de detalles" de la derecha (oculto al iniciar, D33). Tiene dos
// pestañas: la vista previa de las páginas (D6/D7: se arma en segundo plano
// por PreviewService a partir de los datos del trabajo) y la ficha
// "Configuración" con cada dato y su fiabilidad (D24, app/JobSheet). No
// conoce el spooler: MainWindow le pasa textos y el documento ya renderizable.
//
// Distribución vertical:
//   encabezado (título, líneas de detalle, pestañas) | área de página o ficha | pie de 44 DIP
//   (botones ‹ ›, "Página n de m", zoom; en la ficha, una leyenda).

#include <functional>
#include <string>
#include <vector>

#include "preview/PreviewTypes.h"
#include "ui/Theme.h"
#include "ui/Window.h"

namespace cola::ui {

// Fila de la ficha "Configuración": sección o campo con su valor, de dónde
// sale (detail) y una insignia de fiabilidad opcional.
struct InfoRow {
    // Section = título de grupo con línea debajo; Field = etiqueta + valor.
    enum class Kind { Section, Field };
    Kind kind = Kind::Field;
    std::wstring label;      // título de la sección o etiqueta del campo
    std::wstring value;      // valor del campo (vacío en secciones)
    std::wstring detail;     // origen del dato y, si no es fiable, el motivo
    std::wstring badge;      // "Fiable", "Estimado", "No determinable" (vacío = sin insignia)
    Tone badgeTone = Tone::Neutral;  // color de la insignia
};

// Panel de vista previa y ficha del trabajo seleccionado.
class PreviewPane : public Window {
public:
    // Crea la ventana hija. Devuelve false si falla.
    bool Create(HWND parent);

    // Qué muestra el panel: Empty = mensaje sin trabajo (nada seleccionado o
    // varios); Loading = trabajo elegido, vista previa en preparación;
    // Document = vista previa lista (con páginas o con un mensaje de por qué no).
    enum class Mode { Empty, Loading, Document };
    // Pestañas del panel: la vista previa de las páginas o la ficha con la
    // configuración de impresión del trabajo.
    enum class Tab { Preview, Sheet };

    // Reemplaza las filas de la ficha "Configuración".
    void SetSheet(std::vector<InfoRow> rows);
    // Cambia de pestaña (cancela un arrastre de la página en curso).
    void SetTab(Tab tab);
    // Alterna entre las dos pestañas (atajo de teclado).
    void ToggleTab() { SetTab(tab_ == Tab::Preview ? Tab::Sheet : Tab::Preview); }
    // Pestaña visible.
    Tab tab() const { return tab_; }

    // Modo vacío con un mensaje centrado; olvida documento, ficha y encabezado.
    void ShowEmpty(const std::wstring& message);
    // Modo "cargando" con el encabezado del trabajo (título y líneas de
    // detalle). Si es otro trabajo, vuelve a la página 1 y al zoom inicial.
    void ShowLoading(const std::wstring& title, const std::vector<std::wstring>& details);
    // Si doc es el mismo documento que ya se mostraba, conserva página y zoom.
    // (Se considera el mismo si coinciden título y formato: así un refresco
    // del mismo trabajo no devuelve al operador a la primera página.)
    void ShowDocument(const std::wstring& title, const std::vector<std::wstring>& details, preview::DocumentPtr doc);
    // Actualiza solo el encabezado (p. ej. cambió el estado del trabajo).
    void SetInfo(const std::wstring& title, const std::vector<std::wstring>& details);

    // Página siguiente/anterior; en la ficha, desplaza una pantalla (80 %).
    void NextPage();
    void PrevPage();
    // Zoom ×1,25 / ÷1,25 (entre 50 % y 600 %) y volver a "ajustar" (100 %).
    void ZoomIn();
    void ZoomOut();
    void ZoomReset();
    // Guarda el modo "vista rápida" y repinta (por ahora no cambia el dibujo).
    void SetQuickLook(bool quickLook);

    // Modo actual.
    Mode mode() const { return mode_; }
    // Documento mostrado (nulo fuera del modo Document).
    const preview::DocumentPtr& document() const { return doc_; }

    // Cambió el tema o el DPI: todo se recalcula al pintar.
    void OnStyleChanged() { Invalidate(); }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    // Pinta todo el panel según el modo y la pestaña.
    void Paint(HDC dc, const RECT& client);
    int PaintHeader(HDC dc, const RECT& client);  // devuelve el alto usado
    // Pie: navegación y zoom (vista previa) o leyenda (ficha).
    void PaintFooter(HDC dc, const RECT& footer);
    // Mensaje centrado con un glifo grande encima (si hay lugar).
    void PaintMessage(HDC dc, const RECT& area, wchar_t glyph, const std::wstring& text);
    int PaintTabs(HDC dc, const RECT& client, int top);  // devuelve el alto usado
    // Ficha "Configuración" con desplazamiento propio y su indicador.
    void PaintSheet(HDC dc, const RECT& area);
    // Desplaza la ficha `delta` px (positivo = hacia arriba, como la rueda).
    void ScrollSheet(int delta);
    // Área entre el encabezado y el pie (cliente), donde va la página o la ficha.
    RECT PageArea() const;
    // Rectángulo de la página actual dentro de `area`, con zoom y desplazamiento.
    RECT PageRect(const RECT& area) const;
    // Aplica un zoom (limitado) y corrige el desplazamiento.
    void SetZoom(double zoom);
    // Limita el desplazamiento para que la página ampliada no se pierda de vista.
    void ClampPan();
    // Páginas renderizables del documento (0 sin documento).
    int PageCount() const;

    Mode mode_ = Mode::Empty;
    preview::DocumentPtr doc_;          // documento con las páginas (modo Document)
    std::wstring title_;                // título del encabezado (nombre del documento)
    std::vector<std::wstring> details_; // líneas de detalle bajo el título
    std::wstring emptyMessage_;         // mensaje del modo vacío
    int page_ = 0;                      // página mostrada (índice desde 0)
    double zoom_ = 1.0;  // 1.0 = ajustar a la ventana
    POINT pan_{0, 0};                   // desplazamiento de la página ampliada respecto del centro (px)
    bool dragging_ = false;             // arrastrando la página con el mouse
    POINT dragStart_{};                 // posición del mouse al empezar a arrastrar
    POINT panStart_{};                  // pan_ al empezar a arrastrar
    // Suma de deltas de la rueda aún no convertidos en cambio de página (para
    // ratones/paneles de alta resolución que mandan fracciones de muesca).
    int wheelAccumulator_ = 0;
    bool quickLook_ = false;            // modo "vista rápida" (ver SetQuickLook)
    int headerHeight_ = 0;              // alto del encabezado del último pintado (lo usa PageArea)
    RECT prevButton_{}, nextButton_{};  // botones ‹ › del pie (vacíos si no se muestran)
    Tab tab_ = Tab::Preview;            // pestaña visible
    RECT tabPreview_{}, tabSheet_{};    // rectángulos de las pestañas (para los clics)
    std::vector<InfoRow> sheet_;        // filas de la ficha
    int sheetScroll_ = 0;      // desplazamiento vertical de la ficha (px)
    int sheetContent_ = 0;     // alto total de la ficha (px), calculado al pintar
};

}  // namespace cola::ui
