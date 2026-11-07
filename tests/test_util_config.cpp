// Pruebas de util/ y config/: formato, cadenas, JSON, ajustes e historial.
//
// Módulos probados:
//   - util/Format: FormatBytes (columna Tamaño), Format (printf de cadenas anchas)
//     y la conversión SYSTEMTIME <-> hora Unix (el historial guarda horas Unix).
//   - util/Strings: UTF-8 <-> UTF-16, búsqueda sin mayúsculas, Trim y Split.
//   - config/Json: el JSON propio (D10) con el que se leen y escriben
//     config.json e historial.json.
//   - config/Settings y config/Paths: ida y vuelta de config.json, valores
//     inválidos que vuelven al predeterminado y el servidor del LAIMI (D54).
//   - config/History: límite de registros y estadísticas (páginas de hoy y de
//     la semana, por usuario).
// Casi todo es lógica pura; la única prueba que toca disco es
// LaimiServerFromText (escribe y borra servidor-laimi.txt junto a
// cola_tests.exe).

#include "TestFramework.h"
#include "config/History.h"
#include "config/Json.h"
#include "config/Paths.h"
#include "config/Settings.h"
#include "util/Format.h"
#include "util/Strings.h"

using namespace cola;

// Texto de la columna Tama\u00f1o. Unidades binarias (1 KB = 1024 B), un decimal
// por debajo de 100 y ninguno desde 100, para que quepa en la columna.
TEST(FormatBytes) {
    // 0 es "desconocido" (el spooler da Size = 0 mientras todav\u00eda no lo sabe):
    // se muestra la raya "\u2014" (U+2014), no "0 B".
    CHECK_EQ(util::FormatBytes(0), std::wstring(L"\u2014"));
    // 1536 = 1,5 \u00d7 1024. El separador decimal es siempre el punto (no depende
    // de la configuraci\u00f3n regional del equipo). 300 MB ya no lleva decimal.
    CHECK_EQ(util::FormatBytes(512), std::wstring(L"512 B"));
    CHECK_EQ(util::FormatBytes(1536), std::wstring(L"1.5 KB"));
    CHECK_EQ(util::FormatBytes(5ull * 1024 * 1024), std::wstring(L"5.0 MB"));
    CHECK_EQ(util::FormatBytes(300ull * 1024 * 1024), std::wstring(L"300 MB"));
}

// util::Format es el printf del proyecto para std::wstring.
TEST(FormatWideStrings) {
    // %ls debe funcionar con cadenas anchas en cualquier toolchain.
    // (En MinGW, según el modo de compatibilidad de swprintf, %s puede
    // interpretarse como cadena de char; %ls es lo único portable entre MinGW y
    // MSVC, D2.)
    CHECK_EQ(util::Format(L"%ls-%u", L"abc", 7u), std::wstring(L"abc-7"));
}

// Los archivos (config.json, historial.json, app.log) son UTF-8 y la app
// trabaja en UTF-16: la ida y vuelta no debe perder acentos, ñ ni símbolos
// fuera de Latin-1 (€ ocupa 3 bytes en UTF-8).
TEST(Utf8RoundTrip) {
    std::wstring text = L"Impresión año ñandú €";
    CHECK_EQ(util::FromUtf8(util::ToUtf8(text)), text);
}

// Funciones de texto que usan el filtro de la lista (ContainsInsensitive) y la
// lectura de configuración (Trim, Split).
TEST(StringHelpers) {
    // El filtro de la cola no distingue mayúsculas ("final" encuentra "FINAL").
    CHECK(util::ContainsInsensitive(L"Informe FINAL.pdf", L"final"));
    CHECK(!util::ContainsInsensitive(L"Informe", L"tesis"));
    // Trim quita espacios y tabuladores de ambos extremos.
    CHECK_EQ(util::Trim(L"  hola \t"), std::wstring(L"hola"));
    // Split conserva los campos vacíos: "a,b,,c" son 4 partes y la tercera es
    // "" (no se colapsan separadores seguidos).
    auto parts = util::Split(L"a,b,,c", L',');
    CHECK_EQ(parts.size(), size_t(4));
    CHECK_EQ(parts[2], std::wstring(L""));
}

