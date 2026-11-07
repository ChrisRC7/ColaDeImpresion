// Pruebas de preview/: extracción de páginas de un spool EMF sintético,
// detección de formatos, PJL, conteo de páginas XPS y caché.
//
// Módulos probados:
//   - preview/EmfSpool: ExtractEmfPages (páginas EMF dentro de un EMFSPOOL) y
//     EmfPage (tamaño y dibujo de una página). Es la vista previa nativa (D7).
//   - preview/SpoolFile: DetectFormat (EMF/XPS/PDF/PS/PCL/PCL XL/texto),
//     SkipPjlHeader, rutas de la carpeta de spool (ToAdminShare,
//     SpoolDirectories, SpoolFileCandidates, D6) y ShadowMatchesJob (búsqueda
//     del trabajo en los .SHD con "spool file pooling").
//   - preview/ZipReader: CountXpsPages (un XPS es un ZIP).
//   - preview/PreviewTypes: FitPage (ajuste de la página al panel).
//   - core/Spooler: IsLocalServer.
// (La caché de PreviewService no tiene pruebas en este archivo: depende de
// hilos y del spooler.)
// Ninguna prueba necesita una impresora ni el spooler: los spools, ZIP y .SHD
// se arman en memoria con las funciones auxiliares de abajo. Las de EMF sí usan
// GDI real (CreateEnhMetaFile), que existe en cualquier Windows.

#include <windows.h>

#include <cstring>

#include "TestFramework.h"
#include "core/Spooler.h"
#include "preview/EmfSpool.h"
#include "preview/PreviewTypes.h"
#include "preview/SpoolFile.h"
#include "preview/ZipReader.h"

using namespace cola::preview;
namespace core = cola::core;

namespace {

// Crea en memoria un EMF real de una página tamaño carta con un rectángulo y
// el texto "Hola", y devuelve sus bytes (lo mismo que contiene cada página de
// un spool EMF). `marker` cambia el ancho del rectángulo para que cada página
// sea distinta (bytes diferentes), como en un documento real.
std::vector<std::uint8_t> MakeEmf(int marker) {
    // DC de referencia: la pantalla (el EMF toma de él la resolución).
    HDC ref = GetDC(nullptr);
    // 21590 × 27940 centésimas de mm = 215,9 × 279,4 mm = 8,5 × 11 pulgadas.
    RECT frame{0, 0, 21590, 27940};  // carta, en HIMETRIC
    // nullptr como archivo = EMF en memoria. La descripción de un EMF termina
    // con dos caracteres nulos (aplicación\0documento\0\0).
    HDC dc = CreateEnhMetaFileW(ref, nullptr, &frame, L"test\0\0");
    ReleaseDC(nullptr, ref);
    Rectangle(dc, 10, 10, 100 + marker, 100);
    TextOutW(dc, 20, 20, L"Hola", 4);
    HENHMETAFILE emf = CloseEnhMetaFile(dc);
    // Primera llamada: tamaño; segunda: copia de los bytes.
    UINT size = GetEnhMetaFileBits(emf, 0, nullptr);
    std::vector<std::uint8_t> bytes(size);
    GetEnhMetaFileBits(emf, size, bytes.data());
    DeleteEnhMetaFile(emf);
    return bytes;
}

// Agrega un entero de 32 bits en little endian (el orden de bytes de x86/x64,
// que es el de todos los formatos que se arman aquí: EMFSPOOL, ZIP y .SHD).
void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    std::uint8_t b[4];
    std::memcpy(b, &v, 4);
    out.insert(out.end(), b, b + 4);
}

