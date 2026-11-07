// Pruebas del carnet (D56): nombre de cuenta a partir del usuario del trabajo,
// filtro LDAP escapado, columna Carnet a la derecha de Usuario (cola,
// historial y orden guardado), búsqueda y orden por carnet, y carnets que se
// completan después en el historial. La consulta real al Active Directory no
// se prueba aquí (depende de la red y del dominio).
//
// Módulos probados: core/Directory (AccountName, LdapEscape, CarnetFilter,
// kCarnetAttribute), app/RowBuilder (JobColumns, HistoryColumns, MakeJobRow,
// VisibleJobs con CarnetLookup, SortBy::Carnet, DefaultColumnOrder,
// EffectiveColumnOrder, HistoryMatchesFilter), ui/ListColumns
// (ColumnDisplayOrder) y config/History (FillCarnets). Ninguna prueba abre una
// conexión LDAP: el Active Directory es solo de lectura y además no está
// disponible fuera del dominio del TEC.
//
// Los carnets usados (2021000001, 2023000002) son inventados, con el formato
// de 10 dígitos de los carnets del TEC.

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "TestFramework.h"
#include "app/RowBuilder.h"
#include "config/History.h"
#include "core/Directory.h"
#include "i18n/Strings.h"
#include "ui/ListColumns.h"

using namespace cola;

// El usuario de un trabajo (JOB_INFO_2.pUserName) puede llegar solo, con el
// dominio delante (DOMINIO\usuario) o como UPN (usuario@dominio), según cómo lo
// envió la PC del estudiante. AccountName debe dejar siempre el sAMAccountName
// con el que se busca en el directorio, y devolver "" para lo que no puede ser
// una cuenta, así ni siquiera se consulta (ahorra viajes al controlador y evita
// filtros raros).
TEST(CarnetAccountName) {
    // Las tres formas del mismo usuario dan la misma cuenta; los espacios de los
    // extremos se recortan.
    CHECK_EQ(core::AccountName(L"chrodriguez"), std::wstring(L"chrodriguez"));
    CHECK_EQ(core::AccountName(L"ESTUDIANTES\\chrodriguez"), std::wstring(L"chrodriguez"));
    CHECK_EQ(core::AccountName(L"chrodriguez@estudiantec.cr"), std::wstring(L"chrodriguez"));
    CHECK_EQ(core::AccountName(L"  ana  "), std::wstring(L"ana"));
    // Vacío, o solo el dominio sin usuario: no hay cuenta.
    CHECK(core::AccountName(L"").empty());
    CHECK(core::AccountName(L"ESTUDIANTES\\").empty());
    // Caracteres que un sAMAccountName no admite: no se consulta.
    CHECK(core::AccountName(L"a*b").empty());
    CHECK(core::AccountName(L"a[b]").empty());
    // Más de 64 caracteres: un sAMAccountName real tiene a lo sumo 20; el
    // límite de 64 deja margen y descarta textos que claramente no son cuentas.
    CHECK(core::AccountName(std::wstring(65, L'a')).empty());
}

// Filtro LDAP con el que CarnetService busca las cuentas por lotes (D56).
TEST(CarnetLdapFilter) {
    // Escape RFC 4515: '*' -> \2a, '(' -> \28, ')' -> \29 y '\' -> \5c (código
    // ASCII en hexadecimal). Sin esto un usuario con "*" sería un comodín y uno
    // con paréntesis podría alterar la estructura del filtro.
    CHECK_EQ(core::LdapEscape(L"a*(b)\\c"), std::wstring(L"a\\2a\\28b\\29\\5cc"));
    // Una sola cuenta: solo personas/usuarios (no equipos ni grupos, que también
    // tienen sAMAccountName) con ese nombre.
    CHECK_EQ(core::CarnetFilter({L"ana"}),
             std::wstring(L"(&(objectCategory=person)(objectClass=user)(sAMAccountName=ana))"));
    // Varias cuentas: una sola consulta con un OR (|...) de todas, que es lo que
    // permite pedir los carnets de una cola entera en un viaje.
    CHECK_EQ(core::CarnetFilter({L"ana", L"luis"}),
             std::wstring(L"(&(objectCategory=person)(objectClass=user)(|(sAMAccountName=ana)(sAMAccountName=luis)))"));
    // El carnet está en employeeID; employeeNumber es la cédula y no debe
    // usarse (verificado contra el dominio estudiantes.tec.cr, D56). Esta
    // comprobación evita que alguien lo cambie sin darse cuenta.
    CHECK_EQ(std::wstring(core::kCarnetAttribute), std::wstring(L"employeeID"));
}

