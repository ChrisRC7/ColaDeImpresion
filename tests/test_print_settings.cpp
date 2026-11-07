// Pruebas de la configuración de impresión de un trabajo: DEVMODE, separación
// páginas / copias / caras / hojas con su fiabilidad, opciones PJL/EJL y
// PrintTicket, ficha "Configuración" y archivos temporales privados.
//
// Módulos probados:
//   - core/PrintSettings: ParseDevMode (DEVMODE validado: solo campos
//     declarados en dmFields y que quepan en dmSize), EffectivePaper,
//     SameVisibleSettings.
//   - core/PageAccounting: ComputePageAccounting y RepeatPeriod. Separa
//     páginas lógicas, copias, caras y hojas, cada una Fiable / Estimada / No
//     determinable con su motivo (D24, D31). La regla central: nunca
//     TotalPages × dmCopies a ciegas.
//   - preview/PdlSettings: opciones declaradas en los datos RAW (PJL, DSC de
//     PostScript, EJL de Epson, D25/D29) y conteo de páginas ESC/Page (D46,
//     D53).
//   - preview/XpsPackage: PrintTicket de un XPS (D25).
//   - preview/SpoolFile: ReadLooksIncomplete (lecturas incompletas, D48).
//   - core/Spooler: ReconcileJobLists, ReplacePrinterServer y CacheBypassName
//     (lista de trabajos sin la caché del cliente, D28).
//   - config/History: guardar/leer historial.json, BOM y archivo dañado (D52);
//     util/Format: FormatDateTime (columna Enviado).
//   - app/JobSheet y app/RowBuilder: ficha "Configuración" (F7) y textos de
//     las columnas (papel, orientación, doble cara, origen, presentación).
//   - preview/TempFiles: carpeta temporal privada (D27).
// Los valores de papel van en décimas de milímetro, como en el DEVMODE
// (dmPaperWidth/dmPaperLength): A4 = 2100 × 2970, carta = 2159 × 2794.
// Varios datos de entrada son copias (sin datos personales) de trabajos reales
// de las Epson AM-C6000 del LAIMI.

#include <windows.h>
#include <winspool.h>

#include <algorithm>
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

// DEVMODE vacío pero válido: tamaño completo, versión actual y dmFields = 0
// (ningún campo declarado). Cada prueba declara solo lo que quiere probar.
DEVMODEW MakeDevMode() {
    DEVMODEW dm{};
    dm.dmSize = sizeof(DEVMODEW);
    dm.dmSpecVersion = DM_SPECVERSION;
    return dm;
}

// Trabajo EMF terminado de recibir, con un DEVMODE dado.
// "NT EMF 1.008" + winprint es lo que informa el spooler para un trabajo EMF
// renderizado en el servidor; totalPages es el TotalPages de JOB_INFO_2.
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

// Resultado del análisis de los datos de un trabajo EMF completo: `pages`
// páginas contadas en los registros EMF y `repeatPeriod` el período de
// repetición de sus huellas (0 = no se repiten; 1 = todas iguales, es decir la
// aplicación repitió una página; 2 = A B A B...). Ver RepeatPeriod.
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

// Bytes de un texto (puede contener nulos, por eso recibe std::string y no
// const char*): para armar datos RAW de prueba.
std::vector<std::uint8_t> Bytes(const std::string& text) { return {text.begin(), text.end()}; }

}  // namespace

// --- DEVMODE --------------------------------------------------------------

// Un campo del DEVMODE solo vale si su bit está en dmFields: los controladores
// dejan basura en los campos no declarados. Aquí dmDuplex tiene un valor
// plausible pero no está declarado, así que no debe tomarse.
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

// dmSize dice cuántos bytes públicos trae el DEVMODE (los antiguos o de
// algunos controladores son más cortos). Un campo que queda fuera de dmSize no
// se lee aunque su bit esté en dmFields: leerlo sería leer memoria ajena (los
// datos privados del controlador que siguen al DEVMODE público).
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
    // Sin DEVMODE (JOB_INFO_2.pDevMode nulo): nada presente.
    CHECK(!core::ParseDevMode(nullptr).present);
}

// Valores declarados pero imposibles se descartan: 0 copias, dmDuplex fuera
// de DMDUP_SIMPLEX(1)..DMDUP_HORIZONTAL(3) y dmOrientation fuera de
// DMORIENT_PORTRAIT(1)..DMORIENT_LANDSCAPE(2).
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

// Medidas del papel: un tamaño estándar (DMPAPER_A4) se traduce a sus medidas
// (210 × 297 mm), y DMPAPER_USER usa dmPaperWidth/dmPaperLength (décimas de mm).
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

    // Texto de la columna Papel en español: coma decimal y "×" (U+00D7).
    i18n::SetLanguage(i18n::Language::Spanish);
    CHECK_EQ(app::PaperText(core::ParseDevMode(&dm)), std::wstring(L"Personalizado — 100,0 × 150,0 mm"));
}

