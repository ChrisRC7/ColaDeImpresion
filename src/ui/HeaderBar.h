#pragma once
// Barra superior: selector de impresora, cuadro de búsqueda y botones de
// acción (historial, ayuda, tema, menú). El cuadro de texto también sirve para
// escribir el nombre de un servidor ("modo servidor", Ctrl+O).

#include <functional>
#include <string>

#include "ui/Window.h"

namespace cola::ui {

enum class HeaderButton { Printer, History, Help, Theme, Menu, Count_ };

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

    Callbacks callbacks_;
    HWND edit_ = nullptr;
    HWND tooltip_ = nullptr;
    RECT buttons_[static_cast<int>(HeaderButton::Count_)] = {};
    RECT searchBox_{};
    std::wstring printerTitle_;
    std::wstring printerSubtitle_;
    bool printerWarning_ = false;
    bool historyActive_ = false;
    bool serverMode_ = false;
    std::wstring savedSearch_;
    int hover_ = -1;
    int pressed_ = -1;
    bool trackingMouse_ = false;
    bool suppressChange_ = false;
};

}  // namespace cola::ui
