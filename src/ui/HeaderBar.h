#pragma once
// Barra superior: selector de impresora, cuadro de búsqueda, los botones de
// pausa (trabajos seleccionados e impresora, junto a la búsqueda), en el
// historial el botón "Exportar a Excel", y botones de acción (detalles, historial, ayuda, tema, acerca de, menú). El cuadro de texto también sirve para
// escribir el nombre de un servidor ("modo servidor", Ctrl+O).
//
// Distribución (de izquierda a derecha):
//   [Impresora v] [Buscar...] [Pausar trabajos] [Pausar impresora] [Exportar]
//   ... y a la derecha los botones de ícono: Detalles, Historial, Ayuda,
//   Tema, Acerca de y Menú.
// Todo se pinta a mano (D8) salvo el texto de la búsqueda, que es un EDIT
// nativo hijo de la barra (el borde redondeado y la lupa son dibujo propio).
// Los botones de pausa son la decisión D37; Exportar, D43; Detalles, D33. En
// modo servidor el mismo EDIT pide el nombre del servidor a conectar o el
// servidor del LAIMI (D54); MainWindow decide qué hacer con el texto al pulsar Enter.

#include <functional>
#include <string>

#include "ui/Window.h"

namespace cola::ui {

// Identificador de cada botón de la barra. Sirve también de índice en
// HeaderBar::buttons_ (Count_ = cantidad) y, + 1, de id de la descripción
// emergente. Printer = selector de impresora (abre el menú de colas);
// History = alternar cola/historial; Help = panel de atajos; Theme = claro/
// oscuro; Menu = menú principal "…".

// Details = mostrar u ocultar el panel de detalles (vista previa y configuración).
// PauseJobs / PausePrinter = pausar o reanudar los trabajos seleccionados / la impresora.
// Export = exportar el historial a Excel (solo visible en el historial).
// About = ventana "Acerca de" (autores).
enum class HeaderButton { Printer, History, Help, Theme, Menu, Details, About, PauseJobs, PausePrinter, Export, Count_ };

// Estado de un botón de pausa: qué hará al pulsarlo y su descripción.
struct PauseButtonState {
    bool resume = false;   // true: "Reanudar" (todo está en pausa); false: "Pausar"
    bool enabled = false;  // sin trabajos seleccionados / sin impresoras queda gris
    std::wstring tooltip;  // descripción con el atajo de teclado
    // Comparación miembro a miembro: evita repintar si MainWindow manda el mismo estado.
    bool operator==(const PauseButtonState&) const = default;
};

// Barra superior de la ventana principal (alto fijo de 56 DIP).
class HeaderBar : public Window {
public:
    // Eventos hacia MainWindow.
    struct Callbacks {
        // Se pulsó un botón (clic completo sobre el mismo botón). screenRect =
        // su rectángulo en pantalla, para abrir un menú justo debajo.
        std::function<void(HeaderButton button, RECT screenRect)> clicked;
        // Cambió el texto de búsqueda (en cada tecla; no en modo servidor).
        std::function<void(const std::wstring& text)> searchChanged;
    };

    // Crea la barra, el EDIT de búsqueda y el control de descripciones
    // emergentes (una "herramienta" por botón). Devuelve false si falla.
    bool Create(HWND parent, Callbacks callbacks);
    // Alto que debe darle el layout de MainWindow (56 DIP escalados).
    int PreferredHeight() const;

    // Texto del selector de impresora: título (nombre de la cola o "Todas"),
    // subtítulo (servidor/estado; vacío = una sola línea centrada) y `warning`
    // para mostrarlo con ícono y color de advertencia (impresora con problemas).
    void SetPrinterLabel(const std::wstring& title, const std::wstring& subtitle, bool warning);
    // Marca el botón Historial como activo y muestra/oculta "Exportar a Excel".
    void SetHistoryActive(bool active);
    // Marca el botón Detalles como activo (panel abierto) y cambia su ícono.
    void SetDetailsActive(bool active);
    // Estado de los dos botones de pausa (qué harán, si están habilitados y su descripción).
    void SetPauseButtons(const PauseButtonState& jobs, const PauseButtonState& printer);
    // Descripción del botón Exportar (incluye el atajo actual).
    void SetExportTooltip(const std::wstring& text);

