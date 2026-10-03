#pragma once
// Barra superior: selector de impresora, cuadro de búsqueda, los botones de
// pausa (trabajos seleccionados e impresora, junto a la búsqueda) y botones de
// acción (detalles, historial, ayuda, tema, menú). El cuadro de texto también sirve para
// escribir el nombre de un servidor ("modo servidor", Ctrl+O).

#include <functional>
#include <string>

#include "ui/Window.h"

namespace cola::ui {

// Details = mostrar u ocultar el panel de detalles (vista previa y configuración).
// PauseJobs / PausePrinter = pausar o reanudar los trabajos seleccionados / la impresora.
enum class HeaderButton { Printer, History, Help, Theme, Menu, Details, PauseJobs, PausePrinter, Count_ };

// Estado de un botón de pausa: qué hará al pulsarlo y su descripción.
struct PauseButtonState {
    bool resume = false;   // true: "Reanudar" (todo está en pausa); false: "Pausar"
    bool enabled = false;  // sin trabajos seleccionados / sin impresoras queda gris
    std::wstring tooltip;  // descripción con el atajo de teclado
    bool operator==(const PauseButtonState&) const = default;
};

class HeaderBar : public Window {
public:
    struct Callbacks {
        std::function<void(HeaderButton button, RECT screenRect)> clicked;
        std::function<void(const std::wstring& text)> searchChanged;
    };

    bool Create(HWND parent, Callbacks callbacks);
    int PreferredHeight() const;

    void SetPrinterLabel(const std::wstring& title, const std::wstring& subtitle, bool warning);
    void SetHistoryActive(bool active);
    void SetDetailsActive(bool active);
    void SetPauseButtons(const PauseButtonState& jobs, const PauseButtonState& printer);

    void FocusSearch();
    bool IsEditFocused() const { return GetFocus() == edit_; }
    std::wstring EditText() const;
    void ClearSearch();

    // Modo servidor: el cuadro pide el nombre del servidor.
    void BeginServerInput(const std::wstring& current);
    void EndServerInput();
    bool InServerMode() const { return serverMode_; }

    RECT ButtonScreenRect(HeaderButton button) const;
    void OnStyleChanged();

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    void Layout();
    void Paint(HDC dc, const RECT& client);
    void UpdateCueBanner();
    void UpdateTooltips();
    int HitTest(POINT pt) const;
    void PaintPauseButton(HDC dc, HeaderButton id, const PauseButtonState& state);
    // Ancho de un botón de pausa con el texto más largo de sus dos estados (o solo el ícono).
    int PauseButtonWidth(HeaderButton id) const;
    const PauseButtonState& PauseState(HeaderButton id) const;

    Callbacks callbacks_;
    HWND edit_ = nullptr;
    HWND tooltip_ = nullptr;
    RECT buttons_[static_cast<int>(HeaderButton::Count_)] = {};
    RECT searchBox_{};
    std::wstring printerTitle_;
    std::wstring printerSubtitle_;
    bool printerWarning_ = false;
    bool historyActive_ = false;
    bool detailsActive_ = false;
    PauseButtonState pauseJobs_;
    PauseButtonState pausePrinter_;
    bool pauseLabels_ = true;  // con poco espacio los botones de pausa muestran solo el ícono
    bool serverMode_ = false;
    std::wstring savedSearch_;
    int hover_ = -1;
    int pressed_ = -1;
    bool trackingMouse_ = false;
    bool suppressChange_ = false;
};

}  // namespace cola::ui