// La ficha compara el DEVMODE del trabajo con el de la cola para avisar si el
// trabajo trae una configuración propia (D23). Iguales -> true; cambiar las
// copias -> false.
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

// RepeatPeriod recibe las huellas (hash) de cada página en orden y devuelve el
// período más corto con el que se repiten todas, o 0. Así se detectan las
// copias hechas por la aplicación (Word repite las páginas en el spool en vez
// de usar dmCopies, D24).
TEST(RepeatPeriodDetection) {
    // Una página repetida 4 veces: período 1. Dos páginas × 3 copias
    // intercaladas: período 2.
    CHECK_EQ(core::RepeatPeriod({1, 1, 1, 1}), 1u);
    CHECK_EQ(core::RepeatPeriod({1, 2, 1, 2, 1, 2}), 2u);
    // Sin repetición, repetición incompleta (1 2 1: la última copia está
    // cortada) y una sola página o ninguna: no hay período.
    CHECK_EQ(core::RepeatPeriod({1, 2, 3}), 0u);
    CHECK_EQ(core::RepeatPeriod({1, 2, 1}), 0u);
    CHECK_EQ(core::RepeatPeriod({1}), 0u);
    CHECK_EQ(core::RepeatPeriod({}), 0u);
}

TEST(OnePageTenDriverCopiesIsReliable) {
    // Caso del enunciado: 1 página × 10 copias que aplica el procesador de impresión.
    // (El pedido original del LAIMI.) Con los datos analizados (1 página, sin
    // repeticiones) y el DEVMODE del spool, todo es fiable: 1 página lógica,
    // 10 copias, 10 caras y, a una cara, 10 hojas.
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
    // Los datos tienen 10 páginas idénticas (período 1): se interpreta como 1
    // página × 10 copias, estimado porque también podría ser un documento de
    // 10 páginas iguales. Lo importante: 10 caras, no 10 × 10 = 100.
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
    // (nullptr = sin ContentFacts.) Todo sale Estimado con su motivo: páginas
    // por PossibleAppCopies y copias por DataNotRead.
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

// Varias páginas con copias, doble cara y páginas por cara (N-up).
// Primer caso, EMF: 3 páginas × 2 copias a doble cara (borde largo).
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
    // (En EMF las páginas por cara son una opción privada del controlador,
    // ver "Limitaciones conocidas" en CONTEXTO.md.) 3 × 2 = 6 caras.
    CHECK_EQ(acc.sides.value, 6u);
    CHECK(acc.sides.reliability == Reliability::Estimated);
    CHECK(acc.sides.reason == core::Reason::NupPrivate);
    CHECK_EQ(acc.sheets.value, 4u);  // 2 hojas por copia (3 caras en doble cara) × 2

    // XPS con PrintTicket: 5 páginas, 2 por cara, borde corto, 3 copias.
    // Segundo caso: el PrintTicket sí declara el N-up, así que las caras son
    // fiables. "MS_XPS_PROC" es el procesador de impresión de los drivers v4.
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

// Trabajos RAW (Client Side Rendering): el DEVMODE que ve el servidor suele ser
// el predeterminado de la cola (D29), así que las copias del DEVMODE solo son
// fiables si los datos (PJL documentado) las confirman. Si los datos dicen
// otra cosa, mandan los datos, porque es lo que la impresora obedece (D25).
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
    // (EJL sin el controlador verificado, settingsDocumented = false, y sin
    // copias: el EJL es solo indicio. Con el AM-C6000 verificado sí cuenta, ver
    // EpsonEjlFromLaimiServer.)
    core::ContentFacts ejl = pjl;
    ejl.settingsSource = core::ContentFacts::SettingsSource::Ejl;
    ejl.settingsDocumented = false;
    ejl.copies = {};
    acc = core::ComputePageAccounting(job, &ejl);
    CHECK(acc.copies.reliability == Reliability::Estimated);
}

// Un trabajo RAW que todavía se recibe (SPOOLING) y sin DEVMODE: nada se puede
// determinar, y cada valor lo dice con su motivo (StillSpooling, NoDevMode) en
// vez de mostrar un número inventado.
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

