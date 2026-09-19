#include "config/Settings.h"

#include <windows.h>

#include <algorithm>

#include "util/Log.h"

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
    json[L"activePrinter"] = activePrinter;

    JsonValue keys = JsonValue::MakeObject();
    for (const auto& [command, chord] : shortcuts) keys[command] = chord;
    json[L"shortcuts"] = keys;

    JsonValue spool = JsonValue::MakeObject();
    for (const auto& [server, dir] : spoolDirOverrides) spool[server] = dir;
    json[L"spoolDirOverrides"] = spool;

    json[L"pollIntervalMs"] = pollIntervalMs;
    json[L"confirmCancel"] = confirmCancel;
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
    s.activePrinter = json[L"activePrinter"].AsString(s.activePrinter);

    for (const auto& [command, chord] : json[L"shortcuts"].AsObject())
        if (chord.IsString()) s.shortcuts[command] = chord.AsString();
    for (const auto& [server, dir] : json[L"spoolDirOverrides"].AsObject())
        if (dir.IsString()) s.spoolDirOverrides[server] = dir.AsString();

    s.pollIntervalMs = std::clamp(json[L"pollIntervalMs"].AsInt(s.pollIntervalMs), 500, 60000);
    s.confirmCancel = json[L"confirmCancel"].AsBool(s.confirmCancel);
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

}  // namespace cola::config
