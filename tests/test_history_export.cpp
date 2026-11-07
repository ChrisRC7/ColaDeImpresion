// Pruebas del historial con páginas × copias al liberar un trabajo, del total
// de páginas de la selección y de la exportación del historial a Excel
// (ZIP, XLSX, hoja del historial y resumen).
//
// Módulos probados:
//   - preview/JobFacts (JobFactsService: clave de la caché, Store/Request/Find)
//     y core/JobTracker: que el historial guarde páginas × copias aunque el
//     trabajo cambie de estado o de tamaño al imprimirse (D41, D42).
//   - app/RowBuilder: TotalPrintPages, TotalPagesText, MakeHistoryEntry,
//     SumPrintPages/SumHistoryPages y SelectionText (total de la selección, D44).
//   - config/History: HistoryEntry::PrintPagesCount y estadísticas (D42).
//   - util/ZipWriter, util/Xlsx (escritor .xlsx propio, D43) y la lectura con
//     preview/ZipReader para comprobar el resultado.
//   - app/HistoryExport: resumen, hojas del libro, rango de fechas y nombre del
//     archivo (D43), y ui/ExportDialog: la lógica de la elección del rango.
// Los datos imitan un trabajo real de las Epson AM-C6000 del LAIMI (RAW con
// EJL, D29) porque fue con ellas que apareció el bug de D42.

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
// `status` es el JOB_STATUS_* crudo del spooler: la misma función sirve para
// representar el trabajo retenido, imprimiéndose o todavía recibiéndose.
core::JobInfo EpsonJob(std::uint32_t status) {
    // DEVMODE mínimo válido: dmFields = DM_COPIES declara que solo dmCopies
    // tiene un valor (ParseDevMode ignora los campos no declarados).
    DEVMODEW dm{};
    dm.dmSize = sizeof(DEVMODEW);
    dm.dmSpecVersion = DM_SPECVERSION;
    dm.dmFields = DM_COPIES;
    dm.dmCopies = 1;
    // totalPages = 2 es el TotalPages de JOB_INFO_2 (páginas sin copias) y
    // sizeBytes el tamaño de los datos en el spool.
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

// Datos analizados (ContentFacts) del trabajo de EpsonJob: un EJL como el que
// genera el controlador EPSON AM-C6000 (D29). Se arma así:
//   - "\x1b\x01@EJL" (ESC SOH @EJL) abre cada bloque EJL.
//   - JI ...: datos del trabajo (usuario, equipo, documento y controlador; el
//     nombre del controlador habilita las interpretaciones verificadas).
//   - SET PS=LT (papel carta), DX=OFF (una cara), BD=LE (borde largo),
//     CO=1 y QT=2 (QT = copias, verificado; CO se muestra crudo, D25).
//   - EN LA=ESC/PAGE-COLOR: empiezan los datos ESC/Page (aquí 64 nulos en vez
//     de bandas reales).
//   - Al final, JI PAGES="4": las hojas físicas (2 págs. × 2 copias) y EJ, fin
//     del trabajo. Solo se leen porque se pasa complete = true (todos los datos).
// Resultado esperado: 2 páginas × 2 copias = 4 páginas a imprimir.
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

// Registro de historial con lo que importa para el resumen y el Excel: hora de
// finalización (Unix), resultado, la columna Páginas como texto ya calculado
// (como se guarda desde la 0.5.0, D41; "—" = no determinable), papel y usuario.
// El resto queda fijo (misma impresora, una cara).
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

// Bug de D42: un trabajo de 2 págs. × 2 copias mostraba 4 en la cola y 2 en el
// historial. Al liberarlo pasaba a imprimirse, la clave de la caché cambiaba,
// sus datos analizados se olvidaban y MakeHistoryEntry caía a
// TotalPages × copias del DEVMODE (1 copia en el servidor). Esta prueba
// recorre ese camino: retenido -> imprimiéndose -> tamaño 0 -> sale de la cola.
TEST(HistoryKeepsCopiesAfterRelease) {
    i18n::SetLanguage(i18n::Language::Spanish);
    const auto held = EpsonJob(JOB_STATUS_PAUSED);
    const auto printing = EpsonJob(JOB_STATUS_PRINTING);
    // Pausado o imprimiéndose es el mismo trabajo para la caché de datos analizados.
    CHECK_EQ(preview::JobFactsService::Key(held), preview::JobFactsService::Key(printing));
    // En cambio, uno que todavía se recibe (SPOOLING) tiene otra clave: sus
    // datos están incompletos y hay que volver a analizarlo cuando termine.
    CHECK(preview::JobFactsService::Key(held) != preview::JobFactsService::Key(EpsonJob(JOB_STATUS_SPOOLING)));

    // Con los datos analizados la columna Páginas da 4 (copias del EJL).
    const auto facts = EpsonFacts();
    CHECK_EQ(app::TotalPagesText(app::TotalPrintPages(held, &facts)), std::wstring(L"4"));
    // Sin los datos analizados el total cae a TotalPages x 1 copia: el "2" del historial.
    CHECK_EQ(app::TotalPagesText(app::TotalPrintPages(printing, nullptr)), std::wstring(L"2"));

    // Se analizó mientras estaba retenido; el operador lo libera (pasa a
    // imprimirse) y al salir de la cola el historial guarda lo mismo que la fila.
    // Servicio sin callback de "listo" (nullptr): aquí solo interesa su caché.
    // Store simula que el análisis en segundo plano ya terminó.
    preview::JobFactsService service(nullptr);
    service.Store(held, facts);
    service.Request({printing}, L"", L"");  // no lo lee (se está imprimiendo) ni lo olvida
    // Request (como en cada instantánea) no debe leer un trabajo que se imprime
    // (D39) ni descartar su análisis: Find lo encuentra con el estado nuevo, y
    // el registro del historial guarda 4, igual que la fila.
    auto found = service.Find(printing);
    CHECK(found != nullptr);
    auto entry = app::MakeHistoryEntry(printing, config::JobOutcome::Printed, 1'800'000'000, found.get());
    CHECK_EQ(entry.printPages, std::wstring(L"4"));
    CHECK_EQ(entry.PrintPagesCount(), 4u);

    // Mientras se imprime el spooler puede informar otro tamaño (0 bytes al final,
    // visto con Microsoft Print to PDF): siguen valiendo los datos del trabajo.
    // El tamaño forma parte de la clave, así que Find tiene que recurrir al
    // último análisis con datos de esa impresora + id (IdKey, D42).
    auto shrunk = printing;
    shrunk.sizeBytes = 0;
    service.Request({shrunk}, L"", L"");
    CHECK(service.Find(shrunk) != nullptr);
    // Otro trabajo con otro id no hereda nada.
    auto other = shrunk;
    other.id = 99;
    CHECK(service.Find(other) == nullptr);
    service.Request({}, L"", L"");  // salió de la cola: se olvida
    // (Con la lista vacía se olvidan la entrada por clave y la de IdKey; si no,
    // la caché crecería sin límite y un id reutilizado heredaría datos viejos.)
    CHECK(service.Find(printing) == nullptr);
    CHECK(service.Find(shrunk) == nullptr);
}

// Segunda mitad de D42: JobTracker conserva el mayor Size/TotalPages visto de
// cada trabajo. Sin esto, el historial mostraba "—" en Tamaño para trabajos de
// "Microsoft Print to PDF", que bajan a 0 bytes al imprimirse.
TEST(TrackerKeepsWholeJobForHistory) {
    // Retenido con 4096 bytes y 2 páginas; al imprimirse el spooler informa 0;
    // al salir de la cola el evento describe el trabajo completo.
    // Tres instantáneas seguidas de la misma impresora: retenido, imprimiéndose
    // con tamaño y páginas en 0, y sin el trabajo.
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
    // Un solo evento "terminó", con resultado Impreso (se lo vio imprimiéndose
    // con la impresora operativa, heurística de D13) y con el tamaño y las
    // páginas de cuando estaba retenido, no los 0 de la última instantánea.
    auto events = tracker.Update(snapshot);
    CHECK_EQ(events.size(), size_t(1));
    if (!events.empty()) {
        CHECK(events[0].kind == core::JobEvent::Kind::Finished);
        CHECK(events[0].outcome == config::JobOutcome::Printed);
        CHECK_EQ(events[0].job.sizeBytes, 4096ull);
        CHECK_EQ(events[0].job.totalPages, 2u);
    }
}

// PrintPagesCount es el número que usan las estadísticas: el de la columna
// Páginas guardada (printPages, páginas × copias) y, en registros anteriores a
// la 0.5.0 que no la tienen, las páginas del spooler (pages). "—" cuenta 0.
TEST(HistoryPrintPagesCount) {
    config::HistoryEntry e;
    e.pages = 3;
    CHECK_EQ(e.PrintPagesCount(), 3u);  // registro anterior: las del spooler
    e.printPages = L"12";
    CHECK_EQ(e.PrintPagesCount(), 12u);
    e.printPages = L"—";
    CHECK_EQ(e.PrintPagesCount(), 0u);  // no determinable

    // Las estadísticas cuentan como la columna Páginas.
    // Dos impresos de hoy: uno con 4 y otro no determinable. Las páginas de hoy
    // son 4 (el "—" no suma) pero los trabajos de hoy son 2.
    config::History history(L"", 10);
    const std::int64_t now = 1'800'000'000;
    history.Add(Entry(now - 60, config::JobOutcome::Printed, L"4", L"Carta", L"ana"));
    history.Add(Entry(now - 30, config::JobOutcome::Printed, L"—", L"Carta", L"ana"));
    auto stats = history.ComputeStats(now);
    CHECK_EQ(stats.pagesToday, 4ull);
    CHECK_EQ(stats.jobsToday, 2ull);
}

// Total de páginas de la selección en la barra inferior (D44): suma la columna
// Páginas de los trabajos seleccionados y cuenta aparte los que no se pueden
// determinar, para que el operador sepa cuánto va a imprimir antes de liberar.
TEST(SelectionPagesTotal) {
    i18n::SetLanguage(i18n::Language::Spanish);
    // a y b: dos trabajos Epson analizados (4 páginas cada uno).
    // unknown: RAW sin TotalPages ni datos analizados: su Páginas es "—".
    const auto facts = EpsonFacts();
    const auto a = EpsonJob(JOB_STATUS_PAUSED);
    auto b = EpsonJob(JOB_STATUS_PAUSED);
    b.id = 13;
    core::JobInfo unknown;
    unknown.datatype = L"RAW";
    // Hace de JobFactsService::Find: solo hay análisis para los que tienen
    // páginas (a y b).
    app::FactsLookup lookup = [&](const core::JobInfo& job) -> const core::ContentFacts* {
        return job.datatype == L"RAW" && job.totalPages ? &facts : nullptr;
    };
    // 3 trabajos, 4 + 4 = 8 páginas y 1 sin determinar. En la cola el texto dice
    // "a imprimir" (false = no es el historial); sin selección, texto vacío.
    auto total = app::SumPrintPages({&a, &b, &unknown}, lookup);
    CHECK_EQ(total.jobs, 3u);
    CHECK_EQ(total.pages, 8ull);
    CHECK_EQ(total.unknown, 1u);
    CHECK_EQ(app::SelectionText(total, false),
             std::wstring(L"Selección: 3 trabajo(s) · 8 páginas a imprimir (1 sin determinar)"));
    CHECK(app::SelectionText(app::PagesTotal{}, false).empty());

    // En el historial se suma la columna Páginas guardada (5 + 2) y el texto
    // no dice "a imprimir" (true = historial).
    auto e1 = Entry(1, config::JobOutcome::Printed, L"5", L"", L"x");
    auto e2 = Entry(2, config::JobOutcome::Printed, L"2", L"", L"x");
    auto history = app::SumHistoryPages({&e1, &e2});
    CHECK_EQ(history.pages, 7ull);
    CHECK_EQ(app::SelectionText(history, true), std::wstring(L"Selección: 2 trabajo(s) · 7 páginas"));
}

// ZIP sin compresión que sirve de contenedor del .xlsx (D43).
TEST(ZipWriterAndCrc) {
    // 0xCBF43926 es el valor de control estándar del CRC-32 (el de ZIP,
    // polinomio 0xEDB88320) para el texto "123456789". Si el CRC está mal,
    // Excel considera el archivo dañado y ofrece "repararlo".
    CHECK_EQ(util::Crc32("123456789"), 0xCBF43926u);
    // Lo escrito por ZipWriter debe poder leerlo el lector de ZIP propio
    // (directorio central correcto), con los nombres en el orden agregado y
    // las subcarpetas en el nombre ("carpeta/b.xml").
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

// Piezas del escritor .xlsx propio (util/Xlsx, D43).
TEST(XlsxHelpers) {
    // Nombre de columna de Excel a partir del índice (base 0): es biyectivo en
    // base 26 sin cero, así que después de Z viene AA (26), después de ZZ (701)
    // viene AAA (702).
    CHECK_EQ(util::xlsx::ColumnName(0), std::wstring(L"A"));
    CHECK_EQ(util::xlsx::ColumnName(25), std::wstring(L"Z"));
    CHECK_EQ(util::xlsx::ColumnName(26), std::wstring(L"AA"));
    CHECK_EQ(util::xlsx::ColumnName(701), std::wstring(L"ZZ"));
    CHECK_EQ(util::xlsx::ColumnName(702), std::wstring(L"AAA"));
    // Fecha de Excel: días desde el 30/12/1899 con la hora como fracción. El
    // 9/10/2026 es el día 46304 y las 18:00 son 0,75 de día. Se compara con ==
    // porque 0,75 es exacto en binario.
    SYSTEMTIME t{};
    t.wYear = 2026;
    t.wMonth = 10;
    t.wDay = 9;
    t.wHour = 18;
    CHECK(util::xlsx::SerialDate(t) == 46304.75);
    // Nombres de hoja: Excel no admite [ ] : * ? / \ y limita a 31 caracteres;
    // un nombre inválido hace que Excel diga que el archivo está dañado.
    CHECK_EQ(util::xlsx::SafeSheetName(L"Hist[o]ria:l*?/\\"), std::wstring(L"Historial"));
    CHECK_EQ(util::xlsx::SafeSheetName(std::wstring(40, L'x')).size(), size_t(31));

    // Libro con dos hojas iguales (texto con caracteres especiales de XML y
    // una ñ, más un número) y autofiltro.
    util::xlsx::Sheet sheet;
    sheet.name = L"Datos";
    sheet.autoFilter = true;
    sheet.rows.push_back({util::xlsx::Cell::Text(L"<&> ñ"), util::xlsx::Cell::Number(1.5)});
    const std::string bytes = util::xlsx::BuildWorkbook({sheet, sheet});
    auto names = preview::ListZipEntries(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    // Cada hoja es una parte xl/worksheets/sheetN.xml dentro del ZIP.
    bool second = false;
    for (const auto& n : names) second = second || n == "xl/worksheets/sheet2.xml";
    CHECK(second);
    // Como el ZIP no comprime, el XML se puede buscar directamente en los
    // bytes. "\xC3\xB1" es la ñ en UTF-8; < & > deben ir como entidades.
    CHECK(bytes.find("&lt;&amp;&gt; \xC3\xB1") != std::string::npos);  // escapado y en UTF-8
    // El número va como valor numérico (<v>) con punto decimal, no como texto.
    CHECK(bytes.find("<v>1.5</v>") != std::string::npos);
    // El autofiltro necesita el nombre definido _xlnm._FilterDatabase en el
    // libro; sin él Excel no muestra las flechas de filtro.
    CHECK(bytes.find("_xlnm._FilterDatabase") != std::string::npos);

    // Filas fijas: título + encabezados.
    // Inmovilizar 2 filas se escribe como un panel con ySplit="2" cuya parte
    // desplazable empieza en A3 (las hojas "Por ..." usan frozenRows = 2).
    sheet.frozenRows = 2;
    const std::string frozen = util::xlsx::BuildWorkbook({sheet});
    CHECK(frozen.find("ySplit=\"2\" topLeftCell=\"A3\"") != std::string::npos);
}

// Resumen del historial y libro de Excel completo (D43).
// Cinco registros de prueba (más reciente primero):
//   ana  Carta 4 impreso | luis A3 10 impreso | ana Carta "—" impreso |
//   ana  Carta 7 cancelado | ana sin papel 2 impreso.
TEST(HistorySummaryAndWorkbook) {
    i18n::SetLanguage(i18n::Language::Spanish);
    const std::int64_t now = 1'800'000'000;
    std::vector<config::HistoryEntry> entries;
    entries.push_back(Entry(now - 10, config::JobOutcome::Printed, L"4", L"Carta", L"ana"));
    entries.push_back(Entry(now - 20, config::JobOutcome::Printed, L"10", L"A3", L"luis"));
    entries.push_back(Entry(now - 30, config::JobOutcome::Printed, L"—", L"Carta", L"ana"));
    entries.push_back(Entry(now - 40, config::JobOutcome::Cancelled, L"7", L"Carta", L"ana"));
    entries.push_back(Entry(now - 50, config::JobOutcome::Printed, L"2", L"", L"ana"));

    // Totales esperados: 4 impresos con 4 + 10 + 2 = 16 páginas y 1 impreso sin
    // páginas conocidas ("—"); el cancelado (7) solo cuenta en su resultado.
    // Por papel solo cuentan los impresos: Carta 4 págs. en 2 trabajos (el "—"
    // suma trabajo pero no páginas), A3 10 y "" (sin dato) 2. ana: 4 + 2 = 6.
    // El trabajo más grande tiene 10 páginas; el período va de now-50 a now-10.
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
    // Se simula un columnOrder (D38) intercambiando las dos primeras columnas
    // del historial; la hoja debe seguirlo y terminar con Servidor (columna
    // extra que solo existe en el Excel).
    std::vector<std::wstring> order;
    for (const auto& c : app::HistoryColumns()) order.push_back(c.id);
    std::swap(order[0], order[1]);
    auto sheets = app::BuildHistoryWorkbook(entries, now, order);
    // Historial, Resumen y una hoja por tabla (resultado, papel, impresora, doble
    // cara, orientación, origen, usuario, equipo, día y hora).
    // 13 = Historial + Resumen + 11 tablas: las 10 de la lista de arriba más
    // "Por presentación" (D55).
    CHECK_EQ(sheets.size(), size_t(13));
    // Una fila de encabezados + una por registro.
    CHECK_EQ(sheets[0].rows.size(), entries.size() + 1);
    CHECK_EQ(sheets[0].rows[0][0].text, std::wstring(L"Documento"));
    CHECK_EQ(sheets[0].rows[0].back().text, std::wstring(L"Servidor"));
    // Páginas como número; "—" se conserva como texto.
    // (Como número, Excel puede sumarla y filtrarla; la fila 1 es el primer
    // registro, con 4, y la 3 el de "—".)
    size_t pagesColumn = 0;
    for (size_t i = 0; i < sheets[0].rows[0].size(); ++i)
        if (sheets[0].rows[0][i].text == L"Páginas") pagesColumn = i;
    CHECK(sheets[0].rows[1][pagesColumn].kind == util::xlsx::Cell::Kind::Number);
    CHECK(sheets[0].rows[1][pagesColumn].number == 4.0);
    CHECK_EQ(sheets[0].rows[3][pagesColumn].text, std::wstring(L"—"));
    // El resumen general ya no lleva las tablas: cada una está en su hoja.
    // (Desde la 0.5.4: con todas una debajo de otra había que bajar mucho para
    // llegar a la siguiente.) Se busca el título de la tabla de papel en el
    // Resumen y no debe estar; luego se revisa la hoja "Por papel".
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
        // Fila de total: la tercera columna (páginas) suma los 16 impresos.
        CHECK(paper->rows.back()[2].number == 16.0);
    }
    // Ningún nombre de hoja debe necesitar corrección (Excel rechazaría el
    // archivo si alguno fuera inválido o pasara de 31 caracteres).
    for (const auto& sheet : sheets) CHECK(util::xlsx::SafeSheetName(sheet.name) == sheet.name);

    // Sin trabajos impresos las tablas por categoría no quedan en blanco: dicen por qué.
    // Caso real de D47: en el historial.json del usuario había 0 impresos (todo
    // "Retirado") y las tablas del Excel salían vacías sin explicación.
    std::vector<config::HistoryEntry> removed = {Entry(now, config::JobOutcome::Removed, L"3", L"Carta", L"ana")};
    auto empty = app::BuildHistoryWorkbook(removed, now);
    for (const auto& sheet : empty) {
        if (sheet.name != L"Por papel") continue;
        CHECK_EQ(sheet.rows.size(), size_t(4));  // título, encabezados, aviso, total
        CHECK(sheet.rows[2][0].text.find(L"No hay trabajos impresos") == 0);
    }
    // El libro completo se arma sin errores y el nombre sugerido es un .xlsx.
    CHECK(!util::xlsx::BuildWorkbook(sheets).empty());
    CHECK(app::DefaultExportFileName(now).find(L".xlsx") != std::wstring::npos);
}

// Exportar todo el historial o un rango de fechas (0.5.6, D43): días con
// registros, filtro por rango, textos del período, nombre del archivo y la
// lógica del diálogo ui/ExportDialog.
TEST(HistoryExportDateRange) {
    i18n::SetLanguage(i18n::Language::Spanish);
    const std::int64_t now = 1'800'000'000;
    const std::int64_t day = 86'400;
    // Cuatro registros en tres días distintos: dos de hoy (now y now-60),
    // uno de hace 2 días y uno de hace 5 (cancelado: también cuenta como
    // registro del día, el filtro no mira el resultado).
    std::vector<config::HistoryEntry> entries;
    entries.push_back(Entry(now, config::JobOutcome::Printed, L"4", L"Carta", L"ana"));
    entries.push_back(Entry(now - 60, config::JobOutcome::Printed, L"1", L"Carta", L"ana"));
    entries.push_back(Entry(now - 2 * day, config::JobOutcome::Printed, L"2", L"A3", L"luis"));
    entries.push_back(Entry(now - 5 * day, config::JobOutcome::Cancelled, L"3", L"Carta", L"ana"));

    // Días con registros, del más antiguo al más reciente.
    // (Los extremos del diálogo solo pueden ser estos días. La clave es la
    // fecha local "AAAA-MM-DD", que se ordena bien como texto; DayDisplay la
    // muestra como DD/MM/AAAA.) Si no hay 3 días, los índices de abajo no
    // serían válidos: se sale para no leer fuera del vector.
    const auto days = app::HistoryDays(entries);
    CHECK_EQ(days.size(), size_t(3));
    if (days.size() != 3) return;
    CHECK(days[0].key < days[1].key && days[1].key < days[2].key);
    CHECK_EQ(days[2].records, 2u);
    CHECK_EQ(app::DayDisplay(L"2026-10-09"), std::wstring(L"09/10/2026"));

    // Todo, un rango y un solo día.
    // ExportPeriod vacío ({}) = todo el historial. Los dos días más recientes
    // dejan 3 registros (los extremos se incluyen); el día más antiguo, 1.
    CHECK_EQ(app::FilterHistory(entries, {}).size(), size_t(4));
    const app::ExportPeriod lastTwo{days[1].key, days[2].key};
    const auto filtered = app::FilterHistory(entries, lastTwo);
    CHECK_EQ(filtered.size(), size_t(3));
    CHECK_EQ(filtered.front().timeUnix, now);  // conserva el orden
    CHECK_EQ(app::FilterHistory(entries, {days[0].key, days[0].key}).size(), size_t(1));

    // Texto del período (en el Resumen) y nombre sugerido del archivo: con un
    // solo día no se repite la fecha.
    CHECK_EQ(app::PeriodText({}), std::wstring(L"Todo el historial"));
    CHECK_EQ(app::PeriodText({L"2026-10-01", L"2026-10-09"}), std::wstring(L"Del 01/10/2026 al 09/10/2026"));
    CHECK_EQ(app::PeriodText({L"2026-10-09", L"2026-10-09"}), std::wstring(L"09/10/2026"));
    CHECK_EQ(app::DefaultExportFileName(now, {L"2026-10-01", L"2026-10-09"}),
             std::wstring(L"Historial de impresión 2026-10-01 a 2026-10-09.xlsx"));
    CHECK_EQ(app::DefaultExportFileName(now, {L"2026-10-09", L"2026-10-09"}),
             std::wstring(L"Historial de impresión 2026-10-09.xlsx"));

    // El resumen dice qué período se exportó.
    // Se busca la fila "Período exportado" en la hoja Resumen (sheets[1]); la
    // hoja Historial (sheets[0]) solo tiene los registros filtrados.
    const auto sheets = app::BuildHistoryWorkbook(filtered, now, {}, lastTwo);
    bool periodLine = false;
    for (const auto& row : sheets[1].rows)
        if (row.size() > 1 && row[0].text == L"Período exportado")
            periodLine = row[1].text == app::PeriodText(lastTwo);
    CHECK(periodLine);
    CHECK_EQ(sheets[0].rows.size(), filtered.size() + 1);

    // Elección del diálogo: el extremo que se cambia arrastra al otro.
    // Tres días con 1, 2 y 4 registros (índices 0, 1 y 2).
    std::vector<ui::ExportDay> choices = {{L"a", 1}, {L"b", 2}, {L"c", 4}};
    ui::ExportChoice choice;
    choice.all = false;
    // Se cambió "desde" (true) a un día posterior a "hasta": "hasta" lo sigue.
    choice.from = 2;
    choice.to = 1;
    ui::NormalizeExportChoice(choice, choices.size(), true);
    CHECK_EQ(choice.to, size_t(2));
    // Se cambió "hasta" (false) a un día anterior a "desde": "desde" baja
    // hasta él; queda solo el día 0, con 1 registro.
    choice.from = 2;
    choice.to = 0;
    ui::NormalizeExportChoice(choice, choices.size(), false);
    CHECK_EQ(choice.from, size_t(0));
    CHECK_EQ(ui::ExportChoiceRecords(choices, choice), 1u);
    // Un índice fuera de la lista se lleva al último día: 0..2 = 1 + 2 + 4 = 7.
    choice.to = 9;  // fuera de rango
    ui::NormalizeExportChoice(choice, choices.size(), false);
    CHECK_EQ(choice.to, size_t(2));
    CHECK_EQ(ui::ExportChoiceRecords(choices, choice), 7u);
    // "Todo el historial" cuenta todos los registros sin mirar from/to.
    choice.all = true;
    CHECK_EQ(ui::ExportChoiceRecords(choices, choice), 7u);
}
