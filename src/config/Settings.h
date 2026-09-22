#pragma once
// Preferencias del usuario, persistidas en config.json (ver Paths.h).
// Para agregar una opción nueva: añadir el campo aquí, leerlo en FromJson y
// escribirlo en ToJson (Settings.cpp). Los valores desconocidos se ignoran, así
// que los archivos viejos siguen funcionando.

#include <map>
#include <string>
#include <vector>

#include "config/Json.h"

namespace cola::config {

enum class ThemeMode { System, Light, Dark };
enum class NotifyMode { None, ErrorsOnly, All };

struct WindowPlacement {
    int x = CW_USEDEFAULT_VALUE;
    int y = CW_USEDEFAULT_VALUE;
    int width = 1100;
    int height = 680;
    bool maximized = false;
    static constexpr int CW_USEDEFAULT_VALUE = static_cast<int>(0x80000000);
};

struct Settings {
    ThemeMode theme = ThemeMode::System;
    std::wstring language = L"es";  // "es" | "en"

    // Servidores de impresión conocidos ("" = equipo local, "\\\\SERVIDOR" = remoto).
    std::vector<std::wstring> servers;
    std::wstring activeServer;
    // Impresora seleccionada; "*" = vista de todas las impresoras del servidor.
    std::wstring activePrinter = L"*";
    // Impresoras fijadas en la barra de acceso rápido (nombre completo, en orden).
    std::vector<std::wstring> favoritePrinters;
    bool showPrinterBar = true;

    // Orden de la cola: "queue" (orden real de impresión), "machine", "time",
    // "user", "document", "status", "size" (ver app::SortBy).
    std::wstring sortBy = L"queue";
    bool sortDescending = false;

    // Atajos personalizados: nombre de comando -> combinación ("Ctrl+P").
    // Solo se guardan los que difieren de los predeterminados.
    std::map<std::wstring, std::wstring> shortcuts;

    // Carpeta de spool alternativa por servidor (si no se puede usar \\srv\C$).
    std::map<std::wstring, std::wstring> spoolDirOverrides;

    int pollIntervalMs = 3000;   // respaldo si las notificaciones del spooler fallan
    bool confirmCancel = true;   // pedir confirmación antes de cancelar trabajos
    bool autoHoldNewJobs = true; // pausar (retener) cada trabajo nuevo hasta que un operador lo libere
    bool pauseAllOnStart = true; // al iniciar/conectar: pausar las impresoras y retener los trabajos en cola
    bool minimizeToTray = true;  // cerrar la ventana la oculta en la bandeja
    bool startWithWindows = false;
    NotifyMode notifications = NotifyMode::ErrorsOnly;
    bool keepHistory = true;
    int historyLimit = 2000;
    bool showPreviewPane = true;
    double previewRatio = 0.38;  // ancho del panel de vista previa (0..1)
    int previewDpi = 110;        // resolución al rasterizar PS/PCL/PDF/XPS
    int previewMaxPages = 30;    // páginas máximas que se preparan por documento

    // Rutas a Ghostscript / GhostPCL / GhostXPS (vacío = detección automática).
    std::wstring ghostscriptPath;
    std::wstring ghostpclPath;
    std::wstring ghostxpsPath;

    WindowPlacement window;

    JsonValue ToJson() const;
    static Settings FromJson(const JsonValue& json);

    // Carga desde disco; si no existe o es inválido, devuelve los valores por defecto.
    static Settings Load(const std::wstring& path);
    bool Save(const std::wstring& path) const;
};

const wchar_t* ThemeModeToString(ThemeMode mode);
ThemeMode ThemeModeFromString(const std::wstring& text);
const wchar_t* NotifyModeToString(NotifyMode mode);
NotifyMode NotifyModeFromString(const std::wstring& text);

}  // namespace cola::config
