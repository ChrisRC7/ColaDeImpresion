// Pruebas de la configuración de impresión de un trabajo: DEVMODE, separación
// páginas / copias / caras / hojas con su fiabilidad, opciones PJL/EJL y
// PrintTicket, ficha "Configuración" y archivos temporales privados.

#include <windows.h>
#include <winspool.h>

#include <cstddef>
#include <cstring>
#include <string>

#include "TestFramework.h"
#include "app/JobSheet.h"
#include "app/RowBuilder.h"
#include "config/History.h"
#include "core/PageAccounting.h"
#include "core/PrintSettings.h"
#include "core/Spooler.h"
#include "i18n/Strings.h"
#include "preview/PdlSettings.h"
#include "preview/SpoolFile.h"
#include "preview/TempFiles.h"
#include "preview/XpsPackage.h"
#include "util/Format.h"

using namespace cola;
using core::Reliability;

namespace {

DEVMODEW MakeDevMode() {
    DEVMODEW dm{};
    dm.dmSize = sizeof(DEVMODEW);
    dm.dmSpecVersion = DM_SPECVERSION;
    return dm;
}

// Trabajo EMF terminado de recibir, con un DEVMODE dado.
core::JobInfo EmfJob(std::uint32_t totalPages, const DEVMODEW& dm) {
    core::JobInfo job;
    job.id = 7;
    job.printer = L"\\\\SRV\\Lab";
    job.datatype = L"NT EMF 1.008";
    job.printProcessor = L"winprint";
    job.totalPages = totalPages;
    job.settings = core::ParseDevMode(&dm);
    return job;
}

core::ContentFacts EmfFacts(std::uint32_t pages, std::uint32_t repeatPeriod) {
    core::ContentFacts f;
    f.analyzed = f.complete = true;
    f.kind = core::DataKind::Emf;
    f.language = L"EMF";
    f.pageSource = core::ContentFacts::PageSource::EmfRecords;
    f.pages = pages;
    f.repeatChecked = true;
    f.repeatPeriod = repeatPeriod;
    return f;
}

std::vector<std::uint8_t> Bytes(const std::string& text) { return {text.begin(), text.end()}; }

}  // namespace

// --- DEVMODE --------------------------------------------------------------

TEST(DevModeUsesOnlyDeclaredFields) {
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_COPIES | DM_PAPERSIZE | DM_ORIENTATION;
    dm.dmCopies = 10;
    dm.dmPaperSize = DMPAPER_LETTER;
    dm.dmOrientation = DMORIENT_LANDSCAPE;
    dm.dmDuplex = DMDUP_VERTICAL;  // no declarado en dmFields: no debe usarse
    auto s = core::ParseDevMode(&dm);
    CHECK(s.present);
    CHECK(s.copies.valid);
    CHECK_EQ(s.copies.value, 10);
    CHECK(s.paperSize.valid);
    CHECK(s.orientation.valid);
    CHECK(!s.duplex.valid);
    CHECK(!s.collate.valid);
}

TEST(DevModeRespectsDmSize) {
    DEVMODEW dm = MakeDevMode();
    // DEVMODE antiguo / truncado: termina justo antes de dmFormName.
    dm.dmSize = static_cast<WORD>(offsetof(DEVMODEW, dmFormName));
    dm.dmFields = DM_COLLATE | DM_FORMNAME | DM_NUP;
    dm.dmCollate = DMCOLLATE_TRUE;
    wcscpy(dm.dmFormName, L"Letter");
    auto s = core::ParseDevMode(&dm);
    CHECK(s.collate.valid);      // está antes de dmFormName: cabe
    CHECK(!s.formName.valid);    // el bit está puesto pero el campo no cabe en dmSize
    CHECK(!s.nup.valid);
    CHECK(!core::ParseDevMode(nullptr).present);
}

TEST(DevModeRejectsOutOfRangeValues) {
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_COPIES | DM_DUPLEX | DM_ORIENTATION;
    dm.dmCopies = 0;
    dm.dmDuplex = 7;
    dm.dmOrientation = 5;
    auto s = core::ParseDevMode(&dm);
    CHECK(!s.copies.valid);
    CHECK(!s.duplex.valid);
    CHECK(!s.orientation.valid);
}

TEST(EffectivePaperStandardAndCustom) {
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_PAPERSIZE;
    dm.dmPaperSize = DMPAPER_A4;
    auto a4 = core::EffectivePaper(core::ParseDevMode(&dm));
    CHECK(a4.known && !a4.custom);
    CHECK_EQ(a4.width, 2100);
    CHECK_EQ(a4.length, 2970);

    dm.dmFields = DM_PAPERSIZE | DM_PAPERWIDTH | DM_PAPERLENGTH;
    dm.dmPaperSize = DMPAPER_USER;
    dm.dmPaperWidth = 1000;   // 100 mm
    dm.dmPaperLength = 1500;  // 150 mm
    auto custom = core::EffectivePaper(core::ParseDevMode(&dm));
    CHECK(custom.known && custom.custom);
    CHECK_EQ(custom.width, 1000);
    CHECK_EQ(custom.length, 1500);

    i18n::SetLanguage(i18n::Language::Spanish);
    CHECK_EQ(app::PaperText(core::ParseDevMode(&dm)), std::wstring(L"Personalizado — 100,0 × 150,0 mm"));
}

TEST(SameVisibleSettingsComparesJobAndQueue) {
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_COPIES | DM_PAPERSIZE;
    dm.dmCopies = 1;
    dm.dmPaperSize = DMPAPER_LETTER;
    auto queue = core::ParseDevMode(&dm);
    CHECK(core::SameVisibleSettings(queue, queue));
    dm.dmCopies = 2;
    CHECK(!core::SameVisibleSettings(queue, core::ParseDevMode(&dm)));
}

// --- Páginas / copias / caras / hojas ---------------------------------------

TEST(RepeatPeriodDetection) {
    CHECK_EQ(core::RepeatPeriod({1, 1, 1, 1}), 1u);
    CHECK_EQ(core::RepeatPeriod({1, 2, 1, 2, 1, 2}), 2u);
    CHECK_EQ(core::RepeatPeriod({1, 2, 3}), 0u);
    CHECK_EQ(core::RepeatPeriod({1, 2, 1}), 0u);
    CHECK_EQ(core::RepeatPeriod({1}), 0u);
    CHECK_EQ(core::RepeatPeriod({}), 0u);
}

