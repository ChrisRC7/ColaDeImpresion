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

constexpr wchar_t kMutexName[] = L"Local\\ColaDeImpresion.SingleInstance";

struct Arguments {
    StartupOptions startup;
    bool elevatedRestart = false;
    // Solo para desarrollo: permite probar una compilación nueva mientras la
    // versión de producción sigue abierta en el mismo equipo (con otra
    // configuración, p. ej. modo portable en la carpeta de compilación).
    bool additionalInstance = false;
};

Arguments ParseArguments() {
    Arguments args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--tray") args.startup.startInTray = true;
        else if (a == L"--elevated-restart") args.elevatedRestart = true;
        else if (a == L"--instancia-adicional") args.additionalInstance = true;
        else if (a == L"--config" && i + 1 < argc) config::SetSettingsFileOverride(argv[++i]);
        else if (a == L"--server" && i + 1 < argc) {
            args.startup.server = argv[++i];
            args.startup.hasServer = true;
        }
    }
    if (argv) LocalFree(argv);
    return args;
}

}  // namespace

int RunApplication() {
    const Arguments args = ParseArguments();

    // Instancia única: si ya hay una, se le pide que se muestre y salimos.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS && !args.additionalInstance) {
        if (args.elevatedRestart) {
            // Reinicio como administrador: esperar a que la instancia anterior cierre.
            DWORD wait = WaitForSingleObject(mutex, 15000);
            if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
                CloseHandle(mutex);
                return 1;
            }
        } else {
            HWND existing = FindWindowW(MainWindow::kClassName, nullptr);
            if (existing) PostMessageW(existing, MainWindow::ShowInstanceMessage(), 0, 0);
            CloseHandle(mutex);
            return 0;
        }
    }

    util::LogInit(config::LogFile());
    util::LogInfo(L"Inicio de ColaDeImpresion %ls", COLA_VERSION_STRING);
    // Copias temporales de documentos que hayan quedado de una ejecución anterior.
    preview::CleanupStaleTempFiles();

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ui::gfx::StartGdiPlus();
    ui::InitDarkModeSupport();

    int exitCode = 0;
    {
        MainWindow window;
        if (!window.Create(args.startup)) {
            util::LogError(L"No se pudo crear la ventana principal: %lu", GetLastError());
            exitCode = 1;
        } else {
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (window.PreTranslate(msg)) continue;
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            exitCode = static_cast<int>(msg.wParam);
        }
    }

    // La ventana (y sus hilos de vista previa) ya se destruyó: nada usa la carpeta temporal.
    preview::RemovePrivateTempDir();
    ui::gfx::StopGdiPlus();
    if (SUCCEEDED(com)) CoUninitialize();
    util::LogInfo(L"Fin de ColaDeImpresion");
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return exitCode;
}

}  // namespace cola::app
