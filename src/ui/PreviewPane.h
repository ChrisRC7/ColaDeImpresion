#pragma once
// Panel de vista previa: datos del trabajo arriba, la página en el centro y
// navegación / zoom abajo. Zoom con Ctrl+rueda o atajos, arrastre para mover
// la página ampliada, rueda sin Ctrl para cambiar de página.

#include <functional>
#include <string>
#include <vector>

#include "preview/PreviewTypes.h"
#include "ui/Window.h"

namespace cola::ui {

class PreviewPane : public Window {
public:
    bool Create(HWND parent);

    enum class Mode { Empty, Loading, Document };

    void ShowEmpty(const std::wstring& message);
    void ShowLoading(const std::wstring& title, const std::vector<std::wstring>& details);
    // Si doc es el mismo documento que ya se mostraba, conserva página y zoom.
    void ShowDocument(const std::wstring& title, const std::vector<std::wstring>& details, preview::DocumentPtr doc);
    // Actualiza solo el encabezado (p. ej. cambió el estado del trabajo).
    void SetInfo(const std::wstring& title, const std::vector<std::wstring>& details);

    void NextPage();
    void PrevPage();
    void ZoomIn();
    void ZoomOut();
    void ZoomReset();
    void SetQuickLook(bool quickLook);

    Mode mode() const { return mode_; }
    const preview::DocumentPtr& document() const { return doc_; }

    void OnStyleChanged() { Invalidate(); }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    void Paint(HDC dc, const RECT& client);
    int PaintHeader(HDC dc, const RECT& client);  // devuelve el alto usado
    void PaintFooter(HDC dc, const RECT& footer);
    void PaintMessage(HDC dc, const RECT& area, wchar_t glyph, const std::wstring& text);
    RECT PageArea() const;
    RECT PageRect(const RECT& area) const;
    void SetZoom(double zoom);
    void ClampPan();
    int PageCount() const;

    Mode mode_ = Mode::Empty;
    preview::DocumentPtr doc_;
    std::wstring title_;
    std::vector<std::wstring> details_;
    std::wstring emptyMessage_;
    int page_ = 0;
    double zoom_ = 1.0;  // 1.0 = ajustar a la ventana
    POINT pan_{0, 0};
    bool dragging_ = false;
    POINT dragStart_{};
    POINT panStart_{};
    int wheelAccumulator_ = 0;
    bool quickLook_ = false;
    int headerHeight_ = 0;
    RECT prevButton_{}, nextButton_{};
};

}  // namespace cola::ui
