#include "ui/TrayIcon.h"

#include <shellapi.h>

namespace cola::ui {

bool TrayIcon::Add(HWND owner, HICON icon, const std::wstring& tooltip) {
    if (added_) return true;
    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = kCallbackMessage;
    data_.hIcon = icon;
    wcsncpy_s(data_.szTip, tooltip.c_str(), _TRUNCATE);
    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    if (added_) {
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
    if (!data_.hWnd) return;
    added_ = false;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    if (added_) Shell_NotifyIconW(NIM_SETVERSION, &data_);
}

void TrayIcon::SetTooltip(const std::wstring& tooltip) {
    if (!added_) return;
    data_.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcsncpy_s(data_.szTip, tooltip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::Notify(const std::wstring& title, const std::wstring& text, bool error) {
    if (!added_) return;
    data_.uFlags = NIF_INFO;
    wcsncpy_s(data_.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(data_.szInfo, text.c_str(), _TRUNCATE);
    data_.dwInfoFlags = (error ? NIIF_ERROR : NIIF_INFO) | NIIF_RESPECT_QUIET_TIME;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

UINT TrayIcon::TaskbarCreatedMessage() {
    static UINT message = RegisterWindowMessageW(L"TaskbarCreated");
    return message;
}

}  // namespace cola::ui