TEST(OnePageTenDriverCopiesIsReliable) {
    // Caso del enunciado: 1 página × 10 copias que aplica el procesador de impresión.
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_COPIES | DM_DUPLEX;
    dm.dmCopies = 10;
    dm.dmDuplex = DMDUP_SIMPLEX;
    auto job = EmfJob(1, dm);
    auto facts = EmfFacts(1, 0);
    auto acc = core::ComputePageAccounting(job, &facts);
    CHECK_EQ(acc.logicalPages.value, 1u);
    CHECK(acc.logicalPages.reliability == Reliability::Reliable);
    CHECK_EQ(acc.copies.value, 10u);
    CHECK(acc.copies.reliability == Reliability::Reliable);
    CHECK_EQ(acc.sides.value, 10u);
    CHECK(acc.sides.reliability == Reliability::Reliable);
    CHECK_EQ(acc.sheets.value, 10u);
    CHECK(acc.sheets.reliability == Reliability::Reliable);
}

TEST(AppRepeatedPagesAreNotMultipliedAgain) {
    // La aplicación repitió la página 10 veces y dejó dmCopies = 1: TotalPages ya incluye las copias.
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_COPIES;
    dm.dmCopies = 1;
    auto job = EmfJob(10, dm);
    auto facts = EmfFacts(10, 1);
    auto acc = core::ComputePageAccounting(job, &facts);
    CHECK_EQ(acc.dataPages.value, 10u);
    CHECK_EQ(acc.logicalPages.value, 1u);
    CHECK(acc.logicalPages.reliability == Reliability::Estimated);
    CHECK_EQ(acc.copies.value, 10u);
    CHECK(acc.copies.reason == core::Reason::RepeatDetected);
    CHECK_EQ(acc.sides.value, 10u);  // no 100
    CHECK(acc.sides.reliability == Reliability::Estimated);
}

TEST(TotalPagesTimesCopiesIsNeverReliableWithoutContent) {
    // Sin leer los datos no se sabe si la aplicación repitió páginas: 4 × 3 es una estimación.
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_COPIES;
    dm.dmCopies = 3;
    auto job = EmfJob(4, dm);
    auto acc = core::ComputePageAccounting(job, nullptr);
    CHECK(acc.logicalPages.reliability == Reliability::Estimated);
    CHECK(acc.logicalPages.reason == core::Reason::PossibleAppCopies);
    // Sin leer los datos, el dmCopies de JOB_INFO_2 puede estar desactualizado (Word).
    CHECK(acc.copies.reliability == Reliability::Estimated);
    CHECK(acc.copies.reason == core::Reason::DataNotRead);
    CHECK_EQ(acc.sides.value, 12u);
    CHECK(acc.sides.reliability == Reliability::Estimated);
    CHECK_EQ(acc.printPages.value, 12u);
    CHECK(acc.printPages.reliability == Reliability::Estimated);
}

TEST(MultiPageCopiesNupAndDuplex) {
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_COPIES | DM_DUPLEX;
    dm.dmCopies = 2;
    dm.dmDuplex = DMDUP_VERTICAL;
    auto job = EmfJob(3, dm);
    auto facts = EmfFacts(3, 0);
    auto acc = core::ComputePageAccounting(job, &facts);
    CHECK(acc.logicalPages.reliability == Reliability::Reliable);
    // N-up desconocido: se supone 1 por cara y se marca como estimado.
    CHECK_EQ(acc.sides.value, 6u);
    CHECK(acc.sides.reliability == Reliability::Estimated);
    CHECK(acc.sides.reason == core::Reason::NupPrivate);
    CHECK_EQ(acc.sheets.value, 4u);  // 2 hojas por copia (3 caras en doble cara) × 2

    // XPS con PrintTicket: 5 páginas, 2 por cara, borde corto, 3 copias.
    core::JobInfo xps;
    xps.datatype = L"RAW";
    xps.printProcessor = L"MS_XPS_PROC";
    xps.totalPages = 5;
    core::ContentFacts t;
    t.analyzed = t.complete = t.repeatChecked = true;
    t.kind = core::DataKind::Xps;
    t.pageSource = core::ContentFacts::PageSource::XpsFixedPages;
    t.pages = 5;
    t.settingsSource = core::ContentFacts::SettingsSource::PrintTicket;
    t.copies.Set(3);
    t.pagesPerSheet.Set(2);
    t.duplex.Set(DMDUP_HORIZONTAL);
    auto x = core::ComputePageAccounting(xps, &t);
    CHECK(x.kind == core::DataKind::Xps);
    CHECK_EQ(x.sides.value, 9u);   // ceil(5/2) = 3 caras × 3
    CHECK(x.sides.reliability == Reliability::Reliable);
    CHECK_EQ(x.sheets.value, 6u);  // 2 hojas × 3
    CHECK_EQ(x.duplex.value, static_cast<std::uint32_t>(DMDUP_HORIZONTAL));
}

TEST(RawCopiesNeedConfirmationFromData) {
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_COPIES;
    dm.dmCopies = 10;
    core::JobInfo job;
    job.datatype = L"RAW";
    job.totalPages = 1;
    job.settings = core::ParseDevMode(&dm);
    // Sin datos: solo el DEVMODE del servidor => estimado.
    auto acc = core::ComputePageAccounting(job, nullptr);
    CHECK(acc.kind == core::DataKind::Pdl);
    CHECK(acc.copies.reliability == Reliability::Estimated);
    CHECK(acc.copies.reason == core::Reason::RawUnverified);
    CHECK_EQ(acc.sides.value, 10u);
    CHECK(acc.sides.reliability == Reliability::Estimated);

    // PJL confirma las 10 copias: fiable.
    core::ContentFacts pjl;
    pjl.analyzed = pjl.complete = true;
    pjl.kind = core::DataKind::Pdl;
    pjl.settingsSource = core::ContentFacts::SettingsSource::Pjl;
    pjl.copies.Set(10);
    acc = core::ComputePageAccounting(job, &pjl);
    CHECK(acc.copies.reliability == Reliability::Reliable);
    CHECK(acc.copies.basis == core::Basis::DevModeAndContent);
    CHECK(acc.sides.reliability == Reliability::Reliable);

    // PJL contradice al DEVMODE: la impresora obedece a los datos, que mandan.
    pjl.copies.Set(3);
    acc = core::ComputePageAccounting(job, &pjl);
    CHECK(acc.copiesConflict);
    CHECK_EQ(acc.copies.value, 3u);
    CHECK(acc.copies.reliability == Reliability::Reliable);
    CHECK(acc.copies.basis == core::Basis::Content);

    // EJL (no documentado): nunca vuelve fiable las copias.
    core::ContentFacts ejl = pjl;
    ejl.settingsSource = core::ContentFacts::SettingsSource::Ejl;
    ejl.settingsDocumented = false;
    ejl.copies = {};
    acc = core::ComputePageAccounting(job, &ejl);
    CHECK(acc.copies.reliability == Reliability::Estimated);
}