// Pedido del usuario: la columna Carnet va justo a la derecha de Usuario, en la
// cola (con y sin la columna Impresora de la vista "Todas") y en el historial.
TEST(CarnetColumnRightOfUser) {
    // Los títulos de columna se traducen: se fija el español para comparar
    // "Carnet" (otras pruebas pueden haber dejado el inglés activo).
    i18n::SetLanguage(i18n::Language::Spanish);
    // id de la columna que sigue a `id` en la lista, o "" si no está o es la
    // última.
    auto after = [](const std::vector<ui::ListColumn>& columns, const wchar_t* id) {
        for (size_t i = 0; i + 1 < columns.size(); ++i)
            if (columns[i].id == id) return columns[i + 1].id;
        return std::wstring();
    };
    CHECK_EQ(after(app::JobColumns(true), L"user"), std::wstring(L"carnet"));
    CHECK_EQ(after(app::JobColumns(false), L"user"), std::wstring(L"carnet"));
    CHECK_EQ(after(app::HistoryColumns(), L"history.user"), std::wstring(L"history.carnet"));
    // Orden predeterminado: #, Documento, Estado, Usuario, Carnet... (índice 4).
    CHECK_EQ(app::JobColumns(false)[4].label, std::wstring(L"Carnet"));

    // Un orden guardado por el operador antes de esta columna: Carnet aparece
    // igual a la derecha de Usuario, aunque Usuario se haya movido.
    // Se simula un columnOrder de config.json de la 0.5.16: el orden
    // predeterminado sin las dos columnas de carnet (cola e historial) y con
    // Usuario movido al principio. EffectiveColumnOrder debe insertar "carnet"
    // detrás de la que la precede en el orden predeterminado (Usuario, D38).
    std::vector<std::wstring> saved;
    for (const auto& id : app::DefaultColumnOrder())
        if (id != L"carnet" && id != L"history.carnet") saved.push_back(id);
    saved.erase(std::find(saved.begin(), saved.end(), L"user"));
    saved.insert(saved.begin(), L"user");
    auto columns = app::JobColumns(true);
    auto display = ui::ColumnDisplayOrder(columns, app::EffectiveColumnOrder(saved));
    // display son índices en `columns` en el orden en que se dibujan.
    CHECK_EQ(columns[display[0]].id, std::wstring(L"user"));
    CHECK_EQ(columns[display[1]].id, std::wstring(L"carnet"));
}