// Opciones PJL (documentadas por HP, D25) antes de ENTER LANGUAGE. Después de
// ENTER LANGUAGE empieza el documento: un "@PJL SET COPIES=99" ahí dentro es
// parte de los datos del lenguaje y no debe tomarse.
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
    // DUPLEX=ON + BINDING=SHORTEDGE = doble cara por el borde corto, que en el
    // DEVMODE es DMDUP_HORIZONTAL. PAPER=A4 -> 2100 décimas de mm de ancho.
    CHECK_EQ(f.duplex.value, static_cast<short>(DMDUP_HORIZONTAL));
    CHECK_EQ(f.orientation.value, static_cast<short>(DMORIENT_LANDSCAPE));
    CHECK_EQ(f.paperWidth.value, 2100);

    // QTY son copias intercaladas (a diferencia de COPIES): da copias = 2 y
    // collate = DMCOLLATE_TRUE. Sin UEL al principio también se reconoce.
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
    // Estructura: "\x1b\x01@EJL" abre cada bloque EJL; SJ/JI = inicio y datos
    // del trabajo; SE LA = lenguaje; SET = opciones (PS=LT carta, DX=OFF una
    // cara, BD=LE borde largo, CO=1, QT=1 copias...); EN LA = entra al
    // lenguaje. "\x1d" "10pmE" y "\x1d" "rhE" son dos comandos ESC/Page (GS +
    // parámetros + nombre). Luego 256 nulos en lugar de las bandas de imagen y,
    // al final, JI PAGES="2" (hojas físicas, D29) y EJ (fin del trabajo).
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
    // ESC/Page tiene formato propio (antes se confundía con PCL, D26).
    CHECK(preview::DetectFormat(bytes.data(), bytes.size()) == preview::DataFormat::EscPage);

    // Este encabezado no trae DRIVER=, así que el EJL es solo indicio (D25):
    // se leen papel, doble cara y copias, pero sin marcarlos como documentados.
    core::ContentFacts f;
    preview::ParsePdlSettings(bytes.data(), bytes.size(), true, f);
    CHECK(f.settingsSource == core::ContentFacts::SettingsSource::Ejl);
    CHECK(!f.settingsDocumented);  // sin DRIVER= de un controlador verificado: solo indicio
    CHECK_EQ(f.paperWidth.value, 2159);  // PS=LT => Carta
    CHECK_EQ(f.duplex.value, static_cast<short>(DMDUP_SIMPLEX));
    CHECK_EQ(f.copies.value, 1u);        // QT
    CHECK(f.pageSource == core::ContentFacts::PageSource::None);  // JI PAGES son hojas, no páginas
    CHECK_EQ(f.sheetsCounter.value, 2u);
    // Las opciones crudas se muestran en la ficha (CO no se interpreta), pero
    // sin los campos de JI con usuario/equipo/documento.
    CHECK(f.rawSettings.find(L"CO=1") != std::wstring::npos);
    CHECK(f.rawSettings.find(L"USER") == std::wstring::npos);  // sin datos personales

    // Datos incompletos: el final (JI PAGES) no se busca.
    // (complete = false: con los datos cortados, un "JI PAGES" encontrado no
    // sería necesariamente el del final del trabajo.)
    core::ContentFacts partial;
    preview::ParsePdlSettings(bytes.data(), bytes.size(), false, partial);
    CHECK(!partial.sheetsCounter.valid);
}

