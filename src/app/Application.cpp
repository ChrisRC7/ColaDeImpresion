// Implementación del arranque de la aplicación (ver Application.h): lectura de
// argumentos, instancia única con un mutex con nombre, inicialización de los
// subsistemas de Windows que usa la UI y bucle de mensajes principal (con el
// filtro de atajos de MainWindow::PreTranslate). Todo corre en el hilo de la
// UI; los hilos de fondo los crea y destruye la propia MainWindow.
#include "app/Application.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

#include "app/MainWindow.h"
#include "config/Paths.h"
#include "preview/TempFiles.h"
#include "ui/DarkMode.h"
#include "ui/Gfx.h"
#include "util/Log.h"

namespace cola::app {

namespace {

// Nombre del mutex de instancia única. "Local\" lo limita a la sesión de
// Windows actual: en un servidor con varios operadores conectados por
// Escritorio remoto cada sesión puede tener su propia copia.
constexpr wchar_t kMutexName[] = L"Local\\ColaDeImpresion.SingleInstance";

// Resultado del análisis de la línea de comandos.
struct Arguments {
    // Opciones que se le pasan a MainWindow::Create (bandeja, servidor, LAIMI).
    StartupOptions startup;
    // true si es la instancia relanzada por "Reiniciar como administrador":
    // en vez de delegar en la instancia abierta, espera a que esta cierre.
    bool elevatedRestart = false;
    // Solo para desarrollo: permite probar una compilación nueva mientras la
    // versión de producción sigue abierta en el mismo equipo (con otra
    // configuración, p. ej. modo portable en la carpeta de compilación).
    bool additionalInstance = false;
};

// Lee los argumentos del proceso (GetCommandLineW, en UTF-16, así que acepta
// nombres de servidor con acentos). Los desconocidos se ignoran en silencio;
// los que esperan un valor (--config, --server) se saltan si es el último.
// Efecto colateral: --config fija en ese momento la ruta de config.json
// (config::SetSettingsFileOverride), antes de que nadie lea la configuración.
Arguments ParseArguments() {
    Arguments args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    // argv[0] es el ejecutable; si CommandLineToArgvW falla (argv nulo) no
    // se procesa nada y quedan los valores por defecto.
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--tray") args.startup.startInTray = true;
        else if (a == L"--elevated-restart") args.elevatedRestart = true;
        else if (a == L"--instancia-adicional") args.additionalInstance = true;
        else if (a == L"--config" && i + 1 < argc) config::SetSettingsFileOverride(argv[++i]);
        else if (a == L"--laimi") args.startup.laimi = true;
        else if (a == L"--server" && i + 1 < argc) {
            // hasServer distingue "--server """ (este equipo) de no haber
            // pasado el argumento (se usa el último servidor de config.json).
            args.startup.server = argv[++i];
            args.startup.hasServer = true;
        }
    }
    // CommandLineToArgvW reserva el arreglo con LocalAlloc.
    if (argv) LocalFree(argv);
    return args;
}

}  // namespace

int RunApplication() {
    const Arguments args = ParseArguments();

    // Instancia única: si ya hay una, se le pide que se muestre y salimos.
    // CreateMutexW con bInitialOwner = TRUE: si el mutex es nuevo, esta
    // instancia queda como dueña hasta el final; si ya existía, se obtiene un
    // handle al existente (sin ser dueño) y GetLastError lo indica.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS && !args.additionalInstance) {
        if (args.elevatedRestart) {
            // Reinicio como administrador: esperar a que la instancia anterior cierre.
            // Al cerrar, la anterior libera el mutex y esta lo adquiere
            // (WAIT_ABANDONED si terminó sin liberarlo: también sirve). Si en
            // 15 s no cerró, se abandona para no quedar dos copias abiertas.
            DWORD wait = WaitForSingleObject(mutex, 15000);
            if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
                CloseHandle(mutex);
                return 1;
            }
        } else {
            // Caso normal (p. ej. doble clic en el acceso directo con la app
            // en la bandeja): se busca la ventana por su clase y se le envía
            // el mensaje registrado para que se muestre y pase al frente.
            // PostMessage (no Send) para no quedar colgados si está ocupada.
            HWND existing = FindWindowW(MainWindow::kClassName, nullptr);
            if (existing) PostMessageW(existing, MainWindow::ShowInstanceMessage(), 0, 0);
            CloseHandle(mutex);
            return 0;
        }
    }

    // El log se inicia recién aquí: una instancia que solo delega en la otra
    // no escribe en app.log (evita líneas de "inicio" sin "fin").
    util::LogInit(config::LogFile());
    util::LogInfo(L"Inicio de ColaDeImpresion %ls", COLA_VERSION_STRING);
    // Copias temporales de documentos que hayan quedado de una ejecución anterior.
    // (Pueden ser documentos confidenciales de estudiantes, D27.)
    preview::CleanupStaleTempFiles();

    // Clases estándar (EDIT del cuadro de búsqueda) y de barras; el resto de
    // los controles son propios (D8).
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);
    // COM en modo STA para el hilo de la UI (diálogos del Shell, Shell
    // notify, etc.). Se guarda el HRESULT para desinicializar solo si tuvo éxito.
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ui::gfx::StartGdiPlus();
    // Carga dinámica de las funciones no documentadas de uxtheme (D9); si no
    // existen, el modo oscuro de menús y barras simplemente no se aplica.
    ui::InitDarkModeSupport();

    int exitCode = 0;
    // Bloque propio para que la MainWindow (y con ella todos sus hilos de
    // fondo) se destruya antes de apagar GDI+, COM y la carpeta temporal.
    {
        MainWindow window;
        if (!window.Create(args.startup)) {
            util::LogError(L"No se pudo crear la ventana principal: %lu", GetLastError());
            exitCode = 1;
        } else {
            // Bucle de mensajes principal. GetMessageW devuelve 0 con WM_QUIT
            // y -1 si hay error: en ambos casos se sale (por eso "> 0").
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                // PreTranslate resuelve los atajos de teclado configurables
                // (input/Shortcuts) antes de que el mensaje llegue al control
                // con el foco; si lo consumió, no se despacha.
                if (window.PreTranslate(msg)) continue;
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            // wParam de WM_QUIT = valor pasado a PostQuitMessage.
            exitCode = static_cast<int>(msg.wParam);
        }
    }

    // La ventana (y sus hilos de vista previa) ya se destruyó: nada usa la carpeta temporal.
    preview::RemovePrivateTempDir();
    ui::gfx::StopGdiPlus();
    if (SUCCEEDED(com)) CoUninitialize();
    util::LogInfo(L"Fin de ColaDeImpresion");
    // Se libera el mutex al final de todo: un reinicio elevado que lo está
    // esperando arranca recién cuando esta instancia ya no usa nada.
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return exitCode;
}

}  // namespace cola::app