// El historial guarda las horas como segundos Unix (timeUnix, Enviado, D41):
// convertir un SYSTEMTIME y volver debe dar la misma fecha y hora (hasta el
// minuto; los segundos y milisegundos quedan en 0).
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

// JSON propio (D10): un documento con todos los tipos (n\u00famero entero, bool,
// null, cadena con escape \u, objeto anidado y n\u00famero negativo con decimales)
// se lee bien y, al serializarlo (con sangr\u00eda de 2) y volver a leerlo, da lo
// mismo.
TEST(JsonParseAndSerialize) {
    config::JsonValue v;
    // "x\u00f1" en el texto JSON es la secuencia de escape de "x\u00f1": el lector
    // debe convertirla al car\u00e1cter UTF-16 correspondiente.
    std::string text = R"({"a": 1, "b": [true, null, "x\u00f1"], "c": {"d": -2.5}})";
    CHECK(config::JsonValue::Parse(text, v));
    CHECK_EQ(v[L"a"].AsInt(), 1);
    CHECK(v[L"b"].AsArray()[0].AsBool());
    CHECK(v[L"b"].AsArray()[1].IsNull());
    CHECK_EQ(v[L"b"].AsArray()[2].AsString(), std::wstring(L"x\u00f1"));
    CHECK_EQ(v[L"c"][L"d"].AsNumber(), -2.5);

    // Ida y vuelta: Serialize escribe la ñ en UTF-8 (o escapada) y Parse la
    // recupera igual; -2.5 no pierde precisión.
    config::JsonValue again;
    CHECK(config::JsonValue::Parse(v.Serialize(2), again));
    CHECK_EQ(again[L"b"].AsArray()[2].AsString(), std::wstring(L"x\u00f1"));
    CHECK_EQ(again[L"c"][L"d"].AsNumber(), -2.5);
}

// Un JSON dañado debe rechazarse (Parse devuelve false) en vez de leerse a
// medias: así History::Load detecta un historial.json dañado y lo respalda
// antes de sobrescribirlo (D52), y config.json vuelve a los predeterminados.
TEST(JsonRejectsInvalid) {
    config::JsonValue v;
    // Falta el valor de "a".
    CHECK(!config::JsonValue::Parse("{\"a\": }", v));
    // Arreglo sin cerrar (archivo truncado).
    CHECK(!config::JsonValue::Parse("[1, 2", v));
    // Texto sobrante después de un documento válido.
    CHECK(!config::JsonValue::Parse("{} extra", v));
}

// JsonValue guarda arreglos y objetos en un shared_ptr (copiar es barato). Esta
// prueba asegura que la copia se comporta como un valor independiente: al
// modificar b se separa de a ("copia al escribir") y a no cambia. Sin eso,
// modificar una copia de la configuración alteraría la original.
TEST(JsonCopyOnWrite) {
    config::JsonValue a = config::JsonValue::MakeObject();
    a[L"x"] = 1;
    config::JsonValue b = a;  // comparten el mismo objeto interno
    b[L"x"] = 2;              // aquí b debe hacer su propia copia
    CHECK_EQ(a[L"x"].AsInt(), 1);
    CHECK_EQ(b[L"x"].AsInt(), 2);
}

