// Cola de Impresión — gestor de colas de impresión para el LAIMI (TEC).
// Punto de entrada: toda la lógica de arranque está en app/Application.
//
// wWinMain es el punto de entrada de una aplicación gráfica de Windows (sin
// consola) en su variante Unicode. Ninguno de sus parámetros se usa:
//  - HINSTANCE se puede obtener con GetModuleHandle(nullptr);
//  - el segundo HINSTANCE siempre es nullptr (resto de Windows de 16 bits);
//  - la línea de comandos (--tray, --config RUTA, --laimi...) la lee
//    RunApplication con GetCommandLineW/CommandLineToArgvW;
//  - nCmdShow se ignora porque la app decide cómo mostrarse (posición
//    guardada, o iniciar en la bandeja con --tray).
// El valor devuelto es el código de salida del proceso.

#include <windows.h>

#include "app/Application.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return cola::app::RunApplication(); }
