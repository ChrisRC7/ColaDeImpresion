#pragma once
// Lista de trabajos (control propio) en forma de tabla, como la cola de
// Windows: un encabezado con columnas y una fila por trabajo.
//   #  Documento            Estado     Usuario   Equipo   Páginas  Tamaño  Enviado
// Un clic en el encabezado de una columna ordenable avisa por callback
// (sortClicked) y la columna muestra la flecha del orden actual. Soporta
// selección múltiple (Ctrl/Shift + clic, Shift + flechas, arrastre con el botón
// izquierdo como en el Explorador), scroll con rueda y estado vacío. No conoce
// el spooler: recibe columnas y filas ya preparadas (ver app/RowBuilder), por
// eso sirve igual para la cola y para el historial. Nunca se oculta una
// columna: si no caben, aparece la barra horizontal.
// Como en la cola de Windows, el ancho de cada columna se cambia arrastrando
// el borde derecho de su título (doble clic en el borde = ajustar al
// contenido); si las columnas no caben aparece una barra horizontal. Los
// anchos elegidos se avisan por callback para guardarlos.
// También como en Windows, arrastrar el título de una columna la mueve a otra
// posición (una línea marca dónde va a quedar); el orden nuevo se avisa por
// callback para guardarlo. Un clic sin arrastrar sigue ordenando.

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ui/ListColumns.h"
#include "ui/Theme.h"
#include "ui/Window.h"

namespace cola::ui {

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
        // El operador cambió el ancho de una columna (id, DIP); -1 = volver al predeterminado.
        std::function<void(const std::wstring& id, int width)> columnResized;
        std::function<void(POINT screen)> headerMenu;  // clic derecho en el encabezado
        // El operador arrastró una columna: ids de las columnas de esta vista en el orden nuevo.
        std::function<void(const std::vector<std::wstring>& order)> columnMoved;
    };

    bool Create(HWND parent, Callbacks callbacks);

    void SetColumns(std::vector<ListColumn> columns);
    // Anchos elegidos por el operador (id -> DIP).
    void SetColumnWidths(std::map<std::wstring, int> widths);
    // Orden de presentación de las columnas (ids; ver ui::NormalizeColumnOrder).
    void SetColumnOrder(std::vector<std::wstring> order);
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
    using Slot = ColumnSlot;
    // Columnas visibles en coordenadas del cliente (con el desplazamiento horizontal aplicado).
    std::vector<Slot> Layout(int width) const;
    std::vector<Slot> LayoutUnscrolled(int width) const;  // en el orden de presentación
    int ContentWidth(int width) const;  // ancho total de las columnas (px)
    int ClientWidth() const;

    void Paint(HDC dc, const RECT& client);
    void PaintHeader(HDC dc, const RECT& client);
    void PaintRow(HDC dc, int index, const RECT& rowRect, const std::vector<Slot>& slots);
    void PaintEmpty(HDC dc, const RECT& client);
    int HeaderHeight() const;
    int RowHeight() const;
    int ViewHeight() const;          // alto visible para las filas (sin el encabezado)
    int RowAt(int clientY) const;    // -1 si no hay fila (o es el encabezado)
    int HeaderColumnAt(POINT pt) const;  // índice de columna bajo el punto en el encabezado, o -1
    int DividerAt(POINT pt) const;       // columna cuyo borde derecho está bajo el punto, o -1
    int FitWidth(size_t column) const;   // ancho (DIP) que necesita el contenido de una columna
    void ScrollToX(int x);
    int ContentHeight() const { return RowCount() * RowHeight(); }
    void UpdateScrollBar();
    void ScrollTo(int y);  // desplazamiento vertical
    void EnsureVisible(int row);
    void SetFocusRow(int row, bool extend, bool toggle);
    void NotifySelection();
    void UpdateBandSelection();
    RECT BandRectClient() const;
    // Arrastre de columnas: dónde quedaría la columna si se suelta en x
    // (columna de referencia y si va después de ella) y la x de la línea que lo marca.
    bool DropTarget(int x, size_t& anchor, bool& after, int& lineX) const;
    void FinishColumnDrag(bool apply);
    void PaintColumnDrag(HDC dc, const RECT& client);

    Callbacks callbacks_;
    std::vector<ListColumn> columns_;
    std::vector<std::wstring> order_;  // orden de presentación (ids)
    std::vector<size_t> display_;      // índices de columns_ en el orden de presentación
    int sortId_ = -1;
    bool sortDescending_ = false;
    int hoverHeader_ = -1;
    std::vector<ListRow> rows_;
    std::set<std::wstring> selected_;
    int focus_ = -1;
    int anchor_ = -1;
    int hover_ = -1;
    int scrollY_ = 0;
    int scrollX_ = 0;
    std::map<std::wstring, int> widths_;  // anchos del operador (id -> DIP)
    // Cambio de ancho con el mouse.
    int hoverDivider_ = -1;     // columna cuyo borde está bajo el mouse
    int resizing_ = -1;         // columna que se está redimensionando
    int resizeStartX_ = 0;      // x del mouse al empezar (px)
    int resizeStartWidth_ = 0;  // ancho de la columna al empezar (px)
    // Clic o arrastre del título de una columna.
    int headerPressed_ = -1;    // columna presionada (aún puede ser un clic para ordenar)
    bool dragging_ = false;     // superó el umbral: se está moviendo la columna
    int dragStartX_ = 0;        // x del mouse al presionar (px, cliente)
    int dragX_ = 0;             // x actual del mouse (px, cliente)
    int dragGrabOffset_ = 0;    // distancia del mouse al borde izquierdo del título
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
