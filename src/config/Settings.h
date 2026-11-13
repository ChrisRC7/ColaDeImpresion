#pragma once
// Preferencias del usuario, persistidas en config.json (ver Paths.h).
// Para agregar una opción nueva: añadir el campo aquí, leerlo en FromJson y
// escribirlo en ToJson (Settings.cpp). Los valores desconocidos se ignoran, así
// que los archivos viejos siguen funcionando.
//
// También al revés: una clave que falta toma el valor predeterminado del
// campo, y los números fuera de rango se acotan (std::clamp en FromJson), así
// un config.json editado a mano nunca deja la app en un estado imposible.
// config.json es por usuario de Windows (D49). Settings es un struct de datos
// sin sincronización: lo usa el hilo de la UI (MainWindow).

#include <map>
#include <string>
#include <vector>

#include "config/Json.h"

namespace cola::config {

// Tema de colores: System sigue la preferencia de Windows (modo de apps
// claro/oscuro); Light y Dark lo fuerzan.
enum class ThemeMode { System, Light, Dark };
// Notificaciones de la bandeja: ninguna, solo errores (predeterminado) o
// todos los eventos (trabajo nuevo, impreso...).
enum class NotifyMode { None, ErrorsOnly, All };

// Posición y tamaño de la ventana principal (píxeles de pantalla) para
// restaurarla al abrir.
struct WindowPlacement {
    int x = CW_USEDEFAULT_VALUE;  // CW_USEDEFAULT_VALUE = que Windows elija
    int y = CW_USEDEFAULT_VALUE;
    int width = 1100;
    int height = 680;
    bool maximized = false;
    // Mismo valor que CW_USEDEFAULT de <windows.h>, copiado para no incluir
    // windows.h en este encabezado.
    static constexpr int CW_USEDEFAULT_VALUE = static_cast<int>(0x80000000);
};

// Límites del ancho de una columna de la lista (DIP).
// (DIP = píxeles independientes del DPI: 1 DIP = 1 px a 96 DPI.)
constexpr int kMinColumnWidth = 28;
constexpr int kMaxColumnWidth = 1200;

// Todas las preferencias. Los valores iniciales de cada campo SON los
// predeterminados (también los que se usan si config.json no existe).
struct Settings {
    ThemeMode theme = ThemeMode::System;
    std::wstring language = L"es";  // "es" | "en"

    // Servidores de impresión conocidos ("" = equipo local, "\\\\SERVIDOR" = remoto).
    // Es la lista de recientes del menú (D4).
    std::vector<std::wstring> servers;
    // Servidor al que se conecta al abrir (mismo formato).
    std::wstring activeServer;
    // Servidor del menú "Conectar al servidor del LAIMI" elegido por el
    // operador; vacío = el de servidor-laimi.txt o el predeterminado (LaimiServer).
    std::wstring laimiServer;
    // Impresora seleccionada; "*" = vista de todas las impresoras del servidor.
    std::wstring activePrinter = L"*";
    // Impresoras fijadas en la barra de acceso rápido (nombre completo, en orden).
    // (D19: Ctrl+1…Ctrl+9 siguen este orden.)
    std::vector<std::wstring> favoritePrinters;
    // Mostrar la barra de acceso rápido bajo la cabecera.
    bool showPrinterBar = true;

    // Orden de la cola: "queue" (orden real de impresión), "machine", "time",
    // "user", "document", "status", "size" (ver app::SortBy).
    // (D20/D30: es solo una vista, no cambia la prioridad en el spooler.)
    std::wstring sortBy = L"queue";
    bool sortDescending = false;  // orden inverso
    // Ancho de columnas elegido por el operador (id de columna -> DIP). Las
    // que no están usan su ancho predeterminado.
    // (D36; al leer se acota a [kMinColumnWidth, kMaxColumnWidth].)
    std::map<std::wstring, int> columnWidths;
    // Orden de las columnas elegido por el operador arrastrando los títulos
    // (ids de la cola y del historial; ver ui::NormalizeColumnOrder). Vacío =
    // orden predeterminado.
    std::vector<std::wstring> columnOrder;

    // Atajos personalizados: nombre de comando -> combinación ("Ctrl+P").
    // Solo se guardan los que difieren de los predeterminados.
    std::map<std::wstring, std::wstring> shortcuts;

    // Carpeta de spool alternativa por servidor (si no se puede usar \\srv\C$).
    // Clave: servidor ("\\SRV"); valor: carpeta donde están los .SPL/.SHD
    // (D6). Solo se usa como respaldo de la lectura por API (D22). Se edita a
    // mano en config.json.
    std::map<std::wstring, std::wstring> spoolDirOverrides;

