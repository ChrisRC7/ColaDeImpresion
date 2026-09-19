// Pruebas de util/ y config/: formato, cadenas, JSON, ajustes e historial.

#include "TestFramework.h"
#include "config/History.h"
#include "config/Json.h"
#include "config/Settings.h"
#include "util/Format.h"
#include "util/Strings.h"

using namespace cola;

TEST(FormatBytes) {
    CHECK_EQ(util::FormatBytes(0), std::wstring(L"\u2014"));
    CHECK_EQ(util::FormatBytes(512), std::wstring(L"512 B"));
    CHECK_EQ(util::FormatBytes(1536), std::wstring(L"1.5 KB"));
    CHECK_EQ(util::FormatBytes(5ull * 1024 * 1024), std::wstring(L"5.0 MB"));
    CHECK_EQ(util::FormatBytes(300ull * 1024 * 1024), std::wstring(L"300 MB"));
}

TEST(FormatWideStrings) {
    // %ls debe funcionar con cadenas anchas en cualquier toolchain.
    CHECK_EQ(util::Format(L"%ls-%u", L"abc", 7u), std::wstring(L"abc-7"));
}

TEST(Utf8RoundTrip) {
    std::wstring text = L"Impresión año ñandú €";
    CHECK_EQ(util::FromUtf8(util::ToUtf8(text)), text);
}

TEST(StringHelpers) {
    CHECK(util::ContainsInsensitive(L"Informe FINAL.pdf", L"final"));
    CHECK(!util::ContainsInsensitive(L"Informe", L"tesis"));
    CHECK_EQ(util::Trim(L"  hola \t"), std::wstring(L"hola"));
    auto parts = util::Split(L"a,b,,c", L',');
    CHECK_EQ(parts.size(), size_t(4));
    CHECK_EQ(parts[2], std::wstring(L""));
}

TEST(UnixTimeRoundTrip) {
    SYSTEMTIME st{};
    st.wYear = 2026;
    st.wMonth = 9;
    st.wDay = 19;
    st.wHour = 15;
    st.wMinute = 30;
    std::int64_t secs = util::ToUnixSeconds(st);
    SYSTEMTIME back = util::FromUnixSeconds(secs);
    CHECK_EQ(back.wYear, st.wYear);
    CHECK_EQ(back.wDay, st.wDay);
    CHECK_EQ(back.wHour, st.wHour);
    CHECK_EQ(back.wMinute, st.wMinute);
}

TEST(JsonParseAndSerialize) {
    config::JsonValue v;
    std::string text = R"({"a": 1, "b": [true, null, "x\u00f1"], "c": {"d": -2.5}})";
    CHECK(config::JsonValue::Parse(text, v));
    CHECK_EQ(v[L"a"].AsInt(), 1);
    CHECK(v[L"b"].AsArray()[0].AsBool());
    CHECK(v[L"b"].AsArray()[1].IsNull());
    CHECK_EQ(v[L"b"].AsArray()[2].AsString(), std::wstring(L"x\u00f1"));
    CHECK_EQ(v[L"c"][L"d"].AsNumber(), -2.5);

    config::JsonValue again;
    CHECK(config::JsonValue::Parse(v.Serialize(2), again));
    CHECK_EQ(again[L"b"].AsArray()[2].AsString(), std::wstring(L"x\u00f1"));
    CHECK_EQ(again[L"c"][L"d"].AsNumber(), -2.5);
}

TEST(JsonRejectsInvalid) {
    config::JsonValue v;
    CHECK(!config::JsonValue::Parse("{\"a\": }", v));
    CHECK(!config::JsonValue::Parse("[1, 2", v));
    CHECK(!config::JsonValue::Parse("{} extra", v));
}

TEST(JsonCopyOnWrite) {
    config::JsonValue a = config::JsonValue::MakeObject();
    a[L"x"] = 1;
    config::JsonValue b = a;
    b[L"x"] = 2;
    CHECK_EQ(a[L"x"].AsInt(), 1);
    CHECK_EQ(b[L"x"].AsInt(), 2);
}

TEST(SettingsRoundTrip) {
    config::Settings s;
    s.theme = config::ThemeMode::Dark;
    s.language = L"en";
    s.servers = {L"\\\\LAIMI-PS", L""};
    s.activeServer = L"\\\\LAIMI-PS";
    s.shortcuts[L"pause"] = L"Ctrl+Shift+X";
    s.spoolDirOverrides[L"\\\\LAIMI-PS"] = L"\\\\LAIMI-PS\\spool$";
    s.notifications = config::NotifyMode::All;
    s.ghostscriptPath = L"C:\\gs\\gswin64c.exe";
    config::Settings r = config::Settings::FromJson(s.ToJson());
    CHECK(r.theme == config::ThemeMode::Dark);
    CHECK_EQ(r.language, std::wstring(L"en"));
    CHECK_EQ(r.servers.size(), size_t(2));
    CHECK_EQ(r.activeServer, s.activeServer);
    CHECK_EQ(r.shortcuts[L"pause"], std::wstring(L"Ctrl+Shift+X"));
    CHECK_EQ(r.spoolDirOverrides[L"\\\\LAIMI-PS"], std::wstring(L"\\\\LAIMI-PS\\spool$"));
    CHECK(r.notifications == config::NotifyMode::All);
    CHECK_EQ(r.ghostscriptPath, s.ghostscriptPath);
}

TEST(SettingsDefaultsOnGarbage) {
    config::JsonValue json = config::JsonValue::MakeObject();
    json[L"pollIntervalMs"] = 1;          // fuera de rango
    json[L"theme"] = L"purple";           // desconocido
    json[L"language"] = L"fr";            // no soportado
    config::Settings s = config::Settings::FromJson(json);
    CHECK_EQ(s.pollIntervalMs, 500);
    CHECK(s.theme == config::ThemeMode::System);
    CHECK_EQ(s.language, std::wstring(L"es"));
}

TEST(HistoryStatsAndLimit) {
    config::History history(L"", 3);
    const std::int64_t now = 1'800'000'000;
    auto add = [&](std::int64_t t, std::uint32_t pages, config::JobOutcome outcome, const wchar_t* user) {
        config::HistoryEntry e;
        e.timeUnix = t;
        e.pages = pages;
        e.outcome = outcome;
        e.user = user;
        history.Add(e);
    };
    add(now - 10 * 86400, 50, config::JobOutcome::Printed, L"old");  // fuera de la semana
    add(now - 60, 3, config::JobOutcome::Printed, L"ana");
    add(now - 30, 7, config::JobOutcome::Cancelled, L"ana");         // cancelado: no cuenta
    add(now - 10, 2, config::JobOutcome::Printed, L"luis");
    CHECK_EQ(history.Entries().size(), size_t(3));  // límite
    auto stats = history.ComputeStats(now);
    CHECK_EQ(stats.pagesToday, 5ull);
    CHECK_EQ(stats.pagesWeek, 5ull);
    CHECK_EQ(stats.pagesPerUser[L"ana"], 3ull);
    CHECK_EQ(stats.jobsToday, 2ull);
}
