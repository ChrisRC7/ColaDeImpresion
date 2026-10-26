// Pruebas del historial con páginas × copias al liberar un trabajo, del total
// de páginas de la selección y de la exportación del historial a Excel
// (ZIP, XLSX, hoja del historial y resumen).

#include <windows.h>
#include <winspool.h>

#include <string>

#include "TestFramework.h"
#include "app/HistoryExport.h"
#include "app/RowBuilder.h"
#include "config/History.h"
#include "core/JobTracker.h"
#include "core/PrintSettings.h"
#include "i18n/Strings.h"
#include "preview/JobFacts.h"
#include "preview/PdlSettings.h"
#include "preview/ZipReader.h"
#include "ui/ExportDialog.h"
#include "util/Strings.h"
#include "util/Xlsx.h"
#include "util/ZipWriter.h"

using namespace cola;

namespace {

// Trabajo RAW de una Epson del LAIMI: 2 páginas x 2 copias declaradas en el EJL
// del controlador; el DEVMODE del servidor dice 1 copia (el predeterminado de la cola).
core::JobInfo EpsonJob(std::uint32_t status) {
    DEVMODEW dm{};
    dm.dmSize = sizeof(DEVMODEW);
    dm.dmSpecVersion = DM_SPECVERSION;
    dm.dmFields = DM_COPIES;
    dm.dmCopies = 1;
    core::JobInfo job;
    job.id = 12;
    job.printer = L"\\\\SRV\\LAIMI 1 - INK";
    job.document = L"Tarea.pdf";
    job.user = L"ana";
    job.datatype = L"RAW";
    job.totalPages = 2;
    job.sizeBytes = 4096;
    job.rawStatus = status;
    job.settings = core::ParseDevMode(&dm);
    return job;
}

core::ContentFacts EpsonFacts() {
    std::string data =
        "\x1b\x01@EJL \n@EJL JI USER=\"x\" MACHINE=\"\\\\PC\" DOCUMENT=\"d\" DRIVER=\"EPSON AM-C6000 Series\"\n"
        "@EJL SE LA=ESC/PAGE\n@EJL SET PS=LT DX=OFF BD=LE CO=1 QT=2\n@EJL EN LA=ESC/PAGE-COLOR\n";
    data += std::string(64, '\0');
    data += "\x1b\x01@EJL \n@EJL JI PAGES=\"4\"\n\x1b\x01@EJL \n@EJL EJ \n";
    core::ContentFacts f;
    f.analyzed = f.complete = true;
    f.kind = core::DataKind::Pdl;
    preview::ParsePdlSettings(reinterpret_cast<const std::uint8_t*>(data.data()), data.size(), true, f);
    return f;
}

config::HistoryEntry Entry(std::int64_t time, config::JobOutcome outcome, const wchar_t* printPages,
                           const wchar_t* paper, const wchar_t* user) {
    config::HistoryEntry e;
    e.timeUnix = time;
    e.outcome = outcome;
    e.printPages = printPages;
    e.paper = paper;
    e.user = user;
    e.printer = L"\\\\SRV\\LAIMI 1";
    e.document = L"doc";
    e.duplex = L"No";
    return e;
}

}  // namespace

