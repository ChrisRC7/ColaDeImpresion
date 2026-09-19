// Cola de Impresión — gestor de colas de impresión para el LAIMI (TEC).
// Punto de entrada: toda la lógica de arranque está en app/Application.

#include <windows.h>

#include "app/Application.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return cola::app::RunApplication(); }
