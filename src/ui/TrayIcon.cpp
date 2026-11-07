// Implementación del ícono de bandeja con Shell_NotifyIconW.

#include "ui/TrayIcon.h"

#include <shellapi.h>

namespace cola::ui {

bool TrayIcon::Add(HWND owner, HICON icon, const std::wstring& tooltip) {
    if (added_) return true;
    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    // Identificador fijo: la app tiene un solo ícono; (hWnd, uID) lo identifica
    // en las llamadas siguientes.
    data_.uID = 1;
    // NIF_SHOWTIP: con NOTIFYICON_VERSION_4 la descripción emergente estándar
    // solo aparece si se pide explícitamente.
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = kCallbackMessage;
    data_.hIcon = icon;
    // szTip tiene tamaño fijo: se trunca en lugar de fallar.
    wcsncpy_s(data_.szTip, tooltip.c_str(), _TRUNCATE);
    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    if (added_) {
        // Comportamiento moderno (Vista+): eventos NIN_* y coordenadas del clic.
        data_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data_);
    }
    return added_;
}

void TrayIcon::Remove() {
    if (!added_) return;
    Shell_NotifyIconW(NIM_DELETE, &data_);
    added_ = false;
}

void TrayIcon::Readd() {
    // Nunca se agregó (no hay ventana dueña): nada que restaurar.
    if (!data_.hWnd) return;
    // Explorer se reinició y perdió todos los íconos: el nuestro ya no existe
    // aunque added_ diga lo contrario.
    added_ = false;
    // Se restauran las banderas completas porque NIF_INFO u otras pudieron
    // quedar en data_ de la última modificación.
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    // uVersion sigue en NOTIFYICON_VERSION_4 desde Add.
    if (added_) Shell_NotifyIconW(NIM_SETVERSION, &data_);
}

void TrayIcon::SetTooltip(const std::wstring& tooltip) {
    if (!added_) return;
    // Solo se modifica la descripción (las banderas dicen qué campos leer).
    data_.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcsncpy_s(data_.szTip, tooltip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::Notify(const std::wstring& title, const std::wstring& text, bool error) {
    if (!added_) return;
    // NIF_INFO muestra la notificación; en Windows 10/11 aparece como toast.
    data_.uFlags = NIF_INFO;
    wcsncpy_s(data_.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(data_.szInfo, text.c_str(), _TRUNCATE);
    // NIIF_RESPECT_QUIET_TIME: no molestar en la primera hora tras iniciar
    // sesión por primera vez ni con "no molestar" activo.
    data_.dwInfoFlags = (error ? NIIF_ERROR : NIIF_INFO) | NIIF_RESPECT_QUIET_TIME;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

UINT TrayIcon::TaskbarCreatedMessage() {
    // Se registra una sola vez (estática local); el id es el mismo en todo el
    // sistema para el mismo nombre.
    static UINT message = RegisterWindowMessageW(L"TaskbarCreated");
    return message;
}

}  // namespace cola::ui