TEST(SpoolingAndMissingDataAreNotDeterminable) {
    core::JobInfo job;
    job.datatype = L"RAW";
    job.rawStatus = JOB_STATUS_SPOOLING;
    auto acc = core::ComputePageAccounting(job, nullptr);
    CHECK(!acc.dataPages.Known());
    CHECK(acc.dataPages.reason == core::Reason::StillSpooling);
    CHECK(!acc.sides.Known());
    CHECK(!acc.sheets.Known());
    CHECK(!acc.copies.Known());  // sin DEVMODE
    CHECK(acc.copies.reason == core::Reason::NoDevMode);
}

// --- PJL / EJL / formato --------------------------------------------------

TEST(PjlSettingsAreParsed) {
    std::string data = "\x1b%-12345X@PJL JOB NAME=\"x\"\r\n@PJL SET COPIES=3\r\n@PJL SET DUPLEX=ON\r\n"
                       "@PJL SET BINDING=SHORTEDGE\r\n@PJL SET PAPER=A4\r\n@PJL SET ORIENTATION=LANDSCAPE\r\n"
                       "@PJL ENTER LANGUAGE=PCLXL\r\n) HP-PCL XL;2;0\r\n@PJL SET COPIES=99\r\n";
    auto bytes = Bytes(data);
    core::ContentFacts f;
    preview::ParsePdlSettings(bytes.data(), bytes.size(), true, f);
    CHECK(f.settingsSource == core::ContentFacts::SettingsSource::Pjl);
    CHECK(f.settingsDocumented);
    CHECK_EQ(f.copies.value, 3u);  // lo que sigue a ENTER LANGUAGE es el documento
    CHECK_EQ(f.duplex.value, static_cast<short>(DMDUP_HORIZONTAL));
    CHECK_EQ(f.orientation.value, static_cast<short>(DMORIENT_LANDSCAPE));
    CHECK_EQ(f.paperWidth.value, 2100);

    core::ContentFacts q;
    auto qty = Bytes("@PJL SET QTY=2\n@PJL SET DUPLEX=OFF\n@PJL ENTER LANGUAGE=POSTSCRIPT\n%!PS\n");
    preview::ParsePdlSettings(qty.data(), qty.size(), true, q);
    CHECK_EQ(q.copies.value, 2u);
    CHECK_EQ(q.collate.value, static_cast<short>(DMCOLLATE_TRUE));
    CHECK_EQ(q.duplex.value, static_cast<short>(DMDUP_SIMPLEX));
}

TEST(EpsonEjlFromLaimiServer) {
    // Encabezado y final reales de un trabajo de "LAIMI 2 - INK" (EPSON AM-C6000,
    // ESC/Page-Color), sin los campos con datos personales.
    std::string data =
        "\x1b\x01@EJL \n@EJL SJ ID=\"4\"\n@EJL JI ID=\"4\" USER=\"x\" MACHINE=\"\\\\PC\" DOCUMENT=\"d\"\n"
        "@EJL SE LA=ESC/PAGE\n"
        "@EJL SET CG=U8 RS=FN PU=1 MF=OFF PS=LT WA=OFF DX=OFF BD=LE EC=ON SZ=OFF SN=OFF SL=NO EAN128=OFF EM=ON CO=1 "
        "QT=1 PY=AUTO TO=0 LO=0\n@EJL SET PQ=NM USBTM=MAX DP=0 DY=99 OF=OFF AS=OFF OS=OFF MX=OFF PCJ=ON SLQT=0 SLJB=OFF\n"
        "@EJL SET CV=OFF\n@EJL EN LA=ESC/PAGE-COLOR\n\x1d"
        "10pmE\x1drhE";
    data += std::string(256, '\0');
    data += "\x1b\x01@EJL \n@EJL JI PAGES=\"2\"\n\x1b\x01@EJL \n@EJL EJ \n\x1b\x01@EJL \n";
    auto bytes = Bytes(data);
    CHECK(preview::DetectFormat(bytes.data(), bytes.size()) == preview::DataFormat::EscPage);

    core::ContentFacts f;
    preview::ParsePdlSettings(bytes.data(), bytes.size(), true, f);
    CHECK(f.settingsSource == core::ContentFacts::SettingsSource::Ejl);
    CHECK(!f.settingsDocumented);  // sin DRIVER= de un controlador verificado: solo indicio
    CHECK_EQ(f.paperWidth.value, 2159);  // PS=LT => Carta
    CHECK_EQ(f.duplex.value, static_cast<short>(DMDUP_SIMPLEX));
    CHECK_EQ(f.copies.value, 1u);        // QT
    CHECK(f.pageSource == core::ContentFacts::PageSource::None);  // JI PAGES son hojas, no páginas
    CHECK_EQ(f.sheetsCounter.value, 2u);
    CHECK(f.rawSettings.find(L"CO=1") != std::wstring::npos);
    CHECK(f.rawSettings.find(L"USER") == std::wstring::npos);  // sin datos personales

    // Datos incompletos: el final (JI PAGES) no se busca.
    core::ContentFacts partial;
    preview::ParsePdlSettings(bytes.data(), bytes.size(), false, partial);
    CHECK(!partial.sheetsCounter.valid);
}