TEST(EpsonAmC6000OnePageTenCopies) {
    // Encabezado tal como lo genera el controlador EPSON AM-C6000 de las colas del
    // LAIMI para 1 página x 10 copias en horizontal (verificado imprimiendo a archivo).
    // Con DRIVER="EPSON AM-C6000 Series" el EJL cuenta como verificado (D29):
    // QT=10 son las copias, "GS 1poE" la orientación horizontal (GS n poE,
    // 0 = vertical, 1 = horizontal) y JI PAGES="10" las hojas físicas.
    // "30psE" y "0bdE" son otros comandos de configuración del controlador.
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
    // DEVMODE de la cola: 1 copia y doble cara; datos: 10 copias y DX=OFF.
    // Resultado fiable con los datos (copias 10, una cara, 10 caras), con el
    // conflicto marcado para la ficha, y las hojas tomadas del contador del
    // controlador (Basis::DriverSheets).
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
// pages: páginas del documento; duplex: DX=ON/OFF en el EJL; blankBack: agrega
// al final la página sin imagen que el controlador pone como dorso en blanco
// de la última hoja (D46). El JI PAGES del final son las hojas: a doble cara
// ceil(pages / 2).
// Detalles que el contador de D46 debe manejar: los comandos con "{" declaran
// en su primer parámetro cuántos bytes de datos siguen ("7;0;2;3cam{E" va
// seguido de 7 bytes, "6;0;0;1cdb{I" de 6), y esos datos pueden contener FF
// (0x0C), ESC y GS que NO son fin de página ni comandos.
std::string EscPageJob(int pages, bool duplex, bool blankBack) {
    std::string data =
        "\x1b\x01@EJL \n@EJL JI USER=\"x\" MACHINE=\"\\\\PC\" DOCUMENT=\"d\" DRIVER=\"EPSON AM-C6000 Series\"\n"
        "@EJL SE LA=ESC/PAGE\n@EJL SET PS=LT DX=" + std::string(duplex ? "ON" : "OFF") +
        " BD=LE CO=1 QT=1\n@EJL EN LA=ESC/PAGE-COLOR\n"
        "\x1d" "10pmE\x1d" "rhE\x1d" "0;.12muE\x1d" "30psE\x1d" "0poE\x1d" "7;0;2;3cam{E";
    // Los 7 bytes de datos de "cam{E" (con nulos: por eso se indica el largo).
    data += std::string("\x0a\x00\x00\x00\x00\x00\x00", 7);
    // Cada página: comandos de inicio, una banda de imagen de 6 bytes y el FF
    // que la termina.
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
    // Bug de D46: un documento de 1 página aparecía con 2. Para cada caso:
    //   - el contador ve todas las páginas de los datos (incluido el dorso) y
    //     marca si la última está en blanco;
    //   - ParsePdlSettings descuenta el dorso de relleno (pages = las reales);
    //   - ComputePageAccounting usa esas páginas como fiables y marca la
    //     diferencia con TotalPages (pagesMismatch) para la ficha.
    // Campos de Case: páginas reales, DX del EJL, si trae dorso en blanco y el
    // TotalPages que informó el servidor.
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
    // (Con DX=OFF no hay dorso de relleno: la página en blanco es del
    // documento y se imprime, así que 3 + 1 = 4.)
    auto simplex = Bytes(EscPageJob(3, false, true));
    core::ContentFacts s;
    s.analyzed = s.complete = true;
    preview::ParsePdlSettings(simplex.data(), simplex.size(), true, s);
    CHECK_EQ(s.pages, 4u);
}

TEST(EpsonEscPageTruncatedOrInvalid) {
    // Cortado por tamaño: no se cuentan y TotalPages (par) queda estimado.
    // Quitar los últimos 100 bytes elimina el EJL final y el dorso en blanco y
    // corta la 3.ª página: el contador solo vio 2 FF completos y dice
    // Truncated. Sin un conteo completo, ParsePdlSettings no da páginas, pero
    // como hay DX=ON avisa que el TotalPages del spooler puede incluir el
    // dorso (spoolerCountsBlankBack).
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
    // TotalPages par a doble cara: puede traer el dorso de relleno -> 4,
    // estimado con motivo BlankBackCounted.
    auto acc = core::ComputePageAccounting(job, &f);
    CHECK_EQ(acc.dataPages.value, 4u);
    CHECK(acc.dataPages.reliability == Reliability::Estimated);
    CHECK(acc.dataPages.reason == core::Reason::BlankBackCounted);
    job.totalPages = 5;  // impar: no trae dorso de relleno
    CHECK(core::ComputePageAccounting(job, &f).dataPages.reliability == Reliability::Reliable);
    job.totalPages = 4;

    // Algo que no es ESC/Page conocido: se queda TotalPages.
    // "XYZ" sueltos antes del primer comando de página no son un comando
    // válido: el contador se rinde (Invalid) en vez de contar FF al azar, y no
    // se supone nada sobre el dorso.
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

TEST(EpsonEscPageCountedInChunks) {
    // Caso real (D53): PDF de 11 páginas desde Edge a doble cara, 42 MB; solo se
    // guardan 16 MB y TotalPages = 12 por el dorso en blanco. El resto se recorre
    // por bloques: cualquier corte entre bloques (dentro de un comando, de sus
    // datos o del ESC del final) debe dar lo mismo que contarlo entero.
    // 11 páginas + dorso en blanco = 12 en los datos, JI PAGES = 6 hojas.
    // Se prueban bloques de 1, 2, 3, 7 y 64 bytes (cortes en todas las
    // posiciones posibles) y uno solo con todo.
    auto bytes = Bytes(EscPageJob(11, true, true));
    const auto whole = preview::CountEscPagePages(bytes.data(), bytes.size());
    CHECK(whole.end == preview::EscPageCount::End::Complete);
    CHECK_EQ(whole.pages, 12u);
    for (size_t step : {size_t(1), size_t(2), size_t(3), size_t(7), size_t(64), bytes.size()}) {
        preview::EscPageCounter counter;
        for (size_t at = 0; at < bytes.size(); at += step)
            counter.Feed(bytes.data() + at, std::min(step, bytes.size() - at));
        auto count = counter.Result();
        CHECK(count.end == preview::EscPageCount::End::Complete);
        CHECK_EQ(count.pages, whole.pages);
        CHECK(count.lastPageBlank);
        // BytesFed se compara con JOB_INFO_2.Size para saber si se recorrió
        // todo (D53); trailer guarda el EJL del final, de donde salen las hojas.
        CHECK_EQ(counter.BytesFed(), static_cast<std::uint64_t>(bytes.size()));
        CHECK(std::string(count.trailer.begin(), count.trailer.end()).find("JI PAGES=\"6\"") != std::string::npos);
    }
    // Sin llegar al final sigue cortado; algo desconocido falla.
    preview::EscPageCounter cut;
    cut.Feed(bytes.data(), bytes.size() - 100);
    CHECK(cut.Result().end == preview::EscPageCount::End::Truncated);
    CHECK(!cut.Failed());

    // Solo el principio guardado + el conteo de todos los datos: 11 páginas
    // fiables (no TotalPages estimado) y las hojas del EJL del final.
    // head simula los 16 MB guardados (aquí la mitad de los datos); el último
    // argumento de ParsePdlSettings es el conteo hecho por bloques sobre todo
    // el trabajo.
    auto head = Bytes(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size() / 2));
    core::ContentFacts f;
    f.analyzed = true;
    f.kind = core::DataKind::Pdl;
    preview::ParsePdlSettings(head.data(), head.size(), false, f, &whole);
    CHECK(f.pageSource == core::ContentFacts::PageSource::EscPageFormFeeds);
    CHECK(f.pagesFromAllData);
    CHECK_EQ(f.pages, 11u);
    CHECK(!f.spoolerCountsBlankBack);
    CHECK_EQ(f.sheetsCounter.value, 6u);
    core::JobInfo job;
    job.datatype = L"RAW";
    job.totalPages = 12;
    auto acc = core::ComputePageAccounting(job, &f);
    CHECK_EQ(acc.dataPages.value, 11u);
    CHECK(acc.dataPages.reliability == Reliability::Reliable);
    CHECK_EQ(acc.printPages.value, 11u);

    // Un conteo que no llegó al final no se usa: TotalPages estimado, como antes.
    auto partial = cut.Result();
    core::ContentFacts g;
    g.analyzed = true;
    g.kind = core::DataKind::Pdl;
    preview::ParsePdlSettings(head.data(), head.size(), false, g, &partial);
    CHECK(g.pageSource == core::ContentFacts::PageSource::None);
    CHECK(g.spoolerCountsBlankBack);
    CHECK(core::ComputePageAccounting(job, &g).dataPages.reliability == Reliability::Estimated);
}

// Columna Presentación (D55): se deriva de la orientación y del borde de
// encuadernación de la doble cara (DMDUP_VERTICAL = borde largo,
// DMDUP_HORIZONTAL = borde corto). Sin dato fiable de alguno de los dos, "—";
// a una cara no aplica (aunque falte la orientación).
TEST(BindingFromOrientationAndDuplex) {
    i18n::SetLanguage(i18n::Language::Spanish);
    using app::BindingText;
    // Vertical + borde largo / horizontal + borde corto: se hojea de lado (Cuaderno).
    CHECK_EQ(BindingText(DMORIENT_PORTRAIT, DMDUP_VERTICAL), std::wstring(L"Cuaderno"));
    CHECK_EQ(BindingText(DMORIENT_LANDSCAPE, DMDUP_HORIZONTAL), std::wstring(L"Cuaderno"));
    // Vertical + borde corto / horizontal + borde largo: hacia arriba (libreta).
    CHECK_EQ(BindingText(DMORIENT_PORTRAIT, DMDUP_HORIZONTAL), std::wstring(L"Libreta"));
    CHECK_EQ(BindingText(DMORIENT_LANDSCAPE, DMDUP_VERTICAL), std::wstring(L"Libreta"));
    CHECK_EQ(BindingText(DMORIENT_PORTRAIT, DMDUP_SIMPLEX), std::wstring(L"No aplica"));
    CHECK_EQ(BindingText(0, DMDUP_SIMPLEX), std::wstring(L"No aplica"));
    CHECK_EQ(BindingText(0, DMDUP_VERTICAL), std::wstring(L"—"));
    CHECK_EQ(BindingText(DMORIENT_PORTRAIT, 0), std::wstring(L"—"));

    // En la lista: EMF horizontal a doble cara por el borde largo = libreta.
    core::JobInfo job;
    job.datatype = L"NT EMF 1.008";
    job.settings.present = true;
    job.settings.orientation.Set(DMORIENT_LANDSCAPE);
    job.settings.duplex.Set(DMDUP_VERTICAL);
    auto s = app::ResolveListSettings(job, nullptr);
    CHECK_EQ(s.duplex, std::wstring(L"Borde largo"));
    CHECK_EQ(s.binding, std::wstring(L"Libreta"));
    // Epson sin datos leídos: doble cara "—" y presentación también.
    // (D48: en las AM-C6000 RAW el DEVMODE del servidor es el de la cola y no
    // dice nada del trabajo.)
    job.driver = L"EPSON AM-C6000 Series";
    job.datatype = L"RAW";
    CHECK_EQ(app::ResolveListSettings(job, nullptr).binding, std::wstring(L"—"));

    // La columna va a la derecha de Doble cara en la cola y en el historial.
    for (const auto& columns : {app::JobColumns(true), app::HistoryColumns()}) {
        size_t duplex = 0, binding = 0;
        for (size_t i = 0; i < columns.size(); ++i) {
            if (columns[i].id == L"duplex" || columns[i].id == L"history.duplex") duplex = i;
            if (columns[i].id == L"binding" || columns[i].id == L"history.binding") binding = i;
        }
        CHECK_EQ(binding, duplex + 1);
    }
    // Un orden guardado por una versión anterior la recibe detrás de Doble cara.
    // (columnOrder sin "binding", como lo guardaba la 0.5.13; at() da la
    // posición de un id en el orden efectivo.)
    auto order = app::EffectiveColumnOrder({L"duplex", L"orientation", L"paper"});
    auto at = [&](const wchar_t* id) { return std::find(order.begin(), order.end(), id) - order.begin(); };
    CHECK_EQ(at(L"binding"), at(L"duplex") + 1);
}

// D34: la columna "Origen del papel" mostraba códigos ("Bandeja propia del
// controlador (258)"). Ahora la bandeja sale de los datos o del DEVMODE con los
// nombres que usa el personal en las AM-C6000, y en otros controladores con el
// nombre que declara el controlador (DeviceCapabilities).
TEST(EpsonPaperSourceShowsNames) {
    i18n::SetLanguage(i18n::Language::Spanish);
    // Datos del controlador AM-C6000: bandeja en "GS n;m iuE" y papel en PS.
    // parse(PS, parámetros de iuE) arma un trabajo de 1 página con ese papel y
    // esa bandeja y devuelve lo analizado.
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
    // Verificado con el AM-C6000: iuE 0 = Auto selección, 1;0 = Bandeja 1,
    // 2;0 = Bandeja 2 y 15;0 = alimentador manual. PS=ARCHB (12 × 18 pulg.) se
    // nombra por sus medidas para que coincida con el papel del controlador.
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
    // Los códigos >= 256 son privados de cada controlador; en el AM-C6000 el
    // 259 es la Bandeja 2.
    DEVMODEW dm = MakeDevMode();
    dm.dmFields = DM_DEFAULTSOURCE;
    dm.dmDefaultSource = 259;
    core::JobInfo emf = EmfJob(1, dm);
    emf.driver = L"EPSON AM-C6000 Series";
    CHECK_EQ(app::ResolveListSettings(emf, nullptr).source, std::wstring(L"Bandeja 2"));
    // Otro controlador: el nombre que declara (DeviceCapabilities) o uno genérico.
    // Sin la lista de bandejas de la cola (bins), texto genérico con el código;
    // con ella, el nombre que da el controlador (spooler::GetDriverForms).
    core::PrinterInfo other;
    other.driver = L"Lexmark Universal v2";
    CHECK_EQ(app::BinName(259, L"Lexmark Universal v2", &other), std::wstring(L"Bandeja propia del controlador (259)"));
    other.bins.push_back({259, L"Bandeja MP"});
    CHECK_EQ(app::BinName(259, L"Lexmark Universal v2", &other), std::wstring(L"Bandeja MP"));
    // Papel propio del controlador (625) con las medidas de 12 × 18: el nombre de la lista.
    // {código, nombre del controlador, ancho, largo en décimas de mm}: 3048 ×
    // 4572 = 12 × 18 pulgadas, así que se muestra "12 × 18" igual que con
    // PS=ARCHB en los datos. FindPaper(0): el código 0 no es ningún papel.
    core::PrinterInfo epson;
    epson.papers.push_back({625, L"12 x 18 pulg. (305 x 457 mm)", 3048, 4572});
    DEVMODEW big = MakeDevMode();
    big.dmFields = DM_PAPERSIZE;
    big.dmPaperSize = 625;
    core::JobInfo bigJob = EmfJob(1, big);
    CHECK_EQ(app::ResolveListSettings(bigJob, nullptr, &epson).paper, std::wstring(L"12 × 18"));
    CHECK(core::FindPaper(0) == nullptr);
}

// D48: en las Epson AM-C6000 (RAW) la columna Doble cara muestra "—" si no se
// leyeron los datos, en vez del DEVMODE del servidor, que es el de la cola
// (D29) y daba "Borde largo" a trabajos impresos a una cara.
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

    // Con los datos leídos (EJL mínimo con DX=OFF y dos FF = 2 páginas) se
    // muestra lo que dicen los datos: una cara.
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

// D48: ReadLooksIncomplete(tamaño del trabajo, bytes leídos, cortado a
// propósito) detecta lecturas a las que otra copia de la app se llevó bloques.
// Los números son los medidos en vtec-laimiprint: Doc1.pdf (5 112 491 B) leído
// como 5 bloques de 256 KB = 1 310 720 B, y Traslado.pdf (2 064 121 B) leído
// como 1 277 689 B.
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
    // Los comentarios DSC (Document Structuring Conventions de Adobe) están
    // documentados (D25): NumCopies 3 en el Setup, *PageSize A4,
    // *Duplex DuplexTumble (doble cara volteando por el borde corto =
    // DMDUP_HORIZONTAL) y %%Orientation. "%%Pages: (atend)" significa que el
    // número de páginas va en el %%Trailer del final. El "NumCopies 99" dentro
    // de la página 1 es contenido y debe ignorarse.
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
    // (Datos cortados antes del %%Trailer: no se inventa un número de páginas,
    // pero las opciones del encabezado sí valen.)
    core::ContentFacts partial;
    auto head = Bytes(ps.substr(0, ps.find("%%Trailer")));
    preview::ParsePdlSettings(head.data(), head.size(), false, partial);
    CHECK(partial.pageSource == core::ContentFacts::PageSource::None);
    CHECK_EQ(partial.copies.value, 3u);
}