// Simula un archivo EMFSPOOL: encabezado + registros con EMF por página + basura.
// Formato ([MS-EMFSPOOL]): un encabezado y después registros "tipo (u32) +
// tamaño de los datos (u32) + datos". Es lo que deja en el spool una impresora
// con el formato de datos EMF (renderizado en el servidor).
std::vector<std::uint8_t> MakeEmfSpool(int pages) {
    std::vector<std::uint8_t> spool;
    AppendU32(spool, 0x00010000);  // versión
    AppendU32(spool, 16);          // cjSize del encabezado
    // Desplazamientos del nombre del documento y del puerto de salida; 0 =
    // ausentes (el encabezado ocupa justo los 16 bytes declarados).
    AppendU32(spool, 0);
    AppendU32(spool, 0);
    // Un registro "desconocido" (p. ej. DEVMODE) para comprobar que se salta.
    // 0x03 es EMRI_DEVMODE; aquí con 8 bytes de relleno en vez de un DEVMODE
    // real. El extractor debe saltarlo usando el tamaño declarado.
    AppendU32(spool, 0x03);
    AppendU32(spool, 8);
    AppendU32(spool, 0xDEADBEEF);
    AppendU32(spool, 0xCAFEBABE);
    // Cada página: un EMRI_METAFILE_DATA con el EMF completo, seguido del
    // EMRI_METAFILE_EXT que en un spool real apunta hacia atrás a ese EMF.
    for (int i = 0; i < pages; ++i) {
        auto emf = MakeEmf(i);
        AppendU32(spool, 0x0C);  // EMRI_METAFILE_DATA
        AppendU32(spool, static_cast<std::uint32_t>(emf.size()));
        spool.insert(spool.end(), emf.begin(), emf.end());
        AppendU32(spool, 0x0D);  // EMRI_METAFILE_EXT (desplazamiento; ignorado)
        AppendU32(spool, 8);
        AppendU32(spool, 0);
        AppendU32(spool, 0);
    }
    return spool;
}

// Bytes de un texto C (sin el nulo final): para armar datos RAW de prueba
// (PostScript, PDF, PJL, PCL) a partir de literales.
std::vector<std::uint8_t> Bytes(const char* text) {
    return std::vector<std::uint8_t>(text, text + std::strlen(text));
}

// ZIP mínimo (sin compresión) con los nombres dados, con directorio central.
// Las entradas están vacías (0 bytes, CRC 0): para contar páginas XPS solo
// importan los nombres. Estructura (APPNOTE de PKWARE): un encabezado local por
// entrada ("PK\3\4"), luego el directorio central (una entrada "PK\1\2" por
// archivo) y al final el registro de fin de directorio central ("PK\5\6").
std::vector<std::uint8_t> MakeZip(const std::vector<std::string>& names) {
    std::vector<std::uint8_t> zip;
    // Agrega un entero de 16 bits en little endian.
    auto u16 = [&](std::vector<std::uint8_t>& v, std::uint16_t x) {
        v.push_back(static_cast<std::uint8_t>(x));
        v.push_back(static_cast<std::uint8_t>(x >> 8));
    };
    // Desplazamiento de cada encabezado local (el directorio central lo apunta).
    std::vector<std::uint32_t> offsets;
    for (const auto& n : names) {
        offsets.push_back(static_cast<std::uint32_t>(zip.size()));
        // Encabezado local: firma, 5 campos de 16 bits, CRC, tamaños, largo del
        // nombre, largo del campo extra (0) y el nombre.
        AppendU32(zip, 0x04034b50);
        for (int i = 0; i < 5; ++i) u16(zip, 0);  // versión, flags, método, hora, fecha
        AppendU32(zip, 0);                       // crc
        AppendU32(zip, 0);                       // comprimido
        AppendU32(zip, 0);                       // sin comprimir
        u16(zip, static_cast<std::uint16_t>(n.size()));
        u16(zip, 0);
        zip.insert(zip.end(), n.begin(), n.end());
    }
    // Directorio central: es lo que un lector de ZIP consulta primero.
    std::uint32_t cdStart = static_cast<std::uint32_t>(zip.size());
    for (size_t i = 0; i < names.size(); ++i) {
        AppendU32(zip, 0x02014b50);
        // Versión creadora, versión necesaria, flags, método, hora y fecha.
        for (int k = 0; k < 6; ++k) u16(zip, 0);
        AppendU32(zip, 0);  // crc
        AppendU32(zip, 0);  // comprimido
        AppendU32(zip, 0);  // sin comprimir
        u16(zip, static_cast<std::uint16_t>(names[i].size()));
        // Largo del extra y del comentario, disco inicial y atributos internos.
        u16(zip, 0);
        u16(zip, 0);
        u16(zip, 0);
        u16(zip, 0);
        AppendU32(zip, 0);  // atributos externos
        AppendU32(zip, offsets[i]);
        zip.insert(zip.end(), names[i].begin(), names[i].end());
    }
    // Fin del directorio central (22 bytes): número de disco (0, 0), cantidad de
    // entradas en este disco y en total, tamaño y posición del directorio
    // central, y largo del comentario (0).
    std::uint32_t cdSize = static_cast<std::uint32_t>(zip.size()) - cdStart;
    AppendU32(zip, 0x06054b50);
    u16(zip, 0);
    u16(zip, 0);
    u16(zip, static_cast<std::uint16_t>(names.size()));
    u16(zip, static_cast<std::uint16_t>(names.size()));
    AppendU32(zip, cdSize);
    AppendU32(zip, cdStart);
    u16(zip, 0);
    return zip;
}

}  // namespace

