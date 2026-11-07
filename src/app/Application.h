#pragma once
// Punto de arranque de la aplicación: argumentos de línea de comandos,
// instancia única, inicialización de COM / GDI+ / controles comunes, y bucle
// de mensajes.
//
// Argumentos:
//   --tray               inicia oculta en la bandeja (lo usa el inicio con Windows)
//   --server NOMBRE      conecta a ese servidor de impresión ("" = este equipo)
//   --laimi              conecta al servidor del LAIMI (config::LaimiServer)
//   --elevated-restart   (interno) espera a que la instancia anterior termine
//   --instancia-adicional (desarrollo) permite abrir una segunda copia aunque ya
//                        haya otra abierta (ver GUIA_IA.md §6)
//   --config RUTA        (desarrollo) usa ese config.json en lugar del del
//                        usuario en %LOCALAPPDATA% (D49)
//
// Capa: app/ (orquestación). Es lo único que llama main/wWinMain.

namespace cola::app {

// Ejecuta la aplicación completa en el hilo que la llama (el hilo de la UI):
// procesa los argumentos, garantiza la instancia única, inicializa el log,
// COM (STA), GDI+, controles comunes y el modo oscuro, crea la MainWindow y
// corre el bucle de mensajes hasta WM_QUIT. Al salir libera todo en orden
// inverso. Devuelve el código de salida del proceso (el wParam de WM_QUIT,
// 0 si se delegó en una instancia ya abierta, 1 si falló la creación de la
// ventana o la espera del reinicio elevado).
int RunApplication();

}  // namespace cola::app