// --- PrintTicket XPS ------------------------------------------------------

// PrintTicket (XML del Print Schema de Microsoft) dentro de un XPS (D25). El de
// nivel trabajo declara: 3 copias, A4 (MediaSizeWidth/Height en micrómetros:
// 210000 × 297000 -> 2100 × 2970 décimas de mm), horizontal, escala de grises
// (DMCOLOR_MONOCHROME), doble cara por el borde largo (DMDUP_VERTICAL) y 4
// páginas por hoja.
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
    // Solo cambia la orientación; las copias siguen siendo 3. Un ticket sin
    // ninguna opción reconocida devuelve false (no aportó nada).
    CHECK(preview::ApplyPrintTicket(
        R"(<psf:PrintTicket><psf:Feature name="psk:PageOrientation"><psf:Option name="psk:Portrait"/></psf:Feature></psf:PrintTicket>)",
        f));
    CHECK_EQ(f.orientation.value, static_cast<short>(DMORIENT_PORTRAIT));
    CHECK_EQ(f.copies.value, 3u);
    CHECK(!preview::ApplyPrintTicket("<psf:PrintTicket></psf:PrintTicket>", f));
}

// --- Ficha "Configuración" -------------------------------------------------

// Ficha "Configuración" (F7, app/JobSheet): cada valor muestra su fiabilidad
// como insignia ("Fiable", "Estimado"...). Con el caso de 1 página × 10 copias
// y los datos analizados, las caras son 10 y fiables; el resumen del
// encabezado dice "10 págs. a imprimir".
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
    // Se buscan las filas por su título (en español) y se marca que aparecieron,
    // para que la prueba falle también si una fila desaparece.
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