TEST(EpsonAmC6000OnePageTenCopies) {
    // Encabezado tal como lo genera el controlador EPSON AM-C6000 de las colas del
    // LAIMI para 1 página x 10 copias en horizontal (verificado imprimiendo a archivo).
    std::string data =
        "\x1b\x01@EJL \n@EJL JI USER=\"x\" MACHINE=\"\\\\PC\" DOCUMENT=\"d\" DRIVER=\"EPSON AM-C6000 Series\"\n"
        "@EJL SE LA=ESC/PAGE\n@EJL SET PS=LT DX=OFF BD=LE CO=1 QT=10\n@EJL EN LA=ESC/PAGE-COLOR\n"
        "\x1d" "30psE\x1d" "1poE\x1d" "0bdE";
    data += std::string(64, '\0');
    data += "\x1b\x01@EJL \n@EJL JI PAGES=\"10\"\n\x1b\x01@EJL \n@EJL EJ \n";
    auto bytes = Bytes(data);
    core::ContentFacts f;
    f.analyzed = f.complete = true;
    f.kind = core::DataKind::Pdl;
    preview::ParsePdlSettings(bytes.data(), bytes.size(), true, f);
    CHECK(f.settingsDocumented);
    CHECK(f.verifiedDriver.find(L"AM-C6000") != std::wstring::npos);
    CHECK_EQ(f.copies.value, 10u);
    CHECK_EQ(f.orientation.value, static_cast<short>(DMORIENT_LANDSCAPE));
    CHECK_EQ(f.sheetsCounter.value, 10u);

    // En el servidor el DEVMODE es el predeterminado de la cola (1 copia): mandan los datos.
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_COPIES | DM_DUPLEX;
    dm.dmCopies = 1;
    dm.dmDuplex = DMDUP_VERTICAL;
    core::JobInfo job;
    job.datatype = L"RAW";
    job.totalPages = 1;
    job.settings = core::ParseDevMode(&dm);
    auto acc = core::ComputePageAccounting(job, &f);
    CHECK_EQ(acc.copies.value, 10u);
    CHECK(acc.copies.reliability == Reliability::Reliable);
    CHECK(acc.copiesConflict);
    CHECK_EQ(acc.duplex.value, static_cast<std::uint32_t>(DMDUP_SIMPLEX));
    CHECK(acc.duplex.reliability == Reliability::Reliable);
    CHECK_EQ(acc.sides.value, 10u);
    CHECK(acc.sides.reliability == Reliability::Reliable);
    CHECK_EQ(acc.sheets.value, 10u);
    CHECK(acc.sheets.basis == core::Basis::DriverSheets);
}

// Datos ESC/Page como los del controlador AM-C6000: encabezado EJL, comandos
// "GS ...", bandas de imagen "GS n;...cdb{I" + n bytes y un FF por página.
std::string EscPageJob(int pages, bool duplex, bool blankBack) {
    std::string data =
        "\x1b\x01@EJL \n@EJL JI USER=\"x\" MACHINE=\"\\\\PC\" DOCUMENT=\"d\" DRIVER=\"EPSON AM-C6000 Series\"\n"
        "@EJL SE LA=ESC/PAGE\n@EJL SET PS=LT DX=" + std::string(duplex ? "ON" : "OFF") +
        " BD=LE CO=1 QT=1\n@EJL EN LA=ESC/PAGE-COLOR\n"
        "\x1d" "10pmE\x1d" "rhE\x1d" "0;.12muE\x1d" "30psE\x1d" "0poE\x1d" "7;0;2;3cam{E";
    data += std::string("\x0a\x00\x00\x00\x00\x00\x00", 7);
    for (int p = 0; p < pages; ++p) {
        data += "\x1d" "0ptE\x1d" "2pcsO\x1d" "0;10;1;1lbaI\x1d" "6;0;0;1cdb{I";
        data += std::string("\x0c\x1b\x1d\x00\x0c\xff", 6);  // los datos pueden traer FF, ESC y GS
        data += "\x0c";
    }
    // Dorso en blanco de la última hoja (así llega al servidor a doble cara con páginas impares).
    if (blankBack) data += "\x1d" "0ptE\x1d" "0feE\x1d" "0iidE\x1d" "1;1pcsO\x0c";
    data += "\x1b\x01@EJL \n@EJL JI PAGES=\"" + std::to_string(duplex ? (pages + 1) / 2 : pages) +
            "\"\n\x1b\x01@EJL \n@EJL EJ \n\x1b\x01@EJL \n";
    return data;
}

TEST(EpsonEscPagePagesCountedInData) {
    // Medido en el servidor con el AM-C6000 y la cola a doble cara: TotalPages
    // cuenta el dorso en blanco de la última hoja (1 -> 2, 2 -> 2, 3 -> 4). Con 1
    // página el controlador pone DX=OFF; con 3 agrega una página en blanco.
    struct Case {
        int pages;
        bool duplex;
        bool blankBack;
        std::uint32_t totalPages;
    };
    for (const Case& c : {Case{1, false, false, 2}, Case{2, true, false, 2}, Case{3, true, true, 4}}) {
        auto bytes = Bytes(EscPageJob(c.pages, c.duplex, c.blankBack));
        auto count = preview::CountEscPagePages(bytes.data(), bytes.size());
        CHECK(count.end == preview::EscPageCount::End::Complete);
        CHECK_EQ(count.pages, static_cast<std::uint32_t>(c.pages + (c.blankBack ? 1 : 0)));
        CHECK_EQ(count.lastPageBlank, c.blankBack);

        core::ContentFacts f;
        f.analyzed = f.complete = true;
        f.kind = core::DataKind::Pdl;
        preview::ParsePdlSettings(bytes.data(), bytes.size(), true, f);
        CHECK(f.pageSource == core::ContentFacts::PageSource::EscPageFormFeeds);
        CHECK_EQ(f.pages, static_cast<std::uint32_t>(c.pages));

        core::JobInfo job;
        job.datatype = L"RAW";
        job.totalPages = c.totalPages;
        auto acc = core::ComputePageAccounting(job, &f);
        CHECK_EQ(acc.dataPages.value, static_cast<std::uint32_t>(c.pages));
        CHECK(acc.dataPages.reliability == Reliability::Reliable);
        CHECK(acc.dataPages.basis == core::Basis::ContentPages);
        CHECK_EQ(acc.pagesMismatch, c.totalPages != static_cast<std::uint32_t>(c.pages));
        CHECK_EQ(acc.printPages.value, static_cast<std::uint32_t>(c.pages));
        CHECK(acc.printPages.reliability == Reliability::Reliable);
    }
    // A una cara, una página en blanco del documento sí cuenta.
    auto simplex = Bytes(EscPageJob(3, false, true));
    core::ContentFacts s;
    s.analyzed = s.complete = true;
    preview::ParsePdlSettings(simplex.data(), simplex.size(), true, s);
    CHECK_EQ(s.pages, 4u);
}

