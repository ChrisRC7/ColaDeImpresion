#include "config/Settings.h"

#include <windows.h>

#include <algorithm>

#include "config/Paths.h"
#include "util/Log.h"
#include "util/Strings.h"

namespace cola::config {

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

Settings Settings::FromJson(const JsonValue& json) {
    Settings s;
    s.theme = ThemeModeFromString(json[L"theme"].AsString());
    s.language = json[L"language"].AsString(s.language);
    if (s.language != L"es" && s.language != L"en") s.language = L"es";

    for (const auto& v : json[L"servers"].AsArray())
        if (v.IsString()) s.servers.push_back(v.AsString());
    s.activeServer = json[L"activeServer"].AsString();
    s.laimiServer = ServerFromText(util::ToUtf8(json[L"laimiServer"].AsString()));
    s.activePrinter = json[L"activePrinter"].AsString(s.activePrinter);
    for (const auto& v : json[L"favoritePrinters"].AsArray())
        if (v.IsString() && !v.AsString().empty()) s.favoritePrinters.push_back(v.AsString());
    s.showPrinterBar = json[L"showPrinterBar"].AsBool(s.showPrinterBar);
    s.sortBy = json[L"sortBy"].AsString(s.sortBy);
    s.sortDescending = json[L"sortDescending"].AsBool(s.sortDescending);
    for (const auto& [column, width] : json[L"columnWidths"].AsObject())
        if (width.IsNumber()) s.columnWidths[column] = std::clamp(width.AsInt(), kMinColumnWidth, kMaxColumnWidth);
    for (const auto& v : json[L"columnOrder"].AsArray())
        if (v.IsString() && !v.AsString().empty()) s.columnOrder.push_back(v.AsString());

    for (const auto& [command, chord] : json[L"shortcuts"].AsObject())
        if (chord.IsString()) s.shortcuts[command] = chord.AsString();
    for (const auto& [server, dir] : json[L"spoolDirOverrides"].AsObject())
        if (dir.IsString()) s.spoolDirOverrides[server] = dir.AsString();

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
    if (!LoadJsonFile(path, json, &error)) {
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            util::LogError(L"No se pudo leer %ls: %ls", path.c_str(), error.c_str());
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
    std::wstring text = util::FromUtf8(utf8.rfind("\xEF\xBB\xBF", 0) == 0 ? utf8.substr(3) : utf8);
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = util::Trim(std::wstring_view(text).substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == L'#') continue;
        const size_t name = line.find_first_not_of(L'\\');
        if (name == std::wstring::npos) continue;
        return L"\\\\" + line.substr(name);
    }
    return L"";
}

std::wstring LaimiServer(const Settings& settings) {
    if (!settings.laimiServer.empty()) return settings.laimiServer;
    const std::wstring path = ExecutableDir() + L"\\" + kLaimiServerFile;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        std::string text(4096, '\0');
        DWORD read = 0;
        const BOOL ok = ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
        CloseHandle(file);
        text.resize(ok ? read : 0);
        std::wstring server = ServerFromText(text);
        if (!server.empty()) return server;
    }
    return kDefaultLaimiServer;
}

}  // namespace cola::config
