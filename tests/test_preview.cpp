// Pruebas de preview/: extracción de páginas de un spool EMF sintético,
// detección de formatos, PJL, conteo de páginas XPS y caché.

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

std::vector<std::uint8_t> MakeEmf(int marker) {
    HDC ref = GetDC(nullptr);
    RECT frame{0, 0, 21590, 27940};  // carta, en HIMETRIC
    HDC dc = CreateEnhMetaFileW(ref, nullptr, &frame, L"test\0\0");
    ReleaseDC(nullptr, ref);
    Rectangle(dc, 10, 10, 100 + marker, 100);
    TextOutW(dc, 20, 20, L"Hola", 4);
    HENHMETAFILE emf = CloseEnhMetaFile(dc);
    UINT size = GetEnhMetaFileBits(emf, 0, nullptr);
    std::vector<std::uint8_t> bytes(size);
    GetEnhMetaFileBits(emf, size, bytes.data());
    DeleteEnhMetaFile(emf);
    return bytes;
}

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    std::uint8_t b[4];
    std::memcpy(b, &v, 4);
    out.insert(out.end(), b, b + 4);
}

// Simula un archivo EMFSPOOL: encabezado + registros con EMF por página + basura.
std::vector<std::uint8_t> MakeEmfSpool(int pages) {
    std::vector<std::uint8_t> spool;
    AppendU32(spool, 0x00010000);  // versión
    AppendU32(spool, 16);          // cjSize del encabezado
    AppendU32(spool, 0);
    AppendU32(spool, 0);
    // Un registro "desconocido" (p. ej. DEVMODE) para comprobar que se salta.
    AppendU32(spool, 0x03);
    AppendU32(spool, 8);
    AppendU32(spool, 0xDEADBEEF);
    AppendU32(spool, 0xCAFEBABE);
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

std::vector<std::uint8_t> Bytes(const char* text) {
    return std::vector<std::uint8_t>(text, text + std::strlen(text));
}

// ZIP mínimo (sin compresión) con los nombres dados, con directorio central.
std::vector<std::uint8_t> MakeZip(const std::vector<std::string>& names) {
    std::vector<std::uint8_t> zip;
    auto u16 = [&](std::vector<std::uint8_t>& v, std::uint16_t x) {
        v.push_back(static_cast<std::uint8_t>(x));
        v.push_back(static_cast<std::uint8_t>(x >> 8));
    };
    std::vector<std::uint32_t> offsets;
    for (const auto& n : names) {
        offsets.push_back(static_cast<std::uint32_t>(zip.size()));
        AppendU32(zip, 0x04034b50);
        for (int i = 0; i < 5; ++i) u16(zip, 0);  // versión, flags, método, hora, fecha
        AppendU32(zip, 0);                       // crc
        AppendU32(zip, 0);                       // comprimido
        AppendU32(zip, 0);                       // sin comprimir
        u16(zip, static_cast<std::uint16_t>(n.size()));
        u16(zip, 0);
        zip.insert(zip.end(), n.begin(), n.end());
    }
    std::uint32_t cdStart = static_cast<std::uint32_t>(zip.size());
    for (size_t i = 0; i < names.size(); ++i) {
        AppendU32(zip, 0x02014b50);
        for (int k = 0; k < 6; ++k) u16(zip, 0);
        AppendU32(zip, 0);
        AppendU32(zip, 0);
        AppendU32(zip, 0);
        u16(zip, static_cast<std::uint16_t>(names[i].size()));
        u16(zip, 0);
        u16(zip, 0);
        u16(zip, 0);
        u16(zip, 0);
        AppendU32(zip, 0);
        AppendU32(zip, offsets[i]);
        zip.insert(zip.end(), names[i].begin(), names[i].end());
    }
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

TEST(EmfSpoolExtraction) {
    auto spool = MakeEmfSpool(3);
    size_t total = 0;
    auto pages = ExtractEmfPages(spool.data(), spool.size(), 10, &total);
    CHECK_EQ(total, size_t(3));
    CHECK_EQ(pages.size(), size_t(3));
    for (auto& p : pages) {
        HENHMETAFILE emf = SetEnhMetaFileBits(static_cast<UINT>(p.size()), p.data());
        CHECK(emf != nullptr);
        if (emf) DeleteEnhMetaFile(emf);
    }
    // Respeta el máximo pero cuenta todas.
    pages = ExtractEmfPages(spool.data(), spool.size(), 2, &total);
    CHECK_EQ(pages.size(), size_t(2));
    CHECK_EQ(total, size_t(3));
    CHECK(DetectFormat(spool.data(), spool.size()) == DataFormat::EmfSpool);
}

TEST(EmfPageSizeAndRender) {
    EmfPage page(MakeEmf(0));
    SIZE s = page.SizeHimetric();
    CHECK(s.cx > 20000 && s.cx < 23000);
    CHECK(s.cy > 26000 && s.cy < 29000);
    // Renderizar sobre un DC de memoria no debe fallar.
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateCompatibleBitmap(GetDC(nullptr), 100, 130);
    HGDIOBJ old = SelectObject(dc, bmp);
    page.Render(dc, RECT{0, 0, 100, 130});
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
}

TEST(TruncatedEmfIsIgnored) {
    auto spool = MakeEmfSpool(2);
    spool.resize(spool.size() - 40);  // corta la última página
    size_t total = 0;
    auto pages = ExtractEmfPages(spool.data(), spool.size(), 10, &total);
    CHECK_EQ(total, size_t(1));
}

TEST(DetectPrinterLanguages) {
    auto ps = Bytes("%!PS-Adobe-3.0\n%%Pages: 2\n");
    CHECK(DetectFormat(ps.data(), ps.size()) == DataFormat::PostScript);
    auto pdf = Bytes("%PDF-1.7\n");
    CHECK(DetectFormat(pdf.data(), pdf.size()) == DataFormat::Pdf);
    auto pjlPs = Bytes("\x1b%-12345X@PJL JOB\r\n@PJL ENTER LANGUAGE=POSTSCRIPT\r\n%!PS\n");
    CHECK(DetectFormat(pjlPs.data(), pjlPs.size()) == DataFormat::PostScript);
    auto pjlXl = Bytes("\x1b%-12345X@PJL ENTER LANGUAGE=PCLXL\n) HP-PCL XL;2;0\n");
    CHECK(DetectFormat(pjlXl.data(), pjlXl.size()) == DataFormat::PclXl);
    auto pcl = Bytes("\x1b" "E\x1b&l0O");
    CHECK(DetectFormat(pcl.data(), pcl.size()) == DataFormat::Pcl);
    auto text = Bytes("Hola mundo\r\nlinea 2\r\n");
    CHECK(DetectFormat(text.data(), text.size()) == DataFormat::Text);
    std::vector<std::uint8_t> binary = {0x00, 0x01, 0x02, 0x03, 0xff, 0x10, 0x00, 0x05};
    CHECK(DetectFormat(binary.data(), binary.size()) == DataFormat::Unknown);
}

TEST(SkipPjl) {
    auto data = Bytes("\x1b%-12345X@PJL JOB NAME=\"x\"\n@PJL ENTER LANGUAGE=POSTSCRIPT\n%!PS-Adobe");
    size_t pos = SkipPjlHeader(data.data(), data.size());
    CHECK(std::memcmp(data.data() + pos, "%!PS", 4) == 0);
    auto plain = Bytes("%!PS");
    CHECK_EQ(SkipPjlHeader(plain.data(), plain.size()), size_t(0));
}

TEST(XpsPageCount) {
    auto zip = MakeZip({"[Content_Types].xml", "FixedDocumentSequence.fdseq", "Documents/1/FixedDocument.fdoc",
                        "Documents/1/Pages/1.fpage", "Documents/1/Pages/2.fpage", "Documents/1/Pages/3.fpage",
                        "Documents/1/Resources/Fonts/font.odttf"});
    CHECK_EQ(CountXpsPages(zip.data(), zip.size()), 3u);
    CHECK(DetectFormat(zip.data(), zip.size()) == DataFormat::Xps);
    // Sin directorio central (spool todavía escribiéndose): usa los encabezados locales.
    auto partial = MakeZip({"Documents/1/Pages/1.fpage", "Documents/1/Pages/2.fpage"});
    partial.resize(partial.size() - 60);
    CHECK_EQ(CountXpsPages(partial.data(), partial.size()), 2u);
}

TEST(AdminShareConversion) {
    CHECK_EQ(ToAdminShare(L"\\\\SRV", L"C:\\Windows\\System32\\spool\\PRINTERS"),
             std::wstring(L"\\\\SRV\\C$\\Windows\\System32\\spool\\PRINTERS"));
    CHECK_EQ(ToAdminShare(L"", L"C:\\X"), std::wstring(L"C:\\X"));
    // La carpeta alternativa configurada se prueba primero.
    auto candidates = SpoolFileCandidates(SpoolSearch{L"", L"", L"D:\\spool\\"}, 42);
    CHECK(!candidates.empty());
    CHECK_EQ(candidates.front(), std::wstring(L"D:\\spool\\00042.SPL"));
    // En un servidor remoto las rutas pasan por los recursos administrativos.
    auto remote = SpoolDirectories(SpoolSearch{L"\\\\SRV-QUE-NO-EXISTE", L"", L""});
    CHECK(!remote.empty());
    CHECK(remote.front().rfind(L"\\\\SRV-QUE-NO-EXISTE\\", 0) == 0);
    // Este mismo equipo se alcanza con rutas locales (sin pasar por C$).
    CHECK(core::spooler::IsLocalServer(L""));
    CHECK(core::spooler::IsLocalServer(L"localhost"));
    CHECK(!core::spooler::IsLocalServer(L"\\\\SRV-QUE-NO-EXISTE"));
}

TEST(ShadowFileMatching) {
    // .SHD simulado: firma, tamaño, estado, id del trabajo y el nombre del documento en UTF-16.
    std::vector<std::uint8_t> shd;
    AppendU32(shd, 0x00005123);
    AppendU32(shd, 0x000000E8);
    AppendU32(shd, 0x00000000);
    AppendU32(shd, 0x00000000);
    AppendU32(shd, 42);  // id del trabajo
    shd.resize(128, 0);
    std::wstring doc = L"Informe final.docx";
    auto* raw = reinterpret_cast<const std::uint8_t*>(doc.c_str());
    shd.insert(shd.end(), raw, raw + doc.size() * sizeof(wchar_t));
    CHECK(ShadowMatchesJob(shd.data(), shd.size(), 42, doc));
    CHECK(ShadowMatchesJob(shd.data(), shd.size(), 42, L""));
    CHECK(!ShadowMatchesJob(shd.data(), shd.size(), 43, doc));
    CHECK(!ShadowMatchesJob(shd.data(), shd.size(), 42, L"Otro documento"));
    // Segunda pasada: solo por id (servidores que guardan el documento de otra forma).
    CHECK(ShadowMatchesJob(shd.data(), shd.size(), 42, L"Otro documento", false));
    CHECK(!ShadowMatchesJob(shd.data(), shd.size(), 43, L"Otro documento", false));
}

TEST(FitPageKeepsAspect) {
    RECT r = FitPage({21590, 27940}, RECT{0, 0, 1000, 1000});
    int w = r.right - r.left, h = r.bottom - r.top;
    CHECK(h == 1000);
    CHECK(w > 770 && w < 775);
    CHECK_EQ(r.left, (1000 - w) / 2);
}