// config.json: cada campo que se guarda con ToJson debe volver igual con
// FromJson (si no, una preferencia del operador se perdería al reiniciar la
// app). Los valores imitan los reales: un servidor \\LAIMI-PS, un atajo
// reasignado, una carpeta de spool alternativa (spoolDirOverrides, D6), la ruta
// de Ghostscript (D7) y ancho/orden de columnas (D36/D38).
TEST(SettingsRoundTrip) {
    config::Settings s;
    s.theme = config::ThemeMode::Dark;
    s.language = L"en";
    // L"" en la lista de servidores es el equipo local (D4): debe conservarse.
    s.servers = {L"\\\\LAIMI-PS", L""};
    s.activeServer = L"\\\\LAIMI-PS";
    s.shortcuts[L"pause"] = L"Ctrl+Shift+X";
    s.spoolDirOverrides[L"\\\\LAIMI-PS"] = L"\\\\LAIMI-PS\\spool$";
    s.notifications = config::NotifyMode::All;
    s.ghostscriptPath = L"C:\\gs\\gswin64c.exe";
    s.columnWidths[L"user"] = 150;
    s.columnWidths[L"document"] = 5;  // fuera de rango: se ajusta al mínimo
    s.columnOrder = {L"user", L"document", L"status"};
    config::Settings r = config::Settings::FromJson(s.ToJson());
    // Ancho y orden de columnas. El ancho 5 vuelve como kMinColumnWidth (28):
    // FromJson acota los anchos a [kMinColumnWidth, kMaxColumnWidth] para que
    // un config.json editado a mano no deje una columna invisible.
    CHECK(config::Settings{}.columnOrder.empty());  // predeterminado: orden de fábrica
    CHECK(r.columnOrder == s.columnOrder);
    CHECK_EQ(r.columnWidths[L"user"], 150);
    CHECK_EQ(r.columnWidths[L"document"], config::kMinColumnWidth);
    // Resto de las preferencias: tema, idioma, servidores (incluido el local),
    // atajos, carpetas de spool, notificaciones y Ghostscript. laimiServer no
    // se asignó, así que debe seguir vacío (no se inventa uno al guardar).
    CHECK(r.theme == config::ThemeMode::Dark);
    CHECK_EQ(r.language, std::wstring(L"en"));
    CHECK_EQ(r.servers.size(), size_t(2));
    CHECK_EQ(r.activeServer, s.activeServer);
    CHECK_EQ(r.shortcuts[L"pause"], std::wstring(L"Ctrl+Shift+X"));
    CHECK_EQ(r.spoolDirOverrides[L"\\\\LAIMI-PS"], std::wstring(L"\\\\LAIMI-PS\\spool$"));
    CHECK(r.notifications == config::NotifyMode::All);
    CHECK_EQ(r.ghostscriptPath, s.ghostscriptPath);
    CHECK(r.laimiServer.empty());

    // Servidor del LAIMI (D54): el de config.json manda; sin él, el de
    // servidor-laimi.txt (no existe junto a cola_tests.exe) o el predeterminado.
    CHECK_EQ(config::LaimiServer(r), std::wstring(config::kDefaultLaimiServer));
    s.laimiServer = L"\\\\nuevo-laimi";
    r = config::Settings::FromJson(s.ToJson());
    CHECK_EQ(r.laimiServer, std::wstring(L"\\\\nuevo-laimi"));
    CHECK_EQ(config::LaimiServer(r), std::wstring(L"\\\\nuevo-laimi"));
}