TEST(EpsonEscPageTruncatedOrInvalid) {
    // Cortado por tamaño: no se cuentan y TotalPages (par) queda estimado.
    std::string full = EscPageJob(3, true, true);
    auto cut = Bytes(full.substr(0, full.size() - 100));
    auto count = preview::CountEscPagePages(cut.data(), cut.size());
    CHECK(count.end == preview::EscPageCount::End::Truncated);
    CHECK_EQ(count.pages, 2u);
    core::ContentFacts f;
    f.analyzed = true;
    f.kind = core::DataKind::Pdl;
    preview::ParsePdlSettings(cut.data(), cut.size(), false, f);
    CHECK(f.pageSource == core::ContentFacts::PageSource::None);
    CHECK(f.spoolerCountsBlankBack);
    core::JobInfo job;
    job.datatype = L"RAW";
    job.totalPages = 4;
    auto acc = core::ComputePageAccounting(job, &f);
    CHECK_EQ(acc.dataPages.value, 4u);
    CHECK(acc.dataPages.reliability == Reliability::Estimated);
    CHECK(acc.dataPages.reason == core::Reason::BlankBackCounted);
    job.totalPages = 5;  // impar: no trae dorso de relleno
    CHECK(core::ComputePageAccounting(job, &f).dataPages.reliability == Reliability::Reliable);
    job.totalPages = 4;

    // Algo que no es ESC/Page conocido: se queda TotalPages.
    std::string bad = full;
    bad.insert(bad.find("\x1d" "0ptE"), "XYZ");
    auto badBytes = Bytes(bad);
    CHECK(preview::CountEscPagePages(badBytes.data(), badBytes.size()).end == preview::EscPageCount::End::Invalid);
    core::ContentFacts g;
    g.analyzed = g.complete = true;
    g.kind = core::DataKind::Pdl;
    preview::ParsePdlSettings(badBytes.data(), badBytes.size(), true, g);
    CHECK(g.pageSource == core::ContentFacts::PageSource::None);
    CHECK(!g.spoolerCountsBlankBack);
    CHECK_EQ(core::ComputePageAccounting(job, &g).dataPages.value, 4u);
}

TEST(EpsonPaperSourceShowsNames) {
    i18n::SetLanguage(i18n::Language::Spanish);
    // Datos del controlador AM-C6000: bandeja en "GS n;m iuE" y papel en PS.
    auto parse = [](const std::string& ps, const std::string& unit) {
        std::string data =
            "\x1b\x01@EJL \n@EJL JI USER=\"x\" MACHINE=\"\\\\PC\" DOCUMENT=\"d\" DRIVER=\"EPSON AM-C6000 Series\"\n"
            "@EJL SE LA=ESC/PAGE\n@EJL SET PS=" + ps + " DX=OFF BD=LE CO=1 QT=1\n@EJL EN LA=ESC/PAGE-COLOR\n"
            "\x1d" "0poE\x1d" + unit + "iuE\x1d" "0bdE";
        data += std::string(64, '\0');
        data += "\x1b\x01@EJL \n@EJL JI PAGES=\"1\"\n\x1b\x01@EJL \n@EJL EJ \n";
        auto bytes = Bytes(data);
        core::ContentFacts f;
        f.analyzed = f.complete = true;
        f.kind = core::DataKind::Pdl;
        preview::ParsePdlSettings(bytes.data(), bytes.size(), true, f);
        return f;
    };
    core::JobInfo job;
    job.datatype = L"RAW";
    job.totalPages = 1;
    struct Case { const char* ps; const char* unit; const wchar_t* paper; const wchar_t* source; };
    const Case cases[] = {
        {"LT", "0", L"Carta", L"Auto selección"},
        {"A3", "1;0", L"A3", L"Bandeja 1"},
        {"ARCHB", "2;0", L"12 × 18", L"Bandeja 2"},
        {"LT", "15;0", L"Carta", L"Alimentador de papel"},
    };
    for (const auto& c : cases) {
        auto f = parse(c.ps, c.unit);
        CHECK(f.sourceBin.valid);
        auto s = app::ResolveListSettings(job, &f);
        CHECK_EQ(s.paper, std::wstring(c.paper));
        CHECK_EQ(s.source, std::wstring(c.source));
    }

    // Sin datos: el DEVMODE del trabajo con el código propio del controlador.
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_DEFAULTSOURCE;
    dm.dmDefaultSource = 259;
    core::JobInfo emf = EmfJob(1, dm);
    emf.driver = L"EPSON AM-C6000 Series";
    CHECK_EQ(app::ResolveListSettings(emf, nullptr).source, std::wstring(L"Bandeja 2"));
    // Otro controlador: el nombre que declara (DeviceCapabilities) o uno genérico.
    core::PrinterInfo other;
    other.driver = L"Lexmark Universal v2";
    CHECK_EQ(app::BinName(259, L"Lexmark Universal v2", &other), std::wstring(L"Bandeja propia del controlador (259)"));
    other.bins.push_back({259, L"Bandeja MP"});
    CHECK_EQ(app::BinName(259, L"Lexmark Universal v2", &other), std::wstring(L"Bandeja MP"));
    // Papel propio del controlador (625) con las medidas de 12 × 18: el nombre de la lista.
    core::PrinterInfo epson;
    epson.papers.push_back({625, L"12 x 18 pulg. (305 x 457 mm)", 3048, 4572});
    DEVMODEW big = MakeDevMode();
    big.dmFields = DM_PAPERSIZE;
    big.dmPaperSize = 625;
    core::JobInfo bigJob = EmfJob(1, big);
    CHECK_EQ(app::ResolveListSettings(bigJob, nullptr, &epson).paper, std::wstring(L"12 × 18"));
    CHECK(core::FindPaper(0) == nullptr);
}

TEST(EpsonDuplexWithoutDataIsNotShown) {
    i18n::SetLanguage(i18n::Language::Spanish);
    // Caso real (Doc1.pdf en LAIMI 1 - INK): el DEVMODE del servidor es el de la
    // cola (doble cara) y los datos dicen DX=OFF. Sin leer los datos no se sabe.
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_DUPLEX;
    dm.dmDuplex = DMDUP_VERTICAL;
    core::JobInfo job = EmfJob(2, dm);
    job.datatype = L"RAW";
    job.printProcessor = L"EPSON AM-C6000 Series";
    job.driver = L"EPSON AM-C6000 Series";
    CHECK_EQ(app::ResolveListSettings(job, nullptr).duplex, std::wstring(L"—"));
    core::ContentFacts unread;  // lectura incompleta: formato desconocido, sin opciones
    unread.analyzed = true;
    unread.kind = core::DataKind::Pdl;
    CHECK_EQ(app::ResolveListSettings(job, &unread).duplex, std::wstring(L"—"));

    std::string data =
        "\x1b\x01@EJL \n@EJL JI DRIVER=\"EPSON AM-C6000 Series\"\n@EJL SET PS=LT DX=OFF BD=LE CO=1 QT=1\n"
        "@EJL EN LA=ESC/PAGE-COLOR\n\x0c\x0c\x1b\x01@EJL \n@EJL EJ \n";
    auto bytes = Bytes(data);
    core::ContentFacts read;
    read.analyzed = read.complete = true;
    read.kind = core::DataKind::Pdl;
    preview::ParsePdlSettings(bytes.data(), bytes.size(), true, read);
    CHECK_EQ(app::ResolveListSettings(job, &read).duplex, std::wstring(L"Una cara"));

    // Otro controlador RAW: se sigue mostrando el DEVMODE (estimado).
    job.driver = L"HP Universal Printing PCL 6";
    CHECK_EQ(app::ResolveListSettings(job, nullptr).duplex, std::wstring(L"Borde largo"));
}

