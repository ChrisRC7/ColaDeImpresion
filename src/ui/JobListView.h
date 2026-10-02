#pragma once
// Lista de trabajos (control propio) en forma de tabla, como la cola de
// Windows: un encabezado con columnas y una fila por trabajo.
//   #  Documento            Estado     Usuario   Equipo   Páginas  Tamaño  Enviado
// Un clic en el encabezado de una columna ordenable avisa por callback
// (sortClicked) y la columna muestra la flecha del orden actual. Soporta
// selección múltiple (Ctrl/Shift + clic, Shift + flechas, arrastre con el botón
// izquierdo como en el Explorador), scroll con rueda y estado vacío. No conoce
// el spooler: recibe columnas y filas ya preparadas (ver app/RowBuilder), por
// eso sirve igual para la cola y para el historial. En ventanas angostas se
// ocultan primero las columnas de menor prioridad.

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "ui/Theme.h"
#include "ui/Window.h"

namespace cola::ui {

enum class Align { Left, Center, Right };

struct ListColumn {
    std::wstring label;
    int width = 0;          // en DIP; 0 = flexible (ocupa el espacio que sobre)
    Align align = Align::Left;  // del encabezado y de los valores
    bool pill = false;      // la celda se dibuja como insignia de estado (con el tono de la fila)
    bool emphasis = false;  // texto principal de la fila (semibold)
    int sortId = -1;        // identificador que se devuelve al hacer clic; -1 = no ordenable
    int hidePriority = 0;   // en ventanas angostas se ocultan primero las de número mayor (0 = nunca)
};

struct ListRow {
    std::wstring key;
    std::vector<std::wstring> cells;  // una por columna, en el mismo orden
    Tone tone = Tone::Neutral;
    wchar_t glyph = 0;  // ícono del estado (no se dibuja en la tabla: el estado tiene su columna)
};

class JobListView : public Window {
public:
    struct Callbacks {
        std::function<void()> selectionChanged;
        std::function<void(POINT screen)> contextMenu;
        std::function<void()> activate;            // doble clic / Enter
        std::function<void(int sortId)> sortClicked;  // clic en el encabezado de una columna
    };

    bool Create(HWND parent, Callbacks callbacks);

    void SetColumns(std::vector<ListColumn> columns);
    // Columna que muestra la flecha de orden (-1 = ninguna).
    void SetSortIndicator(int sortId, bool descending);

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
    // Posición horizontal de cada columna visible para un ancho dado.
    struct Slot {
        size_t column;
        int left;
        int right;
    };
    std::vector<Slot> Layout(int width) const;

    void Paint(HDC dc, const RECT& client);
    void PaintHeader(HDC dc, const RECT& client);
    void PaintRow(HDC dc, int index, const RECT& rowRect, const std::vector<Slot>& slots);
    void PaintEmpty(HDC dc, const RECT& client);
    int HeaderHeight() const;
    int RowHeight() const;
    int ViewHeight() const;          // alto visible para las filas (sin el encabezado)
    int RowAt(int clientY) const;    // -1 si no hay fila (o es el encabezado)
    int HeaderColumnAt(POINT pt) const;  // índice de columna bajo el punto en el encabezado, o -1
    int ContentHeight() const { return RowCount() * RowHeight(); }
    void UpdateScrollBar();
    void ScrollTo(int y);
    void EnsureVisible(int row);
    void SetFocusRow(int row, bool extend, bool toggle);
    void NotifySelection();
    void UpdateBandSelection();
    RECT BandRectClient() const;

    Callbacks callbacks_;
    std::vector<ListColumn> columns_;
    int sortId_ = -1;
    bool sortDescending_ = false;
    int hoverHeader_ = -1;
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
    POINT bandStart_{};          // en coordenadas del contenido (sin encabezado, con el scroll sumado)
    POINT bandEnd_{};
    std::set<std::wstring> bandBase_;  // selección previa (se conserva con Ctrl)
    std::wstring emptyTitle_;
    std::wstring emptyHint_;
    wchar_t emptyGlyph_ = 0;
};

}  // namespace cola::ui