    // Da el foco al cuadro de búsqueda y selecciona todo su texto (Ctrl+F).
    void FocusSearch();
    // true si el foco del teclado está en el cuadro de texto.
    bool IsEditFocused() const { return GetFocus() == edit_; }
    // Texto actual del cuadro (búsqueda o nombre de servidor según el modo).
    std::wstring EditText() const;
    // Vacía la búsqueda (sin efecto en modo servidor); dispara searchChanged.
    void ClearSearch();

    // Modo servidor: el cuadro pide el nombre del servidor. placeholder = texto
    // de ayuda propio (por defecto, el de "Conectar a servidor").
    // `current` = texto inicial (p. ej. el servidor actual). La búsqueda que
    // había se guarda y vuelve con EndServerInput.
    void BeginServerInput(const std::wstring& current, const std::wstring& placeholder = L"");
    // Sale del modo servidor y restaura la búsqueda anterior (Enter o Esc).
    void EndServerInput();
    // true mientras el cuadro pide un servidor.
    bool InServerMode() const { return serverMode_; }

    // Rectángulo de un botón en coordenadas de pantalla (para ubicar menús).
    RECT ButtonScreenRect(HeaderButton button) const;
    // Cambió el tema, el DPI o el idioma: fuente y tema del EDIT, textos de
    // ayuda, descripciones, distribución y repintado.
    void OnStyleChanged();

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    // Calcula la posición de cada botón, del cuadro de búsqueda y del EDIT
    // según el ancho actual (ver el comentario de la distribución arriba).
    void Layout();
    // Dibuja toda la barra en el DC del doble búfer.
    void Paint(HDC dc, const RECT& client);
    // Texto gris de ayuda dentro del EDIT vacío (EM_SETCUEBANNER), según el modo.
    void UpdateCueBanner();
    // Actualiza rectángulo y texto de cada descripción emergente.
    void UpdateTooltips();
    // Índice (HeaderButton) del botón bajo el punto (coordenadas cliente), o -1.
    int HitTest(POINT pt) const;
    // Dibuja un botón de pausa (ícono + texto, o dos íconos si no hay espacio).
    void PaintPauseButton(HDC dc, HeaderButton id, const PauseButtonState& state);
    // Ancho de un botón de pausa con el texto más largo de sus dos estados (o solo el ícono).
    int PauseButtonWidth(HeaderButton id) const;
    // Estado del botón de pausa `id` (PauseJobs o PausePrinter).
    const PauseButtonState& PauseState(HeaderButton id) const;
    // Ancho del botón Exportar (0 fuera del historial; solo ícono si no hay espacio).
    int ExportButtonWidth() const;
    // Dibuja el botón "Exportar a Excel" (si ocupa lugar).
    void PaintExportButton(HDC dc);

    Callbacks callbacks_;
    HWND edit_ = nullptr;      // EDIT nativo de búsqueda / servidor (hijo de la barra)
    HWND tooltip_ = nullptr;   // control de descripciones emergentes (tooltips_class32)
    RECT buttons_[static_cast<int>(HeaderButton::Count_)] = {};  // rectángulos (cliente) por botón
    RECT searchBox_{};         // marco dibujado del cuadro de búsqueda (ancho 0 = oculto)
    std::wstring printerTitle_;     // título del selector de impresora
    std::wstring printerSubtitle_;  // subtítulo del selector (vacío = una línea)
    bool printerWarning_ = false;   // selector en modo advertencia
    bool historyActive_ = false;    // se está viendo el historial
    bool detailsActive_ = false;    // el panel de detalles está abierto
    PauseButtonState pauseJobs_;    // estado del botón "pausar trabajos"
    PauseButtonState pausePrinter_; // estado del botón "pausar impresora"
    std::wstring exportTooltip_;    // descripción del botón Exportar
    bool pauseLabels_ = true;  // con poco espacio los botones de pausa muestran solo el ícono
    bool serverMode_ = false;       // el cuadro pide un servidor en vez de buscar
    std::wstring serverPlaceholder_;  // texto de ayuda propio del modo servidor ("" = el general)
    std::wstring savedSearch_;      // búsqueda guardada al entrar en modo servidor
    int hover_ = -1;                // botón bajo el mouse (-1 = ninguno)
    int pressed_ = -1;              // botón donde se presionó (el clic vale si se suelta en el mismo)
    bool trackingMouse_ = false;    // TrackMouseEvent pedido (para WM_MOUSELEAVE)
    // true mientras se cambia el texto del EDIT por código: el EN_CHANGE que
    // eso genera no debe avisarse como búsqueda del operador.
    bool suppressChange_ = false;
};

}  // namespace cola::ui
