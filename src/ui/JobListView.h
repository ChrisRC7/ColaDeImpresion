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
//
// Decisiones: D30 (tabla y orden por columna), D36 (anchos), D38 (orden de
// columnas por arrastre), D40 (barra horizontal en vez de ocultar).
// La implementación está repartida en JobListView.cpp (filas, selección,
// desplazamiento y mensajes) y JobListHeader.cpp (encabezado).
//
// Sistemas de coordenadas usados en el código:
//  - "cliente": píxeles de la ventana (0,0 = esquina superior izquierda,
//    incluye el encabezado).
//  - "contenido": y medida desde la primera fila (sin el encabezado) y con el
//    desplazamiento vertical sumado: fila i ocupa [i*RowHeight, (i+1)*RowHeight).
//    La selección por rectángulo se guarda así para que siga a las filas al
//    desplazarse.
//  - Columnas: ColumnSlot en px sin desplazar (LayoutUnscrolled) o con el
//    desplazamiento horizontal ya restado (Layout).

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ui/ListColumns.h"
#include "ui/Theme.h"
#include "ui/Window.h"

namespace cola::ui {

// Una fila de la tabla, ya con los textos listos para mostrar.
struct ListRow {
    // Clave estable del trabajo (impresora + id, o id del registro del
    // historial): la selección y el foco se recuerdan por clave, no por
    // índice, para sobrevivir a los refrescos que reordenan la lista.
    std::wstring key;
    std::vector<std::wstring> cells;  // una por columna, en el mismo orden
    Tone tone = Tone::Neutral;        // tono del estado: color de la insignia (columnas `pill`)
    wchar_t glyph = 0;  // ícono del estado (no se dibuja en la tabla: el estado tiene su columna)
};

// Tabla de trabajos o del historial: control propio pintado con doble búfer.
// Se comunica con MainWindow solo por los callbacks.
class JobListView : public Window {
public:
    // Eventos que la tabla avisa a su dueño. Todos son opcionales.
    struct Callbacks {
        // Cambió la selección o la fila con foco (para la vista previa y el total de páginas).
        std::function<void()> selectionChanged;
        // Menú contextual pedido sobre las filas (clic derecho, tecla Menú o
        // Shift+F10); `screen` = dónde mostrarlo, en coordenadas de pantalla.
        std::function<void(POINT screen)> contextMenu;
        std::function<void()> activate;            // doble clic / Enter
        std::function<void(int sortId)> sortClicked;  // clic en el encabezado de una columna
        // El operador cambió el ancho de una columna (id, DIP); -1 = volver al predeterminado.
        std::function<void(const std::wstring& id, int width)> columnResized;
        std::function<void(POINT screen)> headerMenu;  // clic derecho en el encabezado
        // El operador arrastró una columna: ids de las columnas de esta vista en el orden nuevo.
        std::function<void(const std::vector<std::wstring>& order)> columnMoved;
    };

    // Crea la ventana hija (con barras de desplazamiento y WS_TABSTOP para
    // recibir el foco del teclado) y aplica el estilo actual. Devuelve false si falla.
    bool Create(HWND parent, Callbacks callbacks);

    // Define las columnas (cambia al pasar de la cola al historial, de una
    // impresora a "Todas" o de idioma). Las filas deben traer una celda por columna.
    void SetColumns(std::vector<ListColumn> columns);
    // Anchos elegidos por el operador (id -> DIP).
    void SetColumnWidths(std::map<std::wstring, int> widths);
    // Orden de presentación de las columnas (ids; ver ui::NormalizeColumnOrder).
    void SetColumnOrder(std::vector<std::wstring> order);
    // Columna que muestra la flecha de orden (-1 = ninguna).
    // `descending` elige la flecha hacia abajo.
    void SetSortIndicator(int sortId, bool descending);

    // Reemplaza las filas conservando la selección y el foco por clave.
    void SetRows(std::vector<ListRow> rows);
    // Mensaje cuando no hay filas: título, explicación y glifo grande (ui::glyph).
    void SetEmptyText(std::wstring title, std::wstring hint, wchar_t glyph);

    // delta: +1/-1 para mover; INT_MIN / INT_MAX para ir al primero / último.
    // extendSelection = Shift: selecciona el rango desde el ancla hasta la fila nueva.
    void MoveFocus(int delta, bool extendSelection);
    // Selecciona todas las filas (Ctrl+A).
    void SelectAll();
    // Quita la selección (Esc o clic en el vacío).
    void ClearSelection();
    // true si hay al menos una fila seleccionada.
    bool HasSelection() const { return !selected_.empty(); }
    // Selecciona solo la fila con esa clave, la enfoca y la hace visible (sin efecto si no existe).
    void SelectKey(const std::wstring& key);
    std::vector<std::wstring> SelectedKeys() const;  // en el orden de la lista
    // Clave de la fila con foco ("" si no hay).
    std::wstring FocusedKey() const;
    // Cantidad de filas.
    int RowCount() const { return static_cast<int>(rows_.size()); }
    // Filas actuales (en el orden en que se muestran).
    const std::vector<ListRow>& rows() const { return rows_; }

    // Cambió el tema o el DPI: actualiza las barras de desplazamiento nativas
    // (modo oscuro, D9), sus rangos (el alto de fila depende del DPI) y repinta.
    void OnStyleChanged();

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    using Slot = ColumnSlot;
    // Columnas visibles en coordenadas del cliente (con el desplazamiento horizontal aplicado).
    std::vector<Slot> Layout(int width) const;
    std::vector<Slot> LayoutUnscrolled(int width) const;  // en el orden de presentación
    int ContentWidth(int width) const;  // ancho total de las columnas (px)
    // Ancho del área cliente (px; sin las barras de desplazamiento).
    int ClientWidth() const;