// Columna "Páginas" = páginas × copias (D31), con la fiabilidad guardada
// aparte: la lista muestra solo el número y la ficha dice si es estimado.
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
    // Además, cada fila tiene exactamente una celda por columna (con y sin la
    // columna Impresora, y en el historial): si no coinciden, la tabla
    // dibujaría valores bajo el título equivocado.
    auto columns = app::JobColumns(false);
    auto row = app::MakeJobRow(job, false, &facts);
    CHECK_EQ(row.cells.size(), columns.size());
    for (size_t i = 0; i < columns.size(); ++i)
        if (columns[i].label == L"Páginas") CHECK_EQ(row.cells[i], std::wstring(L"9"));
    CHECK_EQ(app::MakeJobRow(job, true, &facts).cells.size(), app::JobColumns(true).size());
    CHECK(app::HistoryColumns().size() == app::MakeHistoryRow(config::HistoryEntry{}, 0).cells.size());
}

// D41: el historial guarda lo mismo que se veía en la tabla (posición,
// páginas × copias, papel, orientación, doble cara, origen, Enviado y, desde
// D56, el carnet). Se arma un registro con MakeHistoryEntry a partir de un
// trabajo EMF de 3 págs. × 3 copias, horizontal y a doble cara, se guarda en
// un historial.json temporal y se vuelve a leer. De paso se prueban los
// casos de D52: archivo con BOM y archivo dañado.
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
    auto entry = app::MakeHistoryEntry(job, config::JobOutcome::Printed, 1'800'000'000, &facts, nullptr, L"2022040771");

    // Guardar y volver a leer no pierde nada.
    // (Archivo en %TEMP%, no el historial real del usuario; se borra al final.
    // El bloque { } hace que el primer History se destruya antes de leer.)
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
    // writeRaw reemplaza el archivo con bytes exactos (sin pasar por History).
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
    // JSON cortado a la mitad: Load falla y deja historial.json.danado
    // (BackupPath); después Save puede escribir el archivo nuevo. Luego se
    // comparan los campos del registro leído al principio (loaded).
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
    CHECK_EQ(back.carnet, std::wstring(L"2022040771"));
    CHECK_EQ(back.printPages, entry.printPages);
    CHECK_EQ(back.paper, entry.paper);
    CHECK_EQ(back.orientation, entry.orientation);
    CHECK_EQ(back.duplex, entry.duplex);
    CHECK_EQ(back.source, entry.source);
    CHECK_EQ(back.submittedUnix, entry.submittedUnix);

    // Cada columna de la cola tiene la suya en el historial, con el mismo valor
    // (salvo Estado, que en el historial es el resultado).
    auto jobColumns = app::JobColumns(true);
    auto jobRow = app::MakeJobRow(job, true, &facts, nullptr, L"2022040771");
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
    // Índice 7 = Páginas en la cola con Impresora (#, Documento, Estado,
    // Usuario, Carnet, Equipo, Impresora, Páginas): 3 × 3 = 9.
    CHECK_EQ(jobRow.cells[7], std::wstring(L"9"));  // páginas × copias

    // Un registro de una versión anterior (sin esos datos) se muestra con "—".
    // Páginas cae a las del spooler (pages = 5) y la columna siguiente (sin
    // dato) muestra "—".
    config::HistoryEntry old;
    old.pages = 5;
    auto oldRow = app::MakeHistoryRow(old, 0);
    CHECK_EQ(oldRow.cells.size(), historyColumns.size());
    CHECK_EQ(oldRow.cells[7], std::wstring(L"5"));
    CHECK_EQ(oldRow.cells[8], std::wstring(L"—"));
}