// servidor-laimi.txt (D54): el archivo junto al .exe con el servidor del botón
// del LAIMI. Lo puede editar una persona a mano, así que ServerFromText tolera
// lo que deja un editor de texto y normaliza el nombre a la forma \\servidor.
TEST(LaimiServerFromText) {
    // Como lo escribe el Bloc de notas: con BOM, comentarios y CRLF.
    // "\xEF\xBB\xBF" es el BOM UTF-8; las líneas que empiezan con '#' y las
    // vacías se saltan; se recortan los espacios de la línea elegida.
    CHECK_EQ(config::ServerFromText("\xEF\xBB\xBF# comentario\r\n\r\n  \\\\otro-servidor  \r\n"),
             std::wstring(L"\\\\otro-servidor"));
    // Sin barras o con una sola: se completan las dos barras invertidas.
    CHECK_EQ(config::ServerFromText("otro-servidor"), std::wstring(L"\\\\otro-servidor"));
    CHECK_EQ(config::ServerFromText("\\otro"), std::wstring(L"\\\\otro"));
    // Sin ningún nombre (solo comentarios, o solo las barras): "" para que
    // LaimiServer pase al siguiente origen.
    CHECK(config::ServerFromText("# solo comentarios\n\n").empty());
    CHECK(config::ServerFromText("\\\\").empty());

    // Lo que guarda la app (menú "Cambiar servidor del LAIMI…") se vuelve a leer
    // igual; config.json (laimiServer) sigue mandando si tiene un valor.
    // El archivo se escribe de verdad junto a cola_tests.exe (carpeta build-*)
    // y se borra al final, para no afectar a otras pruebas ni a otra ejecución.
    // Si una CHECK falla en medio, DeleteFileW igual se ejecuta (CHECK no corta
    // la prueba).
    const std::wstring file = config::ExecutableDir() + L"\\" + config::kLaimiServerFile;
    CHECK(config::SaveLaimiServerFile(L"\\\\servidor-de-prueba"));
    // Orden de prioridad de LaimiServer: laimiServer de config.json, luego el
    // archivo, luego kDefaultLaimiServer (\\vtec-laimiprint).
    config::Settings s;
    CHECK_EQ(config::LaimiServer(s), std::wstring(L"\\\\servidor-de-prueba"));
    s.laimiServer = L"\\\\propio";
    CHECK_EQ(config::LaimiServer(s), std::wstring(L"\\\\propio"));
    // Sin archivo ni valor en config.json queda el servidor predeterminado.
    DeleteFileW(file.c_str());
    CHECK_EQ(config::LaimiServer(config::Settings{}), std::wstring(config::kDefaultLaimiServer));
}

// Un config.json con valores inválidos (editado a mano o de otra versión) no
// debe romper la app: cada campo fuera de rango o desconocido se corrige o
// vuelve a su valor predeterminado.
TEST(SettingsDefaultsOnGarbage) {
    config::JsonValue json = config::JsonValue::MakeObject();
    json[L"pollIntervalMs"] = 1;          // fuera de rango
    json[L"theme"] = L"purple";           // desconocido
    json[L"language"] = L"fr";            // no soportado
    config::Settings s = config::Settings::FromJson(json);
    // El sondeo (D5) se acota a [500, 60000] ms: 1 ms sube al mínimo (500), no
    // vuelve al predeterminado de 3000. Un sondeo de 1 ms saturaría el
    // servidor de impresión.
    CHECK_EQ(s.pollIntervalMs, 500);
    // Tema desconocido -> seguir al sistema; idioma no soportado -> español.
    CHECK(s.theme == config::ThemeMode::System);
    CHECK_EQ(s.language, std::wstring(L"es"));
}

// historial.json guarda como máximo N registros (2000 en la app; aquí 3) y
// descarta los más viejos. ComputeStats calcula las estadísticas que muestra la
// app: páginas de hoy y de los últimos 7 días, trabajos de hoy y páginas por
// usuario, contando solo los trabajos impresos.
TEST(HistoryStatsAndLimit) {
    // Ruta vacía: historial solo en memoria (no se lee ni se escribe archivo).
    config::History history(L"", 3);
    // Hora Unix fija (enero de 2027) para que la prueba no dependa del reloj.
    // Los registros están a segundos o minutos de "ahora", así que caen en el
    // mismo día local con cualquier zona horaria razonable.
    const std::int64_t now = 1'800'000'000;
    // Agrega un registro con hora, páginas, resultado y usuario.
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
    // Con 4 registros y límite 3 se descartó "old" (el más viejo), así que sus
    // 50 páginas no cuentan ni siquiera aunque estuviera en la semana.
    // Impresos que quedan: ana 3 + luis 2 = 5 hoy y en la semana; el cancelado
    // de ana (7 págs.) no suma páginas ni trabajos: ana = 3, trabajos hoy = 2.
    auto stats = history.ComputeStats(now);
    CHECK_EQ(stats.pagesToday, 5ull);
    CHECK_EQ(stats.pagesWeek, 5ull);
    CHECK_EQ(stats.pagesPerUser[L"ana"], 3ull);
    CHECK_EQ(stats.jobsToday, 2ull);
}