    // Pinta todo: fondo, filas visibles, rectángulo de selección, encabezado
    // y, si se arrastra un título, la columna fantasma.
    void Paint(HDC dc, const RECT& client);
    // Pinta el encabezado (títulos, separadores, flecha de orden). JobListHeader.cpp.
    void PaintHeader(HDC dc, const RECT& client);
    // Pinta la fila `index` en rowRect (coordenadas cliente) con las columnas `slots`.
    void PaintRow(HDC dc, int index, const RECT& rowRect, const std::vector<Slot>& slots);
    // Pinta el estado vacío (glifo, título y explicación centrados).
    void PaintEmpty(HDC dc, const RECT& client);
    // Alto del encabezado (30 DIP; 0 si no hay columnas).
    int HeaderHeight() const;
    // Alto de cada fila (40 DIP: filas "de tarjeta", cómodas para el mouse).
    int RowHeight() const;
    int ViewHeight() const;          // alto visible para las filas (sin el encabezado)
    int RowAt(int clientY) const;    // -1 si no hay fila (o es el encabezado)
    int HeaderColumnAt(POINT pt) const;  // índice de columna bajo el punto en el encabezado, o -1
    int DividerAt(POINT pt) const;       // columna cuyo borde derecho está bajo el punto, o -1
    int FitWidth(size_t column) const;   // ancho (DIP) que necesita el contenido de una columna
    // Desplazamiento horizontal a x px (se limita al rango válido).
    void ScrollToX(int x);
    // Alto total de todas las filas (px, sin el encabezado).
    int ContentHeight() const { return RowCount() * RowHeight(); }
    // Recalcula rango, página y posición de ambas barras de desplazamiento y
    // las muestra u oculta según haga falta.
    void UpdateScrollBar();
    void ScrollTo(int y);  // desplazamiento vertical
    // Desplaza lo mínimo para que la fila `row` quede entera a la vista.
    void EnsureVisible(int row);
    // Mueve el foco a `row` y actualiza la selección: extend = Shift (rango
    // desde el ancla), toggle = Ctrl (alterna solo esa fila); ninguno =
    // selección simple. Hace visible la fila y avisa del cambio.
    void SetFocusRow(int row, bool extend, bool toggle);
    // Llama al callback selectionChanged (si existe).
    void NotifySelection();
    // Recalcula la selección a partir del rectángulo de arrastre.
    void UpdateBandSelection();
    // Rectángulo de selección por arrastre en coordenadas cliente (normalizado).
    RECT BandRectClient() const;
    // Arrastre de columnas: dónde quedaría la columna si se suelta en x
    // (columna de referencia y si va después de ella) y la x de la línea que lo marca.
    bool DropTarget(int x, size_t& anchor, bool& after, int& lineX) const;
    // Termina el clic/arrastre de un título: libera la captura y, si `apply` y
    // hubo arrastre, aplica el nuevo orden y avisa con columnMoved.
    void FinishColumnDrag(bool apply);
    // Pinta la columna arrastrada: su lugar de origen, la línea de destino y
    // el título "fantasma" que sigue al mouse.
    void PaintColumnDrag(HDC dc, const RECT& client);

    Callbacks callbacks_;              // eventos hacia MainWindow
    std::vector<ListColumn> columns_;  // definición de columnas (orden fijo de RowBuilder)
    std::vector<std::wstring> order_;  // orden de presentación (ids)
    std::vector<size_t> display_;      // índices de columns_ en el orden de presentación
    int sortId_ = -1;                  // sortId de la columna con la flecha de orden (-1 = ninguna)
    bool sortDescending_ = false;      // la flecha apunta hacia abajo
    int hoverHeader_ = -1;             // columna (ordenable) cuyo título está bajo el mouse
    std::vector<ListRow> rows_;        // filas actuales
    std::set<std::wstring> selected_;  // claves de las filas seleccionadas
    int focus_ = -1;                   // fila con el foco del teclado (-1 = ninguna)
    int anchor_ = -1;                  // fila de origen de los rangos con Shift
    int hover_ = -1;                   // fila bajo el mouse (-1 = ninguna)
    int scrollY_ = 0;                  // desplazamiento vertical (px de contenido ocultos arriba)
    int scrollX_ = 0;                  // desplazamiento horizontal (px ocultos a la izquierda)
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
    // true mientras TrackMouseEvent está pedido (para recibir WM_MOUSELEAVE y
    // borrar el resaltado al salir el mouse); se pide una vez por entrada.
    bool trackingMouse_ = false;
    // Selección por arrastre (rectángulo, como en el Explorador de archivos).
    bool bandPressed_ = false;   // botón abajo, aún sin superar el umbral
    bool bandActive_ = false;    // arrastrando el rectángulo
    POINT bandStart_{};          // en coordenadas del contenido (sin encabezado, con el scroll sumado)
    POINT bandEnd_{};            // esquina opuesta (mismas coordenadas; sigue al mouse)
    std::set<std::wstring> bandBase_;  // selección previa (se conserva con Ctrl)
    std::wstring emptyTitle_;    // título del estado vacío
    std::wstring emptyHint_;     // explicación del estado vacío
    wchar_t emptyGlyph_ = 0;     // glifo del estado vacío (0 = sin glifo)
};

}  // namespace cola::ui
