// Pruebas del carnet (D56): nombre de cuenta a partir del usuario del trabajo,
// filtro LDAP escapado, columna Carnet a la derecha de Usuario (cola,
// historial y orden guardado), búsqueda y orden por carnet, y carnets que se
// completan después en el historial. La consulta real al Active Directory no
// se prueba aquí (depende de la red y del dominio).

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

TEST(CarnetAccountName) {
    CHECK_EQ(core::AccountName(L"chrodriguez"), std::wstring(L"chrodriguez"));
    CHECK_EQ(core::AccountName(L"ESTUDIANTES\\chrodriguez"), std::wstring(L"chrodriguez"));
    CHECK_EQ(core::AccountName(L"chrodriguez@estudiantec.cr"), std::wstring(L"chrodriguez"));
    CHECK_EQ(core::AccountName(L"  ana  "), std::wstring(L"ana"));
    CHECK(core::AccountName(L"").empty());
    CHECK(core::AccountName(L"ESTUDIANTES\\").empty());
    // Caracteres que un sAMAccountName no admite: no se consulta.
    CHECK(core::AccountName(L"a*b").empty());
    CHECK(core::AccountName(L"a[b]").empty());
    CHECK(core::AccountName(std::wstring(65, L'a')).empty());
}

TEST(CarnetLdapFilter) {
    CHECK_EQ(core::LdapEscape(L"a*(b)\\c"), std::wstring(L"a\\2a\\28b\\29\\5cc"));
    CHECK_EQ(core::CarnetFilter({L"ana"}),
             std::wstring(L"(&(objectCategory=person)(objectClass=user)(sAMAccountName=ana))"));
    CHECK_EQ(core::CarnetFilter({L"ana", L"luis"}),
             std::wstring(L"(&(objectCategory=person)(objectClass=user)(|(sAMAccountName=ana)(sAMAccountName=luis)))"));
    CHECK_EQ(std::wstring(core::kCarnetAttribute), std::wstring(L"employeeID"));
}

TEST(CarnetColumnRightOfUser) {
    i18n::SetLanguage(i18n::Language::Spanish);
    auto after = [](const std::vector<ui::ListColumn>& columns, const wchar_t* id) {
        for (size_t i = 0; i + 1 < columns.size(); ++i)
            if (columns[i].id == id) return columns[i + 1].id;
        return std::wstring();
    };
    CHECK_EQ(after(app::JobColumns(true), L"user"), std::wstring(L"carnet"));
    CHECK_EQ(after(app::JobColumns(false), L"user"), std::wstring(L"carnet"));
    CHECK_EQ(after(app::HistoryColumns(), L"history.user"), std::wstring(L"history.carnet"));
    CHECK_EQ(app::JobColumns(false)[4].label, std::wstring(L"Carnet"));

    // Un orden guardado por el operador antes de esta columna: Carnet aparece
    // igual a la derecha de Usuario, aunque Usuario se haya movido.
    std::vector<std::wstring> saved;
    for (const auto& id : app::DefaultColumnOrder())
        if (id != L"carnet" && id != L"history.carnet") saved.push_back(id);
    saved.erase(std::find(saved.begin(), saved.end(), L"user"));
    saved.insert(saved.begin(), L"user");
    auto columns = app::JobColumns(true);
    auto display = ui::ColumnDisplayOrder(columns, app::EffectiveColumnOrder(saved));
    CHECK_EQ(columns[display[0]].id, std::wstring(L"user"));
    CHECK_EQ(columns[display[1]].id, std::wstring(L"carnet"));
}

TEST(CarnetCellsFilterAndSort) {
    i18n::SetLanguage(i18n::Language::Spanish);
    core::QueueSnapshot snapshot;
    snapshot.server = L"\\\\S";
    core::PrinterInfo printer;
    printer.name = L"\\\\S\\P1";
    snapshot.printers.push_back(printer);
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

    // Celda: el carnet o "—".
    auto columns = app::JobColumns(false);
    size_t index = 0;
    while (columns[index].id != L"carnet") ++index;
    CHECK_EQ(app::MakeJobRow(snapshot.jobs[1], false, nullptr, nullptr, L"2021000001").cells[index],
             std::wstring(L"2021000001"));
    CHECK_EQ(app::MakeJobRow(snapshot.jobs[2], false).cells[index], std::wstring(L"—"));

    // Buscar por carnet.
    auto found = app::VisibleJobs(snapshot, app::kAllPrinters, L"2023000002", app::SortBy::Queue, false, {}, lookup);
    CHECK_EQ(found.size(), size_t(1));
    CHECK_EQ(found[0]->user, std::wstring(L"luis"));

    // Ordenar por carnet (sin carnet primero).
    auto sorted = app::VisibleJobs(snapshot, app::kAllPrinters, L"", app::SortBy::Carnet, false, {}, lookup);
    CHECK_EQ(sorted.size(), size_t(3));
    CHECK_EQ(sorted[0]->user, std::wstring(L"sincarnet"));
    CHECK_EQ(sorted[1]->user, std::wstring(L"ana"));
    CHECK_EQ(sorted[2]->user, std::wstring(L"luis"));
    CHECK(app::SortByFromId(L"carnet") == app::SortBy::Carnet);
}

TEST(CarnetFilledLaterInHistory) {
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
    auto carnetOf = [](const std::wstring& user) { return user == L"ana" ? std::wstring(L"2021000001") : std::wstring(); };
    CHECK(history.FillCarnets(carnetOf));
    CHECK(!history.FillCarnets(carnetOf));  // ya no falta ninguno que se conozca
    for (const auto& e : history.Entries()) {
        if (e.user == L"ana") CHECK_EQ(e.carnet, std::wstring(L"2021000001"));
        if (e.user == L"luis") CHECK_EQ(e.carnet, std::wstring(L"2023000002"));
    }
    CHECK(app::HistoryMatchesFilter(history.Entries().back(), L"2021000001") ||
          app::HistoryMatchesFilter(history.Entries().front(), L"2021000001"));
}
