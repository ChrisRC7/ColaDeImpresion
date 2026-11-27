// Implementación de las preferencias: conversión a/desde JSON, carga y
// guardado de config.json y el servidor del LAIMI (servidor-laimi.txt, D54).

#include "config/Settings.h"

#include <windows.h>

#include <algorithm>

#include "config/Paths.h"
#include "util/Log.h"
#include "util/Strings.h"

namespace cola::config {

// Claves estables en inglés para config.json.
const wchar_t* ThemeModeToString(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Light: return L"light";
        case ThemeMode::Dark: return L"dark";
        default: return L"system";
    }
}

ThemeMode ThemeModeFromString(const std::wstring& text) {
    if (text == L"light") return ThemeMode::Light;
    if (text == L"dark") return ThemeMode::Dark;
    return ThemeMode::System;
}

const wchar_t* NotifyModeToString(NotifyMode mode) {
    switch (mode) {
        case NotifyMode::None: return L"none";
        case NotifyMode::All: return L"all";
        default: return L"errors";
    }
}

NotifyMode NotifyModeFromString(const std::wstring& text) {
    if (text == L"none") return NotifyMode::None;
    if (text == L"all") return NotifyMode::All;
    return NotifyMode::ErrorsOnly;
}

// Escribe TODOS los campos (también los que tienen el valor predeterminado),
// así config.json sirve de referencia de las opciones disponibles. Los
// nombres de clave deben coincidir con los que lee FromJson.
JsonValue Settings::ToJson() const {
    JsonValue json = JsonValue::MakeObject();
    json[L"theme"] = ThemeModeToString(theme);
    json[L"language"] = language;

    JsonValue serverList = JsonValue::MakeArray();
    for (const auto& s : servers) serverList.Push(s);
    json[L"servers"] = serverList;
    json[L"activeServer"] = activeServer;
    json[L"laimiServer"] = laimiServer;
    json[L"activePrinter"] = activePrinter;
    JsonValue favorites = JsonValue::MakeArray();
    for (const auto& p : favoritePrinters) favorites.Push(p);
    json[L"favoritePrinters"] = favorites;
    json[L"showPrinterBar"] = showPrinterBar;
    json[L"sortBy"] = sortBy;
    json[L"sortDescending"] = sortDescending;
    JsonValue widths = JsonValue::MakeObject();
    for (const auto& [column, width] : columnWidths) widths[column] = width;
    json[L"columnWidths"] = widths;
    JsonValue order = JsonValue::MakeArray();
    for (const auto& column : columnOrder) order.Push(column);
    json[L"columnOrder"] = order;

    // Mapas -> objetos JSON (clave -> texto).
    JsonValue keys = JsonValue::MakeObject();
    for (const auto& [command, chord] : shortcuts) keys[command] = chord;
    json[L"shortcuts"] = keys;

    JsonValue spool = JsonValue::MakeObject();
    for (const auto& [server, dir] : spoolDirOverrides) spool[server] = dir;
    json[L"spoolDirOverrides"] = spool;

    json[L"pollIntervalMs"] = pollIntervalMs;
    json[L"confirmCancel"] = confirmCancel;
    json[L"autoHoldNewJobs"] = autoHoldNewJobs;
    json[L"pauseAllOnStart"] = pauseAllOnStart;
    json[L"minimizeToTray"] = minimizeToTray;
    json[L"startWithWindows"] = startWithWindows;
    json[L"notifications"] = NotifyModeToString(notifications);
    json[L"keepHistory"] = keepHistory;
    json[L"historyLimit"] = historyLimit;
    json[L"showPreviewPane"] = showPreviewPane;
    json[L"previewRatio"] = previewRatio;
    json[L"previewDpi"] = previewDpi;
    json[L"previewMaxPages"] = previewMaxPages;

    // Subobjetos "renderers" y "window" para agrupar opciones relacionadas.
    JsonValue renderers = JsonValue::MakeObject();
    renderers[L"ghostscript"] = ghostscriptPath;
    renderers[L"ghostpcl"] = ghostpclPath;
    renderers[L"ghostxps"] = ghostxpsPath;
    json[L"renderers"] = renderers;

    JsonValue win = JsonValue::MakeObject();
    win[L"x"] = window.x;
    win[L"y"] = window.y;
    win[L"width"] = window.width;
    win[L"height"] = window.height;
    win[L"maximized"] = window.maximized;
    json[L"window"] = win;
    return json;
}