TEST(HistoryKeepsCopiesAfterRelease) {
    i18n::SetLanguage(i18n::Language::Spanish);
    const auto held = EpsonJob(JOB_STATUS_PAUSED);
    const auto printing = EpsonJob(JOB_STATUS_PRINTING);
    // Pausado o imprimiéndose es el mismo trabajo para la caché de datos analizados.
    CHECK_EQ(preview::JobFactsService::Key(held), preview::JobFactsService::Key(printing));
    CHECK(preview::JobFactsService::Key(held) != preview::JobFactsService::Key(EpsonJob(JOB_STATUS_SPOOLING)));

    const auto facts = EpsonFacts();
    CHECK_EQ(app::TotalPagesText(app::TotalPrintPages(held, &facts)), std::wstring(L"4"));
    // Sin los datos analizados el total cae a TotalPages x 1 copia: el "2" del historial.
    CHECK_EQ(app::TotalPagesText(app::TotalPrintPages(printing, nullptr)), std::wstring(L"2"));

    // Se analizó mientras estaba retenido; el operador lo libera (pasa a
    // imprimirse) y al salir de la cola el historial guarda lo mismo que la fila.
    preview::JobFactsService service(nullptr);
    service.Store(held, facts);
    service.Request({printing}, L"", L"");  // no lo lee (se está imprimiendo) ni lo olvida
    auto found = service.Find(printing);
    CHECK(found != nullptr);
    auto entry = app::MakeHistoryEntry(printing, config::JobOutcome::Printed, 1'800'000'000, found.get());
    CHECK_EQ(entry.printPages, std::wstring(L"4"));
    CHECK_EQ(entry.PrintPagesCount(), 4u);

    // Mientras se imprime el spooler puede informar otro tamaño (0 bytes al final,
    // visto con Microsoft Print to PDF): siguen valiendo los datos del trabajo.
    auto shrunk = printing;
    shrunk.sizeBytes = 0;
    service.Request({shrunk}, L"", L"");
    CHECK(service.Find(shrunk) != nullptr);
    // Otro trabajo con otro id no hereda nada.
    auto other = shrunk;
    other.id = 99;
    CHECK(service.Find(other) == nullptr);
    service.Request({}, L"", L"");  // salió de la cola: se olvida
    CHECK(service.Find(printing) == nullptr);
    CHECK(service.Find(shrunk) == nullptr);
}

TEST(TrackerKeepsWholeJobForHistory) {
    // Retenido con 4096 bytes y 2 páginas; al imprimirse el spooler informa 0;
    // al salir de la cola el evento describe el trabajo completo.
    core::QueueSnapshot snapshot;
    core::PrinterInfo printer;
    printer.name = EpsonJob(0).printer;
    snapshot.printers.push_back(printer);
    core::JobTracker tracker;
    auto held = EpsonJob(JOB_STATUS_PAUSED);
    held.state = core::JobState::Paused;
    snapshot.jobs = {held};
    tracker.Update(snapshot);
    auto printing = EpsonJob(JOB_STATUS_PRINTING);
    printing.state = core::JobState::Printing;
    printing.sizeBytes = 0;
    printing.totalPages = 0;
    snapshot.jobs = {printing};
    tracker.Update(snapshot);
    snapshot.jobs.clear();
    auto events = tracker.Update(snapshot);
    CHECK_EQ(events.size(), size_t(1));
    if (!events.empty()) {
        CHECK(events[0].kind == core::JobEvent::Kind::Finished);
        CHECK(events[0].outcome == config::JobOutcome::Printed);
        CHECK_EQ(events[0].job.sizeBytes, 4096ull);
        CHECK_EQ(events[0].job.totalPages, 2u);
    }
}

TEST(HistoryPrintPagesCount) {
    config::HistoryEntry e;
    e.pages = 3;
    CHECK_EQ(e.PrintPagesCount(), 3u);  // registro anterior: las del spooler
    e.printPages = L"12";
    CHECK_EQ(e.PrintPagesCount(), 12u);
    e.printPages = L"—";
    CHECK_EQ(e.PrintPagesCount(), 0u);  // no determinable

    // Las estadísticas cuentan como la columna Páginas.
    config::History history(L"", 10);
    const std::int64_t now = 1'800'000'000;
    history.Add(Entry(now - 60, config::JobOutcome::Printed, L"4", L"Carta", L"ana"));
    history.Add(Entry(now - 30, config::JobOutcome::Printed, L"—", L"Carta", L"ana"));
    auto stats = history.ComputeStats(now);
    CHECK_EQ(stats.pagesToday, 4ull);
    CHECK_EQ(stats.jobsToday, 2ull);
}

TEST(SelectionPagesTotal) {
    i18n::SetLanguage(i18n::Language::Spanish);
    const auto facts = EpsonFacts();
    const auto a = EpsonJob(JOB_STATUS_PAUSED);
    auto b = EpsonJob(JOB_STATUS_PAUSED);
    b.id = 13;
    core::JobInfo unknown;
    unknown.datatype = L"RAW";
    app::FactsLookup lookup = [&](const core::JobInfo& job) -> const core::ContentFacts* {
        return job.datatype == L"RAW" && job.totalPages ? &facts : nullptr;
    };
    auto total = app::SumPrintPages({&a, &b, &unknown}, lookup);
    CHECK_EQ(total.jobs, 3u);
    CHECK_EQ(total.pages, 8ull);
    CHECK_EQ(total.unknown, 1u);
    CHECK_EQ(app::SelectionText(total, false),
             std::wstring(L"Selección: 3 trabajo(s) · 8 páginas a imprimir (1 sin determinar)"));
    CHECK(app::SelectionText(app::PagesTotal{}, false).empty());

    auto e1 = Entry(1, config::JobOutcome::Printed, L"5", L"", L"x");
    auto e2 = Entry(2, config::JobOutcome::Printed, L"2", L"", L"x");
    auto history = app::SumHistoryPages({&e1, &e2});
    CHECK_EQ(history.pages, 7ull);
    CHECK_EQ(app::SelectionText(history, true), std::wstring(L"Selección: 2 trabajo(s) · 7 páginas"));
}

TEST(ZipWriterAndCrc) {
    CHECK_EQ(util::Crc32("123456789"), 0xCBF43926u);
    util::ZipWriter zip;
    zip.Add("a.txt", "hola");
    zip.Add("carpeta/b.xml", "<x/>");
    const std::string bytes = zip.Finish();
    auto names = preview::ListZipEntries(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    CHECK_EQ(names.size(), size_t(2));
    if (names.size() == 2) {
        CHECK_EQ(names[0], std::string("a.txt"));
        CHECK_EQ(names[1], std::string("carpeta/b.xml"));
    }
}

TEST(XlsxHelpers) {
    CHECK_EQ(util::xlsx::ColumnName(0), std::wstring(L"A"));
    CHECK_EQ(util::xlsx::ColumnName(25), std::wstring(L"Z"));
    CHECK_EQ(util::xlsx::ColumnName(26), std::wstring(L"AA"));
    CHECK_EQ(util::xlsx::ColumnName(701), std::wstring(L"ZZ"));
    CHECK_EQ(util::xlsx::ColumnName(702), std::wstring(L"AAA"));
    SYSTEMTIME t{};
    t.wYear = 2026;
    t.wMonth = 10;
    t.wDay = 9;
    t.wHour = 18;
    CHECK(util::xlsx::SerialDate(t) == 46304.75);
    CHECK_EQ(util::xlsx::SafeSheetName(L"Hist[o]ria:l*?/\\"), std::wstring(L"Historial"));
    CHECK_EQ(util::xlsx::SafeSheetName(std::wstring(40, L'x')).size(), size_t(31));

    util::xlsx::Sheet sheet;
    sheet.name = L"Datos";
    sheet.autoFilter = true;
    sheet.rows.push_back({util::xlsx::Cell::Text(L"<&> ñ"), util::xlsx::Cell::Number(1.5)});
    const std::string bytes = util::xlsx::BuildWorkbook({sheet, sheet});
    auto names = preview::ListZipEntries(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    bool second = false;
    for (const auto& n : names) second = second || n == "xl/worksheets/sheet2.xml";
    CHECK(second);
    CHECK(bytes.find("&lt;&amp;&gt; \xC3\xB1") != std::string::npos);  // escapado y en UTF-8
    CHECK(bytes.find("<v>1.5</v>") != std::string::npos);
    CHECK(bytes.find("_xlnm._FilterDatabase") != std::string::npos);

    // Filas fijas: título + encabezados.
    sheet.frozenRows = 2;
    const std::string frozen = util::xlsx::BuildWorkbook({sheet});
    CHECK(frozen.find("ySplit=\"2\" topLeftCell=\"A3\"") != std::string::npos);
}

TEST(HistorySummaryAndWorkbook) {
    i18n::SetLanguage(i18n::Language::Spanish);
    const std::int64_t now = 1'800'000'000;
    std::vector<config::HistoryEntry> entries;
    entries.push_back(Entry(now - 10, config::JobOutcome::Printed, L"4", L"Carta", L"ana"));
    entries.push_back(Entry(now - 20, config::JobOutcome::Printed, L"10", L"A3", L"luis"));
    entries.push_back(Entry(now - 30, config::JobOutcome::Printed, L"—", L"Carta", L"ana"));
    entries.push_back(Entry(now - 40, config::JobOutcome::Cancelled, L"7", L"Carta", L"ana"));
    entries.push_back(Entry(now - 50, config::JobOutcome::Printed, L"2", L"", L"ana"));

    const auto s = app::SummarizeHistory(entries);
    CHECK_EQ(s.records, 5ull);
    CHECK_EQ(s.Printed().jobs, 4ull);
    CHECK_EQ(s.Printed().pages, 16ull);
    CHECK_EQ(s.printedUnknownPages, 1ull);
    CHECK_EQ(s.byOutcome[static_cast<int>(config::JobOutcome::Cancelled)].pages, 7ull);
    CHECK_EQ(s.byPaper.at(L"Carta").pages, 4ull);  // el cancelado no cuenta
    CHECK_EQ(s.byPaper.at(L"Carta").jobs, 2ull);
    CHECK_EQ(s.byPaper.at(L"A3").pages, 10ull);
    CHECK_EQ(s.byPaper.at(L"").pages, 2ull);  // sin dato
    CHECK_EQ(s.byUser.at(L"ana").pages, 6ull);
    CHECK_EQ(s.largestPages, 10u);
    CHECK_EQ(s.firstUnix, now - 50);
    CHECK_EQ(s.lastUnix, now - 10);

    // Hoja del historial con el orden de columnas del operador (Documento primero).
    std::vector<std::wstring> order;
    for (const auto& c : app::HistoryColumns()) order.push_back(c.id);
    std::swap(order[0], order[1]);
    auto sheets = app::BuildHistoryWorkbook(entries, now, order);
    // Historial, Resumen y una hoja por tabla (resultado, papel, impresora, doble
    // cara, orientación, origen, usuario, equipo, día y hora).
    CHECK_EQ(sheets.size(), size_t(13));
    CHECK_EQ(sheets[0].rows.size(), entries.size() + 1);
    CHECK_EQ(sheets[0].rows[0][0].text, std::wstring(L"Documento"));
    CHECK_EQ(sheets[0].rows[0].back().text, std::wstring(L"Servidor"));
    // Páginas como número; "—" se conserva como texto.
    size_t pagesColumn = 0;
    for (size_t i = 0; i < sheets[0].rows[0].size(); ++i)
        if (sheets[0].rows[0][i].text == L"Páginas") pagesColumn = i;
    CHECK(sheets[0].rows[1][pagesColumn].kind == util::xlsx::Cell::Kind::Number);
    CHECK(sheets[0].rows[1][pagesColumn].number == 4.0);
    CHECK_EQ(sheets[0].rows[3][pagesColumn].text, std::wstring(L"—"));
    // El resumen general ya no lleva las tablas: cada una está en su hoja.
    CHECK_EQ(sheets[1].name, std::wstring(L"Resumen"));
    for (const auto& row : sheets[1].rows)
        CHECK(row.empty() || row[0].text != L"Páginas impresas por tipo de papel");
    const util::xlsx::Sheet* paper = nullptr;
    for (const auto& sheet : sheets)
        if (sheet.name == L"Por papel") paper = &sheet;
    CHECK(paper != nullptr);
    if (paper) {
        CHECK_EQ(paper->frozenRows, 2);
        CHECK_EQ(paper->rows[0][0].text, std::wstring(L"Páginas impresas por tipo de papel"));
        CHECK_EQ(paper->rows[1][0].text, std::wstring(L"Papel"));
        CHECK_EQ(paper->rows.size(), size_t(2 + 3 + 1));  // título, encabezados, A3/Carta/sin dato, total
        CHECK_EQ(paper->rows[2][0].text, std::wstring(L"A3"));  // la de más páginas primero
        CHECK(paper->rows.back()[2].number == 16.0);
    }
    for (const auto& sheet : sheets) CHECK(util::xlsx::SafeSheetName(sheet.name) == sheet.name);

    // Sin trabajos impresos las tablas por categoría no quedan en blanco: dicen por qué.
    std::vector<config::HistoryEntry> removed = {Entry(now, config::JobOutcome::Removed, L"3", L"Carta", L"ana")};
    auto empty = app::BuildHistoryWorkbook(removed, now);
    for (const auto& sheet : empty) {
        if (sheet.name != L"Por papel") continue;
        CHECK_EQ(sheet.rows.size(), size_t(4));  // título, encabezados, aviso, total
        CHECK(sheet.rows[2][0].text.find(L"No hay trabajos impresos") == 0);
    }
    CHECK(!util::xlsx::BuildWorkbook(sheets).empty());
    CHECK(app::DefaultExportFileName(now).find(L".xlsx") != std::wstring::npos);
}

TEST(HistoryExportDateRange) {
    i18n::SetLanguage(i18n::Language::Spanish);
    const std::int64_t now = 1'800'000'000;
    const std::int64_t day = 86'400;
    std::vector<config::HistoryEntry> entries;
    entries.push_back(Entry(now, config::JobOutcome::Printed, L"4", L"Carta", L"ana"));
    entries.push_back(Entry(now - 60, config::JobOutcome::Printed, L"1", L"Carta", L"ana"));
    entries.push_back(Entry(now - 2 * day, config::JobOutcome::Printed, L"2", L"A3", L"luis"));
    entries.push_back(Entry(now - 5 * day, config::JobOutcome::Cancelled, L"3", L"Carta", L"ana"));

    // Días con registros, del más antiguo al más reciente.
    const auto days = app::HistoryDays(entries);
    CHECK_EQ(days.size(), size_t(3));
    if (days.size() != 3) return;
    CHECK(days[0].key < days[1].key && days[1].key < days[2].key);
    CHECK_EQ(days[2].records, 2u);
    CHECK_EQ(app::DayDisplay(L"2026-10-09"), std::wstring(L"09/10/2026"));

    // Todo, un rango y un solo día.
    CHECK_EQ(app::FilterHistory(entries, {}).size(), size_t(4));
    const app::ExportPeriod lastTwo{days[1].key, days[2].key};
    const auto filtered = app::FilterHistory(entries, lastTwo);
    CHECK_EQ(filtered.size(), size_t(3));
    CHECK_EQ(filtered.front().timeUnix, now);  // conserva el orden
    CHECK_EQ(app::FilterHistory(entries, {days[0].key, days[0].key}).size(), size_t(1));

    CHECK_EQ(app::PeriodText({}), std::wstring(L"Todo el historial"));
    CHECK_EQ(app::PeriodText({L"2026-10-01", L"2026-10-09"}), std::wstring(L"Del 01/10/2026 al 09/10/2026"));
    CHECK_EQ(app::PeriodText({L"2026-10-09", L"2026-10-09"}), std::wstring(L"09/10/2026"));
    CHECK_EQ(app::DefaultExportFileName(now, {L"2026-10-01", L"2026-10-09"}),
             std::wstring(L"Historial de impresión 2026-10-01 a 2026-10-09.xlsx"));
    CHECK_EQ(app::DefaultExportFileName(now, {L"2026-10-09", L"2026-10-09"}),
             std::wstring(L"Historial de impresión 2026-10-09.xlsx"));

    // El resumen dice qué período se exportó.
    const auto sheets = app::BuildHistoryWorkbook(filtered, now, {}, lastTwo);
    bool periodLine = false;
    for (const auto& row : sheets[1].rows)
        if (row.size() > 1 && row[0].text == L"Período exportado")
            periodLine = row[1].text == app::PeriodText(lastTwo);
    CHECK(periodLine);
    CHECK_EQ(sheets[0].rows.size(), filtered.size() + 1);

    // Elección del diálogo: el extremo que se cambia arrastra al otro.
    std::vector<ui::ExportDay> choices = {{L"a", 1}, {L"b", 2}, {L"c", 4}};
    ui::ExportChoice choice;
    choice.all = false;
    choice.from = 2;
    choice.to = 1;
    ui::NormalizeExportChoice(choice, choices.size(), true);
    CHECK_EQ(choice.to, size_t(2));
    choice.from = 2;
    choice.to = 0;
    ui::NormalizeExportChoice(choice, choices.size(), false);
    CHECK_EQ(choice.from, size_t(0));
    CHECK_EQ(ui::ExportChoiceRecords(choices, choice), 1u);
    choice.to = 9;  // fuera de rango
    ui::NormalizeExportChoice(choice, choices.size(), false);
    CHECK_EQ(choice.to, size_t(2));
    CHECK_EQ(ui::ExportChoiceRecords(choices, choice), 7u);
    choice.all = true;
    CHECK_EQ(ui::ExportChoiceRecords(choices, choice), 7u);
}
