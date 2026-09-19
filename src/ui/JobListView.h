#pragma once
// Lista de trabajos (control propio). Muestra filas de dos líneas:
//   ● Documento                                    3 págs   1.2 MB  [Imprimiendo]
//     usuario · equipo · 14:05 · impresora
// Soporta selección múltiple (Ctrl/Shift + clic, Shift + flechas, arrastre con
// el botón izquierdo como en el Explorador), scroll con rueda y estado vacío. No conoce el spooler: recibe filas ya preparadas
// (ver app/RowBuilder), por eso sirve igual para la cola y para el historial.

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "ui/Theme.h"
#include "ui/Window.h"

namespace cola::ui {

struct ListRow {
    std::wstring key;
    std::wstring title;
    std::wstring subtitle;
    std::wstring pages;
    std::wstring size;
    std::wstring status;
    Tone tone = Tone::Neutral;
    wchar_t glyph = 0;  // ícono de estado (0 = punto)
};

class JobListView : public Window {
public:
    struct Callbacks {
        std::function<void()> selectionChanged;
        std::function<void(POINT screen)> contextMenu;
        std::function<void()> activate;  // doble clic / Enter
    };

    bool Create(HWND parent, Callbacks callbacks);

    // Reemplaza las filas conservando la selección y el foco por clave.
    void SetRows(std::vector<ListRow> rows);
    void SetEmptyText(std::wstring title, std::wstring hint, wchar_t glyph);

    // delta: +1/-1 para mover; INT_MIN / INT_MAX para ir al primero / último.
    void MoveFocus(int delta, bool extendSelection);
    void SelectAll();
    void ClearSelection();
    bool HasSelection() const { return !selected_.empty(); }
    void SelectKey(const std::wstring& key);
    std::vector<std::wstring> SelectedKeys() const;  // en el orden de la lista
    std::wstring FocusedKey() const;
    int RowCount() const { return static_cast<int>(rows_.size()); }
    const std::vector<ListRow>& rows() const { return rows_; }

    void OnStyleChanged();

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    void Paint(HDC dc, const RECT& client);
    void PaintRow(HDC dc, int index, const RECT& rowRect);
    void PaintEmpty(HDC dc, const RECT& client);
    int RowHeight() const;
    int RowAt(int y) const;
    int ContentHeight() const { return RowCount() * RowHeight(); }
    void UpdateScrollBar();
    void ScrollTo(int y);
    void EnsureVisible(int row);
    void SetFocusRow(int row, bool extend, bool toggle);
    void NotifySelection();
    void UpdateBandSelection();
    RECT BandRectClient() const;

    Callbacks callbacks_;
    std::vector<ListRow> rows_;
    std::set<std::wstring> selected_;
    int focus_ = -1;
    int anchor_ = -1;
    int hover_ = -1;
    int scrollY_ = 0;
    bool trackingMouse_ = false;
    // Selección por arrastre (rectángulo, como en el Explorador de archivos).
    bool bandPressed_ = false;   // botón abajo, aún sin superar el umbral
    bool bandActive_ = false;    // arrastrando el rectángulo
    POINT bandStart_{};          // en coordenadas del contenido (con el scroll sumado)
    POINT bandEnd_{};
    std::set<std::wstring> bandBase_;  // selección previa (se conserva con Ctrl)
    std::wstring emptyTitle_;
    std::wstring emptyHint_;
    wchar_t emptyGlyph_ = 0;
};

}  // namespace cola::ui