// Patrón: se parte de un Settings con los predeterminados (`s`) y cada campo
// se lee pasando su valor actual como valor por defecto del accesor
// tolerante; así una clave ausente o de otro tipo no cambia nada.
Settings Settings::FromJson(const JsonValue& json) {
    Settings s;
    s.theme = ThemeModeFromString(json[L"theme"].AsString());
    s.language = json[L"language"].AsString(s.language);
    // Solo hay dos idiomas (D16); cualquier otro valor vuelve a español.
    if (s.language != L"es" && s.language != L"en") s.language = L"es";

    // Listas: se ignoran los elementos que no son cadenas (y, en las de
    // impresoras y columnas, los vacíos).
    for (const auto& v : json[L"servers"].AsArray())
        if (v.IsString()) s.servers.push_back(v.AsString());
    s.activeServer = json[L"activeServer"].AsString();
    // Se normaliza igual que el archivo (barras delante, sin comentarios);
    // un valor vacío o solo de barras queda "" (= usar el archivo).
    s.laimiServer = ServerFromText(util::ToUtf8(json[L"laimiServer"].AsString()));
    s.activePrinter = json[L"activePrinter"].AsString(s.activePrinter);
    for (const auto& v : json[L"favoritePrinters"].AsArray())
        if (v.IsString() && !v.AsString().empty()) s.favoritePrinters.push_back(v.AsString());
    s.showPrinterBar = json[L"showPrinterBar"].AsBool(s.showPrinterBar);
    // sortBy no se valida aquí: app::SortBy trata un valor desconocido.
    s.sortBy = json[L"sortBy"].AsString(s.sortBy);
    s.sortDescending = json[L"sortDescending"].AsBool(s.sortDescending);
    for (const auto& [column, width] : json[L"columnWidths"].AsObject())
        if (width.IsNumber()) s.columnWidths[column] = std::clamp(width.AsInt(), kMinColumnWidth, kMaxColumnWidth);
    // Ids desconocidos o repetidos se depuran después (ui::NormalizeColumnOrder).
    for (const auto& v : json[L"columnOrder"].AsArray())
        if (v.IsString() && !v.AsString().empty()) s.columnOrder.push_back(v.AsString());

    // Los atajos se validan al aplicarlos (input::ShortcutMap::ApplyOverrides).
    for (const auto& [command, chord] : json[L"shortcuts"].AsObject())
        if (chord.IsString()) s.shortcuts[command] = chord.AsString();
    for (const auto& [server, dir] : json[L"spoolDirOverrides"].AsObject())
        if (dir.IsString()) s.spoolDirOverrides[server] = dir.AsString();

    // Valores numéricos acotados a rangos razonables (un sondeo de 0 ms
    // saturaría el servidor; uno de horas dejaría la lista congelada).
    s.pollIntervalMs = std::clamp(json[L"pollIntervalMs"].AsInt(s.pollIntervalMs), 500, 60000);
    s.confirmCancel = json[L"confirmCancel"].AsBool(s.confirmCancel);
    s.autoHoldNewJobs = json[L"autoHoldNewJobs"].AsBool(s.autoHoldNewJobs);
    s.pauseAllOnStart = json[L"pauseAllOnStart"].AsBool(s.pauseAllOnStart);
    s.minimizeToTray = json[L"minimizeToTray"].AsBool(s.minimizeToTray);
    s.startWithWindows = json[L"startWithWindows"].AsBool(s.startWithWindows);
    s.notifications = NotifyModeFromString(json[L"notifications"].AsString(L"errors"));
    s.keepHistory = json[L"keepHistory"].AsBool(s.keepHistory);
    s.historyLimit = std::clamp(json[L"historyLimit"].AsInt(s.historyLimit), 50, 100000);
    s.showPreviewPane = json[L"showPreviewPane"].AsBool(s.showPreviewPane);
    s.previewRatio = std::clamp(json[L"previewRatio"].AsNumber(s.previewRatio), 0.2, 0.7);
    s.previewDpi = std::clamp(json[L"previewDpi"].AsInt(s.previewDpi), 48, 300);
    s.previewMaxPages = std::clamp(json[L"previewMaxPages"].AsInt(s.previewMaxPages), 1, 500);

    const JsonValue& renderers = json[L"renderers"];
    s.ghostscriptPath = renderers[L"ghostscript"].AsString();
    s.ghostpclPath = renderers[L"ghostpcl"].AsString();
    s.ghostxpsPath = renderers[L"ghostxps"].AsString();

    // Tamaño mínimo 480 × 320 para que la ventana nunca quede inusable. La
    // posición no se valida aquí (un monitor desconectado se resuelve al
    // crear la ventana).
    const JsonValue& win = json[L"window"];
    s.window.x = win[L"x"].AsInt(s.window.x);
    s.window.y = win[L"y"].AsInt(s.window.y);
    s.window.width = std::max(480, win[L"width"].AsInt(s.window.width));
    s.window.height = std::max(320, win[L"height"].AsInt(s.window.height));
    s.window.maximized = win[L"maximized"].AsBool(false);
    return s;
}

