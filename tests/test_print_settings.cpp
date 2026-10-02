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
#include "core/PageAccounting.h"
#include "core/PrintSettings.h"
#include "core/Spooler.h"
#include "i18n/Strings.h"
#include "preview/PdlSettings.h"
#include "preview/SpoolFile.h"
#include "preview/TempFiles.h"
#include "preview/XpsPackage.h"

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
    CHECK(acc.copies.reliability == Reliability::Reliable);
    CHECK_EQ(acc.sides.value, 12u);
    CHECK(acc.sides.reliability == Reliability::Estimated);
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
    CHECK(app::SettingsSummary(job, &doc).find(L"10 caras") != std::wstring::npos);

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