// Un spool EMF de 3 páginas: se extraen las 3, cada una es un EMF válido que
// Windows puede cargar, el máximo de páginas se respeta (la vista previa limita
// cuántas dibuja) sin dejar de contar todas, y el formato se detecta como
// EmfSpool.
TEST(EmfSpoolExtraction) {
    auto spool = MakeEmfSpool(3);
    size_t total = 0;
    auto pages = ExtractEmfPages(spool.data(), spool.size(), 10, &total);
    CHECK_EQ(total, size_t(3));
    CHECK_EQ(pages.size(), size_t(3));
    // Cada página extraída debe ser un EMF íntegro: SetEnhMetaFileBits lo
    // valida y falla (nullptr) si los bytes están cortados o desplazados.
    for (auto& p : pages) {
        HENHMETAFILE emf = SetEnhMetaFileBits(static_cast<UINT>(p.size()), p.data());
        CHECK(emf != nullptr);
        if (emf) DeleteEnhMetaFile(emf);
    }
    // Respeta el máximo pero cuenta todas.
    // (El panel dice "Se muestran solo las primeras N de M páginas".)
    pages = ExtractEmfPages(spool.data(), spool.size(), 2, &total);
    CHECK_EQ(pages.size(), size_t(2));
    CHECK_EQ(total, size_t(3));
    // La firma 0x00010000 al inicio identifica el EMFSPOOL.
    CHECK(DetectFormat(spool.data(), spool.size()) == DataFormat::EmfSpool);
}