    int pollIntervalMs = 3000;   // respaldo si las notificaciones del spooler fallan
                                 // (D5; se acota a 500..60000 ms)
    bool confirmCancel = true;   // pedir confirmación antes de cancelar trabajos
    bool autoHoldNewJobs = true; // pausar (retener) cada trabajo nuevo hasta que un operador lo libere
                                 // (D17)
    bool pauseAllOnStart = true; // al iniciar/conectar: pausar las impresoras y retener los trabajos en cola
                                 // (D18)
    bool minimizeToTray = true;  // cerrar la ventana la oculta en la bandeja
    // Copia de lo elegido en el menú; el estado real está en el registro
    // (config/Autostart).
    bool startWithWindows = false;
    NotifyMode notifications = NotifyMode::ErrorsOnly;
    bool keepHistory = true;     // registrar los trabajos que salen de la cola (D13)
    int historyLimit = 2000;     // registros máximos del historial (50..100000)
    bool showPreviewPane = false;  // panel de detalles visible (D33: oculto al iniciar)
    double previewRatio = 0.38;  // ancho del panel de vista previa (0..1)
                                 // (se acota a 0,2..0,7)
    int previewDpi = 110;        // resolución al rasterizar PS/PCL/PDF/XPS
                                 // (48..300)
    int previewMaxPages = 30;    // páginas máximas que se preparan por documento
                                 // (1..500)

    // Rutas a Ghostscript / GhostPCL / GhostXPS (vacío = detección automática).
    // (D7: se ejecutan como procesos externos, no se enlazan.)
    std::wstring ghostscriptPath;
    std::wstring ghostpclPath;
    std::wstring ghostxpsPath;

    WindowPlacement window;

    // Conversión a/desde el objeto JSON de config.json. FromJson nunca falla:
    // cada campo ausente, de otro tipo o fuera de rango toma su predeterminado.
    JsonValue ToJson() const;
    static Settings FromJson(const JsonValue& json);

    // Carga desde disco; si no existe o es inválido, devuelve los valores por defecto.
    // Si existe pero no se puede leer se reintenta 2 veces con 200 ms (un
    // bloqueo momentáneo no es un archivo dañado) y, si sigue ilegible, se
    // copia a DamagedCopyPath antes de devolver los valores por defecto: el
    // primer guardado escribirá encima (M2). Lo registra en app.log.
    static Settings Load(const std::wstring& path);
    // Dónde se copia un config.json ilegible: "<ruta>.danado" (cada copia
    // nueva reemplaza la anterior), igual que el historial.
    static std::wstring DamagedCopyPath(const std::wstring& path) { return path + L".danado"; }
    // Guarda de forma atómica (SaveJsonFile). false si falla (también al log).
    bool Save(const std::wstring& path) const;
};

// Servidor del menú "Conectar al servidor del LAIMI" (D54), en este orden:
//   1. laimiServer de config.json (el operador lo cambió desde el menú);
//   2. la primera línea útil de servidor-laimi.txt junto al .exe: vale para
//      todos los usuarios de la copia portable y se cambia con el Bloc de notas,
//      sin compilar;
//   3. kDefaultLaimiServer.
inline constexpr wchar_t kDefaultLaimiServer[] = L"\\\\vtec-laimiprint";
// Nombre del archivo junto al .exe con el servidor del LAIMI.
inline constexpr wchar_t kLaimiServerFile[] = L"servidor-laimi.txt";
// Devuelve el servidor según el orden de arriba (siempre con "\\" delante).
// Lee el archivo en cada llamada (como mucho 4 KB), así un cambio hecho con
// el Bloc de notas se nota sin reiniciar la app.
std::wstring LaimiServer(const Settings& settings);
// Escribe servidor-laimi.txt junto al .exe (comentario + servidor). false si
// no se pudo (p. ej. instalada en Program Files sin permiso de escritura).
// Escritura atómica (".tmp" + MoveFileEx), en UTF-8 sin BOM y con CRLF.
bool SaveLaimiServerFile(const std::wstring& server);
// Servidor escrito en un texto: primera línea que no esté vacía ni empiece con
// '#', siempre con dos barras invertidas delante. "" si no hay ninguna.
// Acepta el nombre con o sin barras ("vtec-laimiprint", "\\vtec-laimiprint")
// e ignora un BOM UTF-8 inicial. También se usa para normalizar laimiServer.
std::wstring ServerFromText(const std::string& utf8);

// Conversión de las enumeraciones a/desde su clave en config.json
// ("system"/"light"/"dark" y "none"/"errors"/"all"). Un texto desconocido da
// el valor predeterminado (System, ErrorsOnly).
const wchar_t* ThemeModeToString(ThemeMode mode);
ThemeMode ThemeModeFromString(const std::wstring& text);
const wchar_t* NotifyModeToString(NotifyMode mode);
NotifyMode NotifyModeFromString(const std::wstring& text);

}  // namespace cola::config