// La celda Carnet, la búsqueda del cuadro de filtro y el orden por la columna
// Carnet usan el carnet que da CarnetLookup (en la app, la caché de
// CarnetService; aquí un mapa fijo).
TEST(CarnetCellsFilterAndSort) {
    i18n::SetLanguage(i18n::Language::Spanish);
    // Instantánea mínima: un servidor \\S con una impresora P1 y tres trabajos;
    // "sincarnet" representa una cuenta que el directorio no conoce o que no
    // tiene employeeID.
    core::QueueSnapshot snapshot;
    snapshot.server = L"\\\\S";
    core::PrinterInfo printer;
    printer.name = L"\\\\S\\P1";
    snapshot.printers.push_back(printer);
    // Trabajo de P1 con id = posición en la cola, así el orden de impresión es
    // 1, 2, 3 (luis, ana, sincarnet).
    auto job = [](std::uint32_t id, const wchar_t* user) {
        core::JobInfo j;
        j.id = id;
        j.printer = L"\\\\S\\P1";
        j.document = L"doc" + std::to_wstring(id);
        j.user = user;
        j.position = id;
        return j;
    };
    snapshot.jobs = {job(1, L"luis"), job(2, L"ana"), job(3, L"sincarnet")};
    const std::map<std::wstring, std::wstring> carnets = {{L"ana", L"2021000001"}, {L"luis", L"2023000002"}};
    app::CarnetLookup lookup = [&](const std::wstring& user) {
        auto it = carnets.find(user);
        return it != carnets.end() ? it->second : std::wstring();
    };

    // Celda: el carnet o "—". La columna se busca por id (no por índice fijo)
    // para que la prueba no dependa del orden de las columnas. El último
    // argumento de MakeJobRow es el carnet ya resuelto; sin él, "—".
    auto columns = app::JobColumns(false);
    size_t index = 0;
    while (columns[index].id != L"carnet") ++index;
    CHECK_EQ(app::MakeJobRow(snapshot.jobs[1], false, nullptr, nullptr, L"2021000001").cells[index],
             std::wstring(L"2021000001"));
    CHECK_EQ(app::MakeJobRow(snapshot.jobs[2], false).cells[index], std::wstring(L"—"));

    // Buscar por carnet: el texto del filtro coincide con el carnet de luis
    // aunque no aparezca en ningún campo del trabajo.
    auto found = app::VisibleJobs(snapshot, app::kAllPrinters, L"2023000002", app::SortBy::Queue, false, {}, lookup);
    CHECK_EQ(found.size(), size_t(1));
    CHECK_EQ(found[0]->user, std::wstring(L"luis"));

    // Ordenar por carnet (sin carnet primero). Orden ascendente de texto: ""
    // va antes que cualquier carnet, y 2021... antes que 2023..., así que el
    // resultado invierte el orden de impresión de ana y luis. SortByFromId
    // traduce el id guardado en config.json ("carnet") al criterio.
    auto sorted = app::VisibleJobs(snapshot, app::kAllPrinters, L"", app::SortBy::Carnet, false, {}, lookup);
    CHECK_EQ(sorted.size(), size_t(3));
    CHECK_EQ(sorted[0]->user, std::wstring(L"sincarnet"));
    CHECK_EQ(sorted[1]->user, std::wstring(L"ana"));
    CHECK_EQ(sorted[2]->user, std::wstring(L"luis"));
    CHECK(app::SortByFromId(L"carnet") == app::SortBy::Carnet);
}

// La respuesta del directorio llega después (hilo de fondo) y los registros
// anteriores a la 0.5.17 no traen carnet: History::FillCarnets completa los que
// faltan sin tocar los que ya lo tienen, y avisa si cambió algo (para guardar
// historial.json solo entonces).
TEST(CarnetFilledLaterInHistory) {
    // a: ana sin carnet (salió de la cola antes de la respuesta del directorio).
    // b: luis con su carnet ya guardado.
    config::History history;
    config::HistoryEntry a;
    a.user = L"ana";
    a.timeUnix = 1;
    config::HistoryEntry b;
    b.user = L"luis";
    b.carnet = L"2023000002";
    b.timeUnix = 2;
    history.Add(a);
    history.Add(b);
    // El "directorio" solo conoce a ana; para luis devuelve "" (no debe borrar
    // el carnet que ya tenía).
    auto carnetOf = [](const std::wstring& user) { return user == L"ana" ? std::wstring(L"2021000001") : std::wstring(); };
    CHECK(history.FillCarnets(carnetOf));
    CHECK(!history.FillCarnets(carnetOf));  // ya no falta ninguno que se conozca
    for (const auto& e : history.Entries()) {
        if (e.user == L"ana") CHECK_EQ(e.carnet, std::wstring(L"2021000001"));
        if (e.user == L"luis") CHECK_EQ(e.carnet, std::wstring(L"2023000002"));
    }
    // El filtro del historial también busca por carnet. Se prueba en los dos
    // extremos porque la prueba no supone en qué orden guarda History los
    // registros (más nuevo primero o al final).
    CHECK(app::HistoryMatchesFilter(history.Entries().back(), L"2021000001") ||
          app::HistoryMatchesFilter(history.Entries().front(), L"2021000001"));
}