// Columna Enviado: fecha y hora local "dd/mm/aaaa hh:mm" a partir del
// SYSTEMTIME en UTC del spooler. La hora exacta depende de la zona horaria del
// equipo que corre las pruebas, por eso solo se comprueba el formato (largo 16
// y separadores) y el año; un SYSTEMTIME vacío da texto vacío.
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

// D30: clic en el título de una columna ordena por ella (ascendente) y otro
// clic en la misma invierte. NextSort(criterio actual, invertido, columna
// pulsada) devuelve el nuevo {criterio, invertido}.
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
    // a tiene más TotalPages (4) pero b imprime más (10): descendente, b
    // primero.
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

// D28: los trabajos enviados desde otras PCs a veces no aparecían porque
// Windows respondía desde la caché de Client Side Rendering. La lista que
// manda es la del servidor (consultada por el nombre DNS completo); de la
// caché solo se agregan las copias locales que esta PC todavía procesa
// (RENDERING_LOCALLY).
TEST(ReconcileJobListsTrustsServer) {
    // Trabajo mínimo con id, posición y JOB_STATUS_* crudo.
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

    // Nombre de la cola por el servidor con nombre completo (salta la caché);
    // una cola local no tiene servidor, así que no hay nombre alternativo.

    CHECK_EQ(core::spooler::ReplacePrinterServer(L"\\\\vtec-laimiprint\\LAIMI 1 - INK", L"vtec-laimiprint.dominio"),
             std::wstring(L"\\\\vtec-laimiprint.dominio\\LAIMI 1 - INK"));
    CHECK(core::spooler::ReplacePrinterServer(L"CutePDF Writer", L"x").empty());
    CHECK(core::spooler::CacheBypassName(L"CutePDF Writer").empty());  // cola local: no hay caché de cliente
}

// --- Archivos temporales ----------------------------------------------------

// D27: las copias para Ghostscript y las miniaturas pueden contener documentos
// confidenciales de estudiantes, así que van en una carpeta por proceso con
// DACL protegida (solo el usuario actual, sin heredar permisos de %TEMP%) que
// se borra entera al terminar.
TEST(PrivateTempDirIsProtectedAndRemoved) {
    // NewTempPath debe dar rutas dentro de esa carpeta, y se puede crear un
    // archivo en ella.
    std::wstring dir = preview::PrivateTempDir();
    CHECK(!dir.empty());
    std::wstring path = preview::NewTempPath(L".tmp");
    CHECK(path.rfind(dir, 0) == 0);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);

    // La DACL de la carpeta está protegida (no hereda de %TEMP%).
    // Primera llamada a GetFileSecurityW para saber el tamaño; segunda para
    // leer el descriptor y mirar el bit SE_DACL_PROTECTED.
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

    // Al quitarla desaparece con todo su contenido (incluido el archivo creado).
    preview::RemovePrivateTempDir();
    CHECK(GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES);
}