TEST(IncompleteReadIsDetected) {
    // Dos copias de la app leyendo a la vez: cada una se queda con bloques sueltos.
    CHECK(preview::ReadLooksIncomplete(5112491, 1310720, false));
    CHECK(preview::ReadLooksIncomplete(2064121, 1277689, false));
    CHECK(!preview::ReadLooksIncomplete(2632, 2632, false));
    // Cortada a propósito por tamaño (16 MB): no es incompleta.
    CHECK(!preview::ReadLooksIncomplete(40ull << 20, 16ull << 20, true));
    // Sin tamaño informado: no se puede saber.
    CHECK(!preview::ReadLooksIncomplete(0, 100, false));
}

TEST(PostScriptDscIsParsed) {
    // Fragmentos reales de PSCRIPT5 (Word -> CutePDF) y de Acrobat ("%%Pages: (atend)").
    std::string ps =
        "%!PS-Adobe-3.0\r\n%%Creator: PScript5.dll Version 5.2.2\r\n%%Pages: (atend)\r\n%%Orientation: Landscape\r\n"
        "%%BeginSetup\r\nfeaturebegin{\r\n%%BeginNonPPDFeature: NumCopies 3\r\n3 dup /NumCopies\r\n%%EndNonPPDFeature\r\n"
        "}featurecleanup\r\n%%BeginFeature: *PageSize A4\r\n<</PageSize [595 842]>> setpagedevice\r\n%%EndFeature\r\n"
        "%%BeginFeature: *Duplex DuplexTumble\r\n%%EndFeature\r\n%%EndSetup\r\n%%Page: 1 1\r\n"
        "%%BeginNonPPDFeature: NumCopies 99\r\nshowpage\r\n%%Trailer\r\n%%Pages: 1\r\n%%EOF\r\n";
    auto bytes = Bytes(ps);
    CHECK(preview::DetectFormat(bytes.data(), bytes.size()) == preview::DataFormat::PostScript);
    core::ContentFacts f;
    preview::ParsePdlSettings(bytes.data(), bytes.size(), true, f);
    CHECK(f.settingsSource == core::ContentFacts::SettingsSource::PostScriptDsc);
    CHECK(f.settingsDocumented);
    CHECK_EQ(f.copies.value, 3u);  // lo que va dentro de las páginas no cuenta
    CHECK_EQ(f.orientation.value, static_cast<short>(DMORIENT_LANDSCAPE));
    CHECK_EQ(f.paperWidth.value, 2100);
    CHECK_EQ(f.duplex.value, static_cast<short>(DMDUP_HORIZONTAL));
    CHECK(f.pageSource == core::ContentFacts::PageSource::PostScriptDsc);
    CHECK_EQ(f.pages, 1u);

    // Incompleto con "(atend)": las páginas del final no se conocen.
    core::ContentFacts partial;
    auto head = Bytes(ps.substr(0, ps.find("%%Trailer")));
    preview::ParsePdlSettings(head.data(), head.size(), false, partial);
    CHECK(partial.pageSource == core::ContentFacts::PageSource::None);
    CHECK_EQ(partial.copies.value, 3u);
}

// --- PrintTicket XPS ------------------------------------------------------

TEST(PrintTicketIsParsed) {
    const std::string job =
        R"(<?xml version="1.0" encoding="UTF-8"?><psf:PrintTicket xmlns:psf="x" xmlns:psk="y" version="1">)"
        R"(<psf:ParameterInit name="psk:JobCopiesAllDocuments"><psf:Value xsi:type="xsd:integer">3</psf:Value></psf:ParameterInit>)"
        R"(<psf:Feature name="psk:PageMediaSize"><psf:Option name="psk:ISOA4">)"
        R"(<psf:ScoredProperty name="psk:MediaSizeWidth"><psf:Value xsi:type="xsd:integer">210000</psf:Value></psf:ScoredProperty>)"
        R"(<psf:ScoredProperty name="psk:MediaSizeHeight"><psf:Value xsi:type="xsd:integer">297000</psf:Value></psf:ScoredProperty>)"
        R"(</psf:Option></psf:Feature>)"
        R"(<psf:Feature name="psk:PageOrientation"><psf:Option name="psk:Landscape" /></psf:Feature>)"
        R"(<psf:Feature name="psk:PageOutputColor"><psf:Option name="psk:Grayscale"></psf:Option></psf:Feature>)"
        R"(<psf:Feature name="psk:JobDuplexAllDocumentsContiguously"><psf:Option name="psk:TwoSidedLongEdge" /></psf:Feature>)"
        R"(<psf:Feature name="psk:JobNUpAllDocumentsContiguously"><psf:Option>)"
        R"(<psf:ScoredProperty name="psk:PagesPerSheet"><psf:Value xsi:type="xsd:integer">4</psf:Value></psf:ScoredProperty>)"
        R"(</psf:Option></psf:Feature></psf:PrintTicket>)";
    core::ContentFacts f;
    CHECK(preview::ApplyPrintTicket(job, f));
    CHECK(f.settingsSource == core::ContentFacts::SettingsSource::PrintTicket);
    CHECK_EQ(f.copies.value, 3u);
    CHECK_EQ(f.paperWidth.value, 2100);
    CHECK_EQ(f.paperLength.value, 2970);
    CHECK_EQ(f.orientation.value, static_cast<short>(DMORIENT_LANDSCAPE));
    CHECK_EQ(f.color.value, static_cast<short>(DMCOLOR_MONOCHROME));
    CHECK_EQ(f.duplex.value, static_cast<short>(DMDUP_VERTICAL));
    CHECK_EQ(f.pagesPerSheet.value, 4u);

    // Un ticket de página posterior (más específico) prevalece en lo que declara.
    CHECK(preview::ApplyPrintTicket(
        R"(<psf:PrintTicket><psf:Feature name="psk:PageOrientation"><psf:Option name="psk:Portrait"/></psf:Feature></psf:PrintTicket>)",
        f));
    CHECK_EQ(f.orientation.value, static_cast<short>(DMORIENT_PORTRAIT));
    CHECK_EQ(f.copies.value, 3u);
    CHECK(!preview::ApplyPrintTicket("<psf:PrintTicket></psf:PrintTicket>", f));
}

