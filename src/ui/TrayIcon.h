#pragma once
// Ícono en la bandeja del sistema: permite dejar la app corriendo en segundo
// plano ("modo silencioso") y mostrar notificaciones (globos / toasts).

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace cola::ui {

class TrayIcon {
public:
    static constexpr UINT kCallbackMessage = WM_APP + 50;

    ~TrayIcon() { Remove(); }

    bool Add(HWND owner, HICON icon, const std::wstring& tooltip);
    void Remove();
    void SetTooltip(const std::wstring& tooltip);
    void Notify(const std::wstring& title, const std::wstring& text, bool error);
    bool IsAdded() const { return added_; }

    // Tras reiniciarse Explorer hay que volver a agregar el ícono.
    static UINT TaskbarCreatedMessage();
    void Readd();

private:
    NOTIFYICONDATAW data_{};
    bool added_ = false;
};

}  // namespace cola::ui