Settings Settings::Load(const std::wstring& path) {
    JsonValue json;
    std::wstring error;
    bool loaded = LoadJsonFile(path, json, &error);
    // M2: un bloqueo momentáneo (antivirus, otra copia de la app guardándolo)
    // no es un archivo dañado. Antes se cargaban los valores por defecto y el
    // primer guardado los escribía encima: se perdían atajos, columnas e
    // impresoras fijadas. Igual que History::Load (D52): se reintenta dos
    // veces (solo si el archivo existe; si no, es el primer uso).
    for (int attempt = 0; !loaded && attempt < 2 && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
         ++attempt) {
        Sleep(200);
        json = JsonValue();
        loaded = LoadJsonFile(path, json, &error);
    }
    if (!loaded) {
        // Que no exista es normal (primer uso). Si existe y sigue ilegible o
        // está dañado, se guarda una copia antes de que la app escriba encima
        // la configuración por defecto.
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            const std::wstring backup = DamagedCopyPath(path);
            if (CopyFileW(path.c_str(), backup.c_str(), FALSE))
                util::LogError(L"No se pudo leer %ls (%ls); copia en %ls", path.c_str(), error.c_str(), backup.c_str());
            else
                util::LogError(L"No se pudo leer %ls (%ls) ni copiarlo (%lu)", path.c_str(), error.c_str(),
                               GetLastError());
        }
        return Settings{};
    }
    return FromJson(json);
}

bool Settings::Save(const std::wstring& path) const {
    bool ok = SaveJsonFile(path, ToJson());
    if (!ok) util::LogError(L"No se pudo guardar la configuración en %ls", path.c_str());
    return ok;
}

std::wstring ServerFromText(const std::string& utf8) {
    // El Bloc de notas puede guardar con BOM: se quita antes de convertir.
    std::wstring text = util::FromUtf8(utf8.rfind("\xEF\xBB\xBF", 0) == 0 ? utf8.substr(3) : utf8);
    size_t pos = 0;
    // Recorre línea por línea (separadas por LF; el CR de CRLF lo quita Trim).
    while (pos <= text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = util::Trim(std::wstring_view(text).substr(pos, end - pos));
        pos = end + 1;
        // Líneas vacías y comentarios (#) no cuentan.
        if (line.empty() || line[0] == L'#') continue;
        // Se quitan las barras invertidas que haya delante (0, 1, 2 o más) y
        // se ponen exactamente dos: así "srv", "\srv" y "\\srv" dan lo mismo.
        // Una línea hecha solo de barras se salta.
        const size_t name = line.find_first_not_of(L'\\');
        if (name == std::wstring::npos) continue;
        return L"\\\\" + line.substr(name);
    }
    return L"";
}

std::wstring LaimiServer(const Settings& settings) {
    // 1. Lo que el operador eligió desde el menú (solo si no se pudo escribir
    //    el archivo compartido, ver MainWindow).
    if (!settings.laimiServer.empty()) return settings.laimiServer;
    // 2. servidor-laimi.txt junto al .exe. Se comparte lectura y escritura
    //    para poder leerlo aunque alguien lo tenga abierto.
    const std::wstring path = ExecutableDir() + L"\\" + kLaimiServerFile;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        // Con 4 KB basta de sobra para los comentarios y el nombre; lo que
        // pase de ahí se ignora.
        std::string text(4096, '\0');
        DWORD read = 0;
        const BOOL ok = ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
        CloseHandle(file);
        text.resize(ok ? read : 0);
        std::wstring server = ServerFromText(text);
        if (!server.empty()) return server;
    }
    // 3. Valor fijo de compilación.
    return kDefaultLaimiServer;
}

bool SaveLaimiServerFile(const std::wstring& server) {
    // Contenido: tres líneas de comentario que explican el archivo a quien lo
    // abra con el Bloc de notas, y el servidor. Los acentos van como bytes
    // UTF-8 escapados (\xC3\xB3 = "ó") para no depender de la codificación
    // con que el compilador lee este .cpp.
    const std::string text =
        "# Servidor de impresi\xC3\xB3n del LAIMI que usan el men\xC3\xBA \"Conectar al servidor del\r\n"
        "# LAIMI\" y \"Abrir vtec-laimiprint.bat\". Se cambia desde la app (men\xC3\xBA >\r\n"
        "# \"Cambiar servidor del LAIMI...\") o aqu\xC3\xAD: la primera l\xC3\xADnea que no empiece con #.\r\n" +
        util::ToUtf8(server) + "\r\n";
    // Escritura atómica: temporal + MoveFileEx, para que las demás copias de
    // la app (otros usuarios de la misma carpeta portable) nunca lean un
    // archivo a medias. Si la carpeta no admite escritura falla aquí mismo.
    const std::wstring path = ExecutableDir() + L"\\" + kLaimiServerFile;
    const std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                    written == text.size();
    CloseHandle(file);
    // Si la escritura o el reemplazo fallan, se borra el temporal y el
    // archivo anterior (si había) queda como estaba.
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

}  // namespace cola::config