// --- Ficha "Configuración" -------------------------------------------------

TEST(JobSheetShowsReliabilityPerValue) {
    i18n::SetLanguage(i18n::Language::Spanish);
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_COPIES | DM_PAPERSIZE | DM_DUPLEX;
    dm.dmCopies = 10;
    dm.dmPaperSize = DMPAPER_LETTER;
    dm.dmDuplex = DMDUP_SIMPLEX;
    auto job = EmfJob(1, dm);
    preview::PreviewDocument doc;
    doc.facts = EmfFacts(1, 0);
    auto rows = app::BuildJobSheet(job, nullptr, &doc);
    bool sides = false, paper = false;
    for (const auto& row : rows) {
        if (row.label == L"Caras físicas previstas") {
            sides = true;
            CHECK_EQ(row.value, std::wstring(L"10"));
            CHECK_EQ(row.badge, std::wstring(L"Fiable"));
        }
        if (row.label == L"Tamaño de papel") {
            paper = true;
            CHECK(row.value.find(L"Carta") == 0);
        }
    }
    CHECK(sides);
    CHECK(paper);
    CHECK(app::SettingsSummary(job, &doc).find(L"10 págs. a imprimir") != std::wstring::npos);

    // Sin DEVMODE ni datos: las caras no se pueden determinar.
    core::JobInfo bare;
    bare.datatype = L"RAW";
    bare.totalPages = 2;
    bool found = false;
    for (const auto& row : app::BuildJobSheet(bare, nullptr, nullptr)) {
        if (row.label != L"Caras físicas previstas") continue;
        found = true;
        CHECK_EQ(row.value, std::wstring(L"No determinable"));
    }
    CHECK(found);
}

// --- Lista: páginas a imprimir y orden por columnas ---------------------------

TEST(TotalPrintPagesIsPagesTimesCopies) {
    i18n::SetLanguage(i18n::Language::Spanish);
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_COPIES;
    dm.dmCopies = 3;
    auto job = EmfJob(3, dm);
    auto facts = EmfFacts(3, 0);
    // 3 páginas x 3 copias = 9 (fiable con los datos analizados).
    auto total = app::TotalPrintPages(job, &facts);
    CHECK_EQ(total.value, 9u);
    // Sin los datos del trabajo: la misma cuenta, pero marcada como estimada.
    auto estimate = app::TotalPrintPages(job, nullptr);
    CHECK_EQ(estimate.value, 9u);
    CHECK(estimate.reliability == core::Reliability::Estimated);
    CHECK_EQ(app::TotalPagesText(estimate), std::wstring(L"9"));  // sin marcas en la lista
    // 1 página x 10 copias del ejemplo del LAIMI.
    DEVMODEW ten = MakeDevMode();
    ten.dmFields = DM_COPIES;
    ten.dmCopies = 10;
    auto one = EmfJob(1, ten);
    auto oneFacts = EmfFacts(1, 0);
    CHECK_EQ(app::TotalPagesText(app::TotalPrintPages(one, &oneFacts)), std::wstring(L"10"));
    // Sin páginas conocidas.
    core::JobInfo unknown;
    unknown.datatype = L"RAW";
    CHECK_EQ(app::TotalPagesText(app::TotalPrintPages(unknown, nullptr)), std::wstring(L"—"));

    // La fila muestra ese total en la columna "Páginas".
    auto columns = app::JobColumns(false);
    auto row = app::MakeJobRow(job, false, &facts);
    CHECK_EQ(row.cells.size(), columns.size());
    for (size_t i = 0; i < columns.size(); ++i)
        if (columns[i].label == L"Páginas") CHECK_EQ(row.cells[i], std::wstring(L"9"));
    CHECK_EQ(app::MakeJobRow(job, true, &facts).cells.size(), app::JobColumns(true).size());
    CHECK(app::HistoryColumns().size() == app::MakeHistoryRow(config::HistoryEntry{}, 0).cells.size());
}

