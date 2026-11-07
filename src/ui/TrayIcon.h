#pragma once
// Ícono en la bandeja del sistema: permite dejar la app corriendo en segundo
// plano ("modo silencioso") y mostrar notificaciones (globos / toasts).
// No reemplaza el ícono del spooler de Windows (eso no se puede, D14): es un
// ícono propio de la app. No es una ventana: los clics llegan a la ventana
// dueña (MainWindow) como el mensaje kCallbackMessage.

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace cola::ui {

// Envoltorio de Shell_NotifyIconW para un único ícono (uID = 1).
class TrayIcon {
public:
    // Mensaje que Windows envía a la ventana dueña ante eventos del ícono
    // (clic, menú contextual, globo cerrado...). Con NOTIFYICON_VERSION_4 el
    // evento va en LOWORD(lParam) y las coordenadas en wParam. WM_APP + 50
    // queda lejos de los WM_APP + 1..3 que usan los hilos (CONTEXTO.md §3.2).
    static constexpr UINT kCallbackMessage = WM_APP + 50;

    // Quita el ícono al destruirse (si no, quedaría un ícono "fantasma" en la
    // bandeja hasta que el usuario pase el mouse por encima).
    ~TrayIcon() { Remove(); }

    // Agrega el ícono asociado a `owner` con el ícono y la descripción
    // emergente dados. Devuelve true si quedó agregado (o ya lo estaba).
    bool Add(HWND owner, HICON icon, const std::wstring& tooltip);
    // Quita el ícono de la bandeja (sin efecto si no estaba).
    void Remove();
    // Cambia el texto que aparece al pasar el mouse (máx. 127 caracteres; se trunca).
    void SetTooltip(const std::wstring& tooltip);
    // Muestra una notificación (globo en Windows 10/11 como toast) con título
    // y texto; `error` usa el ícono de error en lugar del de información.
    void Notify(const std::wstring& title, const std::wstring& text, bool error);
    // true si el ícono está actualmente en la bandeja.
    bool IsAdded() const { return added_; }

    // Tras reiniciarse Explorer hay que volver a agregar el ícono.
    // Devuelve el id del mensaje registrado "TaskbarCreated", que Explorer
    // difunde a todas las ventanas de nivel superior al recrear la barra de tareas.
    static UINT TaskbarCreatedMessage();
    // Vuelve a agregar el ícono con los datos guardados (al recibir TaskbarCreated).
    void Readd();

private:
    // Datos del ícono; se conservan para modificarlo y para Readd.
    NOTIFYICONDATAW data_{};
    bool added_ = false;  // si el ícono está en la bandeja
};

}  // namespace cola::ui