// EmfPage lee el tamaño de la página del encabezado del EMF (rclFrame) para
// dibujarla con la proporción correcta, y Render la reproduce sobre un DC.
TEST(EmfPageSizeAndRender) {
    EmfPage page(MakeEmf(0));
    SIZE s = page.SizeHimetric();
    // Se pidió carta (21590 × 27940 HIMETRIC). GDI ajusta el marco a la
    // resolución del DC de referencia, así que se aceptan márgenes en lugar de
    // valores exactos.
    CHECK(s.cx > 20000 && s.cx < 23000);
    CHECK(s.cy > 26000 && s.cy < 29000);
    // Renderizar sobre un DC de memoria no debe fallar.
    // (No se comprueban los píxeles: solo que no se cuelgue ni se caiga. El
    // mapa de bits de 100 × 130 tiene más o menos la proporción de una carta.)
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateCompatibleBitmap(GetDC(nullptr), 100, 130);
    HGDIOBJ old = SelectObject(dc, bmp);
    page.Render(dc, RECT{0, 0, 100, 130});
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// Un spool incompleto (el trabajo todavía se está recibiendo, o los datos se
// cortaron por el límite de lectura) no debe producir una página EMF rota:
// una página cuyo registro declara más bytes de los que hay se descarta.
TEST(TruncatedEmfIsIgnored) {
    auto spool = MakeEmfSpool(2);
    // Los últimos 16 bytes son el EMRI_METAFILE_EXT de la página 2; quitar 40
    // también corta 24 bytes del final de su EMF. Solo queda entera la 1.
    spool.resize(spool.size() - 40);  // corta la última página
    size_t total = 0;
    auto pages = ExtractEmfPages(spool.data(), spool.size(), 10, &total);
    CHECK_EQ(total, size_t(1));
}

// Detección del lenguaje de los datos RAW. Con Client Side Rendering el
// servidor recibe los datos ya convertidos por el controlador (D7), y de este
// formato depende qué renderizador se usa para la vista previa (Ghostscript,
// GhostPCL...) y qué opciones se pueden leer.
TEST(DetectPrinterLanguages) {
    // PostScript por su firma "%!PS" (aquí con comentarios DSC).
    auto ps = Bytes("%!PS-Adobe-3.0\n%%Pages: 2\n");
    CHECK(DetectFormat(ps.data(), ps.size()) == DataFormat::PostScript);
    auto pdf = Bytes("%PDF-1.7\n");
    CHECK(DetectFormat(pdf.data(), pdf.size()) == DataFormat::Pdf);
    // Con encabezado PJL: "\x1b%-12345X" es el UEL (Universal Exit Language)
    // que abre el bloque PJL. El formato se decide por lo que viene después
    // (ENTER LANGUAGE y la firma del lenguaje), no por el PJL en sí.
    auto pjlPs = Bytes("\x1b%-12345X@PJL JOB\r\n@PJL ENTER LANGUAGE=POSTSCRIPT\r\n%!PS\n");
    CHECK(DetectFormat(pjlPs.data(), pjlPs.size()) == DataFormat::PostScript);
    // PCL XL (PCL 6): su flujo binario empieza con el encabezado de texto
    // ") HP-PCL XL;2;0" (versión de protocolo 2.0).
    auto pjlXl = Bytes("\x1b%-12345X@PJL ENTER LANGUAGE=PCLXL\n) HP-PCL XL;2;0\n");
    CHECK(DetectFormat(pjlXl.data(), pjlXl.size()) == DataFormat::PclXl);
    // PCL 5: ESC E (reinicio de la impresora) y ESC&l0O (orientación vertical).
    // El literal se parte en "\x1b" "E" porque "\x1bE" se leería como un solo
    // escape hexadecimal \x1bE.
    auto pcl = Bytes("\x1b" "E\x1b&l0O");
    CHECK(DetectFormat(pcl.data(), pcl.size()) == DataFormat::Pcl);
    // Texto plano imprimible (impresoras "Generic / Text Only").
    auto text = Bytes("Hola mundo\r\nlinea 2\r\n");
    CHECK(DetectFormat(text.data(), text.size()) == DataFormat::Text);
    // Bytes binarios sin firma conocida: Unknown (no se debe adivinar).
    std::vector<std::uint8_t> binary = {0x00, 0x01, 0x02, 0x03, 0xff, 0x10, 0x00, 0x05};
    CHECK(DetectFormat(binary.data(), binary.size()) == DataFormat::Unknown);
}

// SkipPjlHeader devuelve dónde empiezan los datos del lenguaje después del UEL
// y las líneas "@PJL ...". Hace falta para pasar a Ghostscript solo el
// PostScript (el PJL no es PostScript válido).
TEST(SkipPjl) {
    auto data = Bytes("\x1b%-12345X@PJL JOB NAME=\"x\"\n@PJL ENTER LANGUAGE=POSTSCRIPT\n%!PS-Adobe");
    size_t pos = SkipPjlHeader(data.data(), data.size());
    CHECK(std::memcmp(data.data() + pos, "%!PS", 4) == 0);
    // Sin encabezado PJL los datos empiezan en 0.
    auto plain = Bytes("%!PS");
    CHECK_EQ(SkipPjlHeader(plain.data(), plain.size()), size_t(0));
}

// Las páginas de un XPS (drivers v4, D25) son las partes "*.fpage" del ZIP.
// La lista imita un XPS real: tipos de contenido, secuencia de documentos,
// documento, 3 páginas y una fuente (que no debe contarse).
TEST(XpsPageCount) {
    auto zip = MakeZip({"[Content_Types].xml", "FixedDocumentSequence.fdseq", "Documents/1/FixedDocument.fdoc",
                        "Documents/1/Pages/1.fpage", "Documents/1/Pages/2.fpage", "Documents/1/Pages/3.fpage",
                        "Documents/1/Resources/Fonts/font.odttf"});
    CHECK_EQ(CountXpsPages(zip.data(), zip.size()), 3u);
    CHECK(DetectFormat(zip.data(), zip.size()) == DataFormat::Xps);
    // Sin directorio central (spool todavía escribiéndose): usa los encabezados locales.
    // Quitar 60 bytes elimina el fin de directorio (22 bytes) y parte del
    // directorio central; los dos encabezados locales quedan enteros.
    auto partial = MakeZip({"Documents/1/Pages/1.fpage", "Documents/1/Pages/2.fpage"});
    partial.resize(partial.size() - 60);
    CHECK_EQ(CountXpsPages(partial.data(), partial.size()), 2u);
}

// Rutas del archivo de spool (respaldo de la lectura por la API, D6/D22). El
// servidor informa su carpeta de spool como ruta local ("C:\..."), que desde la
// PC del operador solo se alcanza por el recurso administrativo C$.
TEST(AdminShareConversion) {
    // Ruta local del servidor \\SRV -> \\SRV\C$\... (la letra de unidad pasa a
    // ser el recurso "C$").
    CHECK_EQ(ToAdminShare(L"\\\\SRV", L"C:\\Windows\\System32\\spool\\PRINTERS"),
             std::wstring(L"\\\\SRV\\C$\\Windows\\System32\\spool\\PRINTERS"));
    // Sin servidor (este equipo) la ruta queda igual.
    CHECK_EQ(ToAdminShare(L"", L"C:\\X"), std::wstring(L"C:\\X"));
    // La carpeta alternativa configurada se prueba primero.
    // SpoolSearch{servidor, cola, carpeta alternativa}: el archivo de un trabajo
    // se llama con su id en 5 dígitos (42 -> 00042.SPL); la barra final de la
    // carpeta no se duplica.
    auto candidates = SpoolFileCandidates(SpoolSearch{L"", L"", L"D:\\spool\\"}, 42);
    CHECK(!candidates.empty());
    CHECK_EQ(candidates.front(), std::wstring(L"D:\\spool\\00042.SPL"));
    // En un servidor remoto las rutas pasan por los recursos administrativos.
    // Se usa un nombre que no existe: sin poder preguntarle su carpeta, igual
    // deben salir rutas UNC hacia ese servidor (las predeterminadas por C$ /
    // ADMIN$), nunca rutas locales de esta PC.
    auto remote = SpoolDirectories(SpoolSearch{L"\\\\SRV-QUE-NO-EXISTE", L"", L""});
    CHECK(!remote.empty());
    CHECK(remote.front().rfind(L"\\\\SRV-QUE-NO-EXISTE\\", 0) == 0);
    // Este mismo equipo se alcanza con rutas locales (sin pasar por C$).
    // "" y "localhost" son este equipo (D4: la app puede correr en el propio
    // servidor); un nombre remoto no lo es.
    CHECK(core::spooler::IsLocalServer(L""));
    CHECK(core::spooler::IsLocalServer(L"localhost"));
    CHECK(!core::spooler::IsLocalServer(L"\\\\SRV-QUE-NO-EXISTE"));
}

// Con "spool file pooling" los archivos se llaman FPnnnnn.SPL/.SHD y el número
// no es el id del trabajo: hay que abrir cada .SHD (el archivo "sombra" con
// los datos del trabajo) y ver si es el buscado. ShadowMatchesJob busca el id
// en los primeros DWORD del encabezado (su posición varía entre versiones de
// Windows y 32/64 bits) y lo confirma con el nombre del documento en UTF-16.
TEST(ShadowFileMatching) {
    // .SHD simulado: firma, tamaño, estado, id del trabajo y el nombre del documento en UTF-16.
    std::vector<std::uint8_t> shd;
    AppendU32(shd, 0x00005123);  // firma/versión del .SHD
    AppendU32(shd, 0x000000E8);  // tamaño del encabezado (232 bytes)
    AppendU32(shd, 0x00000000);
    AppendU32(shd, 0x00000000);
    AppendU32(shd, 42);  // id del trabajo
    // Relleno hasta 128 bytes: el nombre del documento va fuera de la zona
    // donde se busca el id (bytes 4 a 64), como en un .SHD real.
    shd.resize(128, 0);
    std::wstring doc = L"Informe final.docx";
    auto* raw = reinterpret_cast<const std::uint8_t*>(doc.c_str());
    shd.insert(shd.end(), raw, raw + doc.size() * sizeof(wchar_t));
    // Id y documento coinciden; sin documento conocido basta el id; con otro id
    // u otro documento no coincide (un id pequeño como 42 podría aparecer por
    // azar en otro campo, por eso se exige el documento en la primera pasada).
    CHECK(ShadowMatchesJob(shd.data(), shd.size(), 42, doc));
    CHECK(ShadowMatchesJob(shd.data(), shd.size(), 42, L""));
    CHECK(!ShadowMatchesJob(shd.data(), shd.size(), 43, doc));
    CHECK(!ShadowMatchesJob(shd.data(), shd.size(), 42, L"Otro documento"));
    // Segunda pasada: solo por id (servidores que guardan el documento de otra forma).
    // El último argumento (requireDocument = false) ignora el documento, pero
    // el id tiene que seguir coincidiendo.
    CHECK(ShadowMatchesJob(shd.data(), shd.size(), 42, L"Otro documento", false));
    CHECK(!ShadowMatchesJob(shd.data(), shd.size(), 43, L"Otro documento", false));
}

// FitPage ubica la página en el panel de vista previa conservando su
// proporción y centrada. Una carta (21590 × 27940, proporción 0,7727) en un
// cuadrado de 1000 × 1000 queda limitada por la altura: 1000 de alto y unos
// 772-773 de ancho, con el mismo margen a cada lado.
TEST(FitPageKeepsAspect) {
    RECT r = FitPage({21590, 27940}, RECT{0, 0, 1000, 1000});
    int w = r.right - r.left, h = r.bottom - r.top;
    CHECK(h == 1000);
    CHECK(w > 770 && w < 775);
    CHECK_EQ(r.left, (1000 - w) / 2);
}