TEST(HistoryKeepsEveryQueueColumn) {
    i18n::SetLanguage(i18n::Language::Spanish);
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_COPIES | DM_DUPLEX | DM_ORIENTATION;
    dm.dmCopies = 3;
    dm.dmDuplex = DMDUP_VERTICAL;
    dm.dmOrientation = DMORIENT_LANDSCAPE;
    auto job = EmfJob(3, dm);
    job.position = 4;
    job.printer = L"\\\\S\\LAIMI 1";
    job.document = L"Tarea.pdf";
    job.user = L"ana";
    job.machine = L"\\\\LAB-PC07";
    job.sizeBytes = 2048;
    job.submitted.wYear = 2026;
    job.submitted.wMonth = 10;
    job.submitted.wDay = 6;
    job.submitted.wHour = 15;
    auto facts = EmfFacts(3, 0);
    auto entry = app::MakeHistoryEntry(job, config::JobOutcome::Printed, 1'800'000'000, &facts);

    // Guardar y volver a leer no pierde nada.
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    const std::wstring path = std::wstring(dir) + L"cola_tests_historial.json";
    {
        config::History history(path);
        history.Add(entry);
        CHECK(history.Save());
    }
    config::History loaded(path);
    CHECK(loaded.Load());
    CHECK_EQ(loaded.Entries().size(), size_t(1));

    // Con BOM UTF-8 (Bloc de notas, PowerShell 5.1) también se lee.
    auto writeRaw = [&](const std::string& text) {
        HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written = 0;
        if (f != INVALID_HANDLE_VALUE) {
            WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
            CloseHandle(f);
        }
    };
    writeRaw("\xEF\xBB\xBF{\"entries\":[{\"time\":5,\"document\":\"a\"}]}");
    config::History withBom(path);
    CHECK(withBom.Load());
    CHECK_EQ(withBom.Entries().size(), size_t(1));

    // Un archivo dañado no se pierde: se copia aparte antes de guardar encima.
    writeRaw("{\"entries\":[{\"time\":5,");
    config::History damaged(path);
    CHECK(!damaged.Load());
    CHECK(GetFileAttributesW(damaged.BackupPath().c_str()) != INVALID_FILE_ATTRIBUTES);
    damaged.Add(entry);
    CHECK(damaged.Save());
    DeleteFileW(damaged.BackupPath().c_str());
    DeleteFileW(path.c_str());
    const auto& back = loaded.Entries().front();
    CHECK_EQ(back.position, 4u);
    CHECK_EQ(back.printPages, entry.printPages);
    CHECK_EQ(back.paper, entry.paper);
    CHECK_EQ(back.orientation, entry.orientation);
    CHECK_EQ(back.duplex, entry.duplex);
    CHECK_EQ(back.source, entry.source);
    CHECK_EQ(back.submittedUnix, entry.submittedUnix);

    // Cada columna de la cola tiene la suya en el historial, con el mismo valor
    // (salvo Estado, que en el historial es el resultado).
    auto jobColumns = app::JobColumns(true);
    auto jobRow = app::MakeJobRow(job, true, &facts);
    auto historyColumns = app::HistoryColumns();
    auto historyRow = app::MakeHistoryRow(back, 0);
    CHECK_EQ(historyRow.cells.size(), historyColumns.size());
    for (size_t i = 0; i < jobColumns.size(); ++i) {
        if (jobColumns[i].id == L"status") continue;
        bool found = false;
        for (size_t h = 0; h < historyColumns.size(); ++h) {
            if (historyColumns[h].id != L"history." + jobColumns[i].id) continue;
            found = true;
            CHECK_EQ(historyRow.cells[h], jobRow.cells[i]);
        }
        CHECK(found);
    }
    CHECK_EQ(jobRow.cells[6], std::wstring(L"9"));  // páginas × copias

    // Un registro de una versión anterior (sin esos datos) se muestra con "—".
    config::HistoryEntry old;
    old.pages = 5;
    auto oldRow = app::MakeHistoryRow(old, 0);
    CHECK_EQ(oldRow.cells.size(), historyColumns.size());
    CHECK_EQ(oldRow.cells[6], std::wstring(L"5"));
    CHECK_EQ(oldRow.cells[7], std::wstring(L"—"));
}

TEST(SubmittedShowsDateAndTime) {
    SYSTEMTIME utc{};
    CHECK(util::FormatDateTime(utc).empty());  // sin fecha
    utc.wYear = 2026;
    utc.wMonth = 10;
    utc.wDay = 2;
    utc.wHour = 18;
    utc.wMinute = 5;
    std::wstring text = util::FormatDateTime(utc);  // hora local: "dd/mm/aaaa hh:mm"
    CHECK_EQ(text.size(), static_cast<size_t>(16));
    CHECK(text[2] == L'/' && text[5] == L'/' && text[10] == L' ' && text[13] == L':');
    CHECK(text.find(L"/2026 ") != std::wstring::npos);
}

TEST(ColumnClickSortsLikeWindowsQueue) {
    using app::SortBy;
    auto next = app::NextSort(SortBy::Queue, false, SortBy::User);
    CHECK(next.first == SortBy::User && !next.second);   // otra columna: ascendente
    next = app::NextSort(next.first, next.second, SortBy::User);
    CHECK(next.first == SortBy::User && next.second);    // misma columna: invierte
    next = app::NextSort(next.first, next.second, SortBy::User);
    CHECK(next.first == SortBy::User && !next.second);
    CHECK(app::SortByFromId(L"pages") == SortBy::Pages);
    CHECK(app::SortByFromId(L"printer") == SortBy::Printer);

    // Orden por páginas a imprimir (páginas x copias), no por TotalPages.
    core::QueueSnapshot snapshot;
    DEVMODEW one = MakeDevMode();
    one.dmFields = DM_COPIES;
    one.dmCopies = 1;
    DEVMODEW five = one;
    five.dmCopies = 5;
    auto a = EmfJob(4, one);   // 4 x 1 = 4
    a.id = 1;
    a.position = 1;
    auto b = EmfJob(2, five);  // 2 x 5 = 10
    b.id = 2;
    b.position = 2;
    snapshot.jobs = {a, b};
    auto byPages = app::VisibleJobs(snapshot, app::kAllPrinters, L"", SortBy::Pages, true);
    CHECK_EQ(byPages[0]->id, 2u);
    CHECK_EQ(byPages[1]->id, 1u);
}

// --- Cola: caché del cliente + servidor ---------------------------------------

TEST(ReconcileJobListsTrustsServer) {
    auto job = [](std::uint32_t id, std::uint32_t position, std::uint32_t status = 0) {
        core::JobInfo j;
        j.id = id;
        j.position = position;
        j.rawStatus = status;
        return j;
    };
    // La caché del cliente no tiene el 85 (llegó desde otra PC), conserva el 70
    // (ya terminó en el servidor) y tiene el 29957, que esta PC todavía procesa.
    std::vector<core::JobInfo> cached = {job(70, 1), job(84, 2),
                                         job(29957, 4, core::kJobStatusRenderingLocally | JOB_STATUS_PAUSED)};
    std::vector<core::JobInfo> server = {job(84, 1), job(85, 2)};
    auto merged = core::ReconcileJobLists(server, cached);
    CHECK_EQ(merged.size(), static_cast<size_t>(3));
    CHECK_EQ(merged[0].id, 84u);
    CHECK_EQ(merged[1].id, 85u);
    CHECK_EQ(merged[2].id, 29957u);  // el 70 (resto de la caché) no vuelve
    CHECK_EQ(core::ReconcileJobLists(server, server).size(), static_cast<size_t>(2));

    CHECK_EQ(core::spooler::ReplacePrinterServer(L"\\\\vtec-laimiprint\\LAIMI 1 - INK", L"vtec-laimiprint.dominio"),
             std::wstring(L"\\\\vtec-laimiprint.dominio\\LAIMI 1 - INK"));
    CHECK(core::spooler::ReplacePrinterServer(L"CutePDF Writer", L"x").empty());
    CHECK(core::spooler::CacheBypassName(L"CutePDF Writer").empty());  // cola local: no hay caché de cliente
}

// --- Archivos temporales ----------------------------------------------------

TEST(PrivateTempDirIsProtectedAndRemoved) {
    std::wstring dir = preview::PrivateTempDir();
    CHECK(!dir.empty());
    std::wstring path = preview::NewTempPath(L".tmp");
    CHECK(path.rfind(dir, 0) == 0);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);

    // La DACL de la carpeta está protegida (no hereda de %TEMP%).
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    DWORD needed = 0;
    GetFileSecurityW(dir.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &needed);
    std::vector<BYTE> sd(needed);
    CHECK(needed > 0 && GetFileSecurityW(dir.c_str(), DACL_SECURITY_INFORMATION, sd.data(), needed, &needed));
    if (needed > 0) {
        GetSecurityDescriptorControl(sd.data(), &control, &revision);
        CHECK((control & SE_DACL_PROTECTED) != 0);
    }

    preview::RemovePrivateTempDir();
    CHECK(GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES);
}
