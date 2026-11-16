// Pruebas de la actualización automática (D58).
//
// Módulos probados:
//   - update/UpdateInfo: versiones, validación del manifiesto y direcciones
//     de descarga.
//   - util/Sha256 (CNG) con los vectores de la norma (FIPS 180-2).
//   - util/Inflate: bloques guardados, con Huffman fijo y dinámico, y datos
//     dañados. Los vectores los generó zlib (Python, wbits = -15).
//   - update/UpdatePackage: rutas seguras, archivos protegidos, lectura del
//     ZIP (uno de .NET/zipfile con DEFLATE y otro de util::ZipWriter) y la
//     instalación en una carpeta temporal: reemplazo, archivos idénticos,
//     protegidos, archivo en uso, deshacer ante un fallo y limpieza.
// No se prueba la descarga (HttpClient, UpdateService): necesita Internet.

#include <windows.h>

#include <string>
#include <vector>

#include "TestFramework.h"
#include "update/UpdateInfo.h"
#include "update/UpdatePackage.h"
#include "util/Inflate.h"
#include "util/Sha256.h"
#include "util/ZipWriter.h"

using namespace cola;

namespace {

// Carpeta temporal propia de cada prueba (se borra con RemoveDirTree).
std::wstring MakeTempDir(const wchar_t* tag) {
    wchar_t base[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, base);
    static int counter = 0;
    std::wstring dir = std::wstring(base) + L"cola_update_" + tag + L"_" + std::to_wstring(GetCurrentProcessId()) +
                       L"_" + std::to_wstring(++counter);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void RemoveDirTree(const std::wstring& dir) {
    WIN32_FIND_DATAW found{};
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = found.cFileName;
            if (name == L"." || name == L"..") continue;
            std::wstring path = dir + L"\\" + name;
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirTree(path);
            else {
                SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(path.c_str());
            }
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    RemoveDirectoryW(dir.c_str());
}

void WriteText(const std::wstring& path, const std::string& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        CloseHandle(file);
    }
}

// Contenido de un archivo, o "<no existe>".
std::string ReadText(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return "<no existe>";
    std::string out(1 << 16, '\0');
    DWORD read = 0;
    ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(file);
    out.resize(read);
    return out;
}

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// ZIP de prueba armado con zipfile de Python: "ColaDeImpresion.exe" con
// DEFLATE (contenido "MZ" + "exe nuevo " x 20) y "herramientas/leeme.txt"
// guardado ("hola").
const std::string kPythonZip(
    "\x50\x4b\x03\x04\x14\x00\x00\x00\x08\x00\x00\x00\x21\x00\x5a\xc4\xd7\xda\x10\x00\x00\x00\xca\x00\x00\x00\x13\x00"
    "\x00\x00\x43\x6f\x6c\x61\x44\x65\x49\x6d\x70\x72\x65\x73\x69\x6f\x6e\x2e\x65\x78\x65\xf3\x8d\x4a\xad\x48\x55\xc8"
    "\x2b\x4d\x2d\xcb\x57\x18\xda\x2c\x00\x50\x4b\x03\x04\x14\x00\x00\x00\x00\x00\x00\x00\x21\x00\x88\xf9\xa0\x6f\x04"
    "\x00\x00\x00\x04\x00\x00\x00\x16\x00\x00\x00\x68\x65\x72\x72\x61\x6d\x69\x65\x6e\x74\x61\x73\x2f\x6c\x65\x65\x6d"
    "\x65\x2e\x74\x78\x74\x68\x6f\x6c\x61\x50\x4b\x01\x02\x14\x00\x14\x00\x00\x00\x08\x00\x00\x00\x21\x00\x5a\xc4\xd7"
    "\xda\x10\x00\x00\x00\xca\x00\x00\x00\x13\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x01\x00\x00\x00\x00\x43"
    "\x6f\x6c\x61\x44\x65\x49\x6d\x70\x72\x65\x73\x69\x6f\x6e\x2e\x65\x78\x65\x50\x4b\x01\x02\x14\x00\x14\x00\x00\x00"
    "\x00\x00\x00\x00\x21\x00\x88\xf9\xa0\x6f\x04\x00\x00\x00\x04\x00\x00\x00\x16\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x80\x01\x41\x00\x00\x00\x68\x65\x72\x72\x61\x6d\x69\x65\x6e\x74\x61\x73\x2f\x6c\x65\x65\x6d\x65\x2e\x74"
    "\x78\x74\x50\x4b\x05\x06\x00\x00\x00\x00\x02\x00\x02\x00\x85\x00\x00\x00\x79\x00\x00\x00\x00\x00",
    276);

// Huffman dinámico (zlib nivel 9): 200 líneas "Linea N de la prueba de la
// cola de impresion del LAIMI.\n" con N de 0 a 199.
const std::string kDynamicDeflate(
    "\xa5\xda\x3f\x6e\xd4\x40\x18\x87\xe1\x9e\x53\xf8\x04\xc8\xbf\xf9\x3f\x25\x65\xa4\xe5\x10\x0b\x6c\xb1\xd2"
    "\x92\x44\x41\xdc\x3f\x28\xe2\x02\x3c\x74\x76\xf1\x55\x7e\x64\x8f\xdf\x99\xcb\xfd\xf9\x76\x3d\xce\xe3\xc7"
    "\xed\x78\x5c\x8f\xd7\xb7\xdf\xb7\x6f\xd7\xbf\x37\xdf\x5f\x1e\x1f\x97\xf7\x9f\xaf\x6f\xb7\x5f\xf7\x97\xe7"
    "\x3f\x37\x8f\xe3\xf2\xe5\xe9\xeb\xd3\xe7\x4f\x97\x8f\xb9\xe0\x5c\xc1\xb9\x8a\x73\x0d\xe7\x3a\xce\x0d\x9c"
    "\x9b\x38\xb7\x70\x6e\xeb\x73\x67\x30\x2a\x26\x4a\x26\x6a\x26\x8a\x26\xaa\x26\xca\x26\xea\x26\x0a\x27\x2a"
    "\xa7\xa8\x9c\xc2\xef\x1a\x95\x53\x54\x4e\x51\x39\x45\xe5\x14\x95\x53\x54\x4e\x51\x39\x45\xe5\x54\x95\x53"
    "\x55\x4e\xe5\xcf\x94\xca\xa9\x2a\xa7\xaa\x9c\xaa\x72\xaa\xca\xa9\x2a\xa7\xaa\x9c\xa6\x72\x9a\xca\x69\x2a"
    "\xa7\xf1\x0a\x47\xe5\x34\x95\xd3\x54\x4e\x53\x39\x4d\xe5\x34\x95\xd3\x55\x4e\x57\x39\x5d\xe5\x74\x95\xd3"
    "\x79\x71\xac\x72\xba\xca\xe9\x2a\xa7\xab\x9c\xae\x72\x86\xca\x19\x2a\x67\xa8\x9c\xa1\x72\x86\xca\x19\xfc"
    "\x5f\xa5\x72\x86\xca\x19\x2a\x67\xa8\x9c\xa9\x72\xa6\xca\x99\x2a\x67\xaa\x9c\xa9\x72\xa6\xca\x99\xfc\x4b"
    "\xae\x72\xa6\xca\x99\x2a\x67\xa9\x9c\xa5\x72\x96\xca\x59\x2a\x67\xa9\x9c\xa5\x72\x96\xca\x59\x5c\x73\x54"
    "\xce\x52\x39\x5b\xe5\x6c\x95\xb3\x55\xce\x56\x39\x5b\xe5\x6c\x95\xb3\x55\xce\x56\x39\x9b\x43\xa0\x97\x40"
    "\x4e\x81\x27\xb7\xc0\x93\x63\xe0\xc9\x35\xf0\xe4\x1c\x78\x72\x0f\x3c\x39\x08\x9e\x5c\x04\x4f\x4e\x82\x27"
    "\x1b\xfa\x8f\x9c\xcc\x86\x3c\x28\x7b\x51\xf6\xa4\xec\x4d\xd9\xa3\xb2\x57\x65\xcf\xca\xdc\x95\xc3\x61\x39"
    "\xc5\xf7\x24\xd8\x10\xb7\xe5\x70\x5c\x0e\xd7\xe5\x70\x5e\x0e\xf7\xe5\x70\x60\x0e\x17\xe6\x70\x62\x0e\x37"
    "\xe6\x54\xdf\xd8\x62\x43\x9c\x99\xc3\x9d\x39\x1c\x9a\xc3\xa5\x39\x9c\x9a\xc3\xad\x39\x1c\x9b\xc3\xb5\x39"
    "\x9c\x9b\xd3\x7c\x77\x94\x0d\x71\x71\x0e\x27\xe7\x70\x73\x0e\x47\xe7\x70\x75\x0e\x67\xe7\x70\x77\x0e\x87"
    "\xe7\x70\x79\x4e\xf7\x2d\x76\x36\xc4\xf1\x39\x5c\x9f\xc3\xf9\x39\xdc\x9f\xc3\x01\x3a\x5c\xa0\xc3\x09\x3a"
    "\xdc\xa0\xc3\x11\x3a\xc3\xcf\x69\xb0\x21\xee\xd0\xe1\x10\x1d\x2e\xd1\xe1\x14\x1d\x6e\xd1\xe1\x18\x1d\xae"
    "\xd1\xe1\x1c\x1d\xee\xd1\x99\x7e\xd8\x87\x0d\x71\x92\x0e\x37\xe9\x70\x94\x0e\x57\xe9\x70\x96\x0e\x77\xe9"
    "\x70\x98\x0e\x97\xe9\x70\x9a\xce\xf2\x13\x63\x6c\x88\xeb\x74\x38\x4f\x87\xfb\x74\x38\x50\x87\x0b\x75\x38"
    "\x51\x87\x1b\x75\x38\x52\x87\x2b\x75\xb6\x1f\x3b\xfc\x77\x43\xef",
    562);

std::string Repeat(const std::string& text, int times) {
    std::string out;
    for (int i = 0; i < times; ++i) out += text;
    return out;
}

bool InflateText(const std::string& compressed, size_t expected, std::string& out) {
    return util::Inflate(reinterpret_cast<const std::uint8_t*>(compressed.data()), compressed.size(), expected, out);
}

}  // namespace

// --- UpdateInfo -------------------------------------------------------------

TEST(UpdateVersionParseAndCompare) {
    update::Version v;
    CHECK(update::ParseVersion(L"0.5.19", v));
    CHECK(v == (update::Version{0, 5, 19}));
    CHECK(update::ParseVersion(L" v1.2 ", v));  // "v" y espacios; 1.2 = 1.2.0
    CHECK(v == (update::Version{1, 2, 0}));
    CHECK(update::ParseVersion(L"0.5.18.0", v));  // el 4.º número se ignora
    CHECK(v == (update::Version{0, 5, 18}));
    CHECK_EQ(v.ToString(), std::wstring(L"0.5.18"));
    for (const wchar_t* bad : {L"", L"v", L"1..2", L"1.2.", L"a.b.c", L"-1.0.0", L"1.2.3.4.5", L"1234567.0.0", L"1.2-beta"})
        CHECK(!update::ParseVersion(bad, v));
    // Se compara número a número (no como texto: 0.5.10 > 0.5.9).
    CHECK((update::Version{0, 5, 10}) > (update::Version{0, 5, 9}));
    CHECK((update::Version{1, 0, 0}) > (update::Version{0, 99, 99}));
    CHECK(!((update::Version{0, 5, 18}) > (update::Version{0, 5, 18})));
    // La versión de esta compilación es la de CMakeLists.txt.
    CHECK_EQ(update::CurrentVersion().ToString(), std::wstring(COLA_VERSION_STRING));
}

TEST(UpdateManifestValidation) {
    const std::string hash(64, 'a');
    update::Manifest m;
    std::wstring error;
    CHECK(update::ParseManifest("{\"version\":\"0.5.19\",\"file\":\"ColaDeImpresion-actualizacion.zip\",\"size\":1234,"
                                "\"sha256\":\"" + std::string(64, 'A') + "\",\"notes\":\"  Novedades  \"}",
                                m, &error));
    CHECK(m.version == (update::Version{0, 5, 19}));
    CHECK_EQ(m.file, std::wstring(L"ColaDeImpresion-actualizacion.zip"));
    CHECK_EQ(m.size, std::uint64_t(1234));
    CHECK_EQ(m.sha256, std::wstring(64, L'a'));  // en minúsculas
    CHECK_EQ(m.notes, std::wstring(L"Novedades"));
    // Rechazos: cada campo inválido por separado.
    auto manifest = [&](const std::string& version, const std::string& file, const std::string& size,
                        const std::string& sha) {
        return "{\"version\":\"" + version + "\",\"file\":\"" + file + "\",\"size\":" + size + ",\"sha256\":\"" + sha +
               "\"}";
    };
    CHECK(update::ParseManifest(manifest("1.0.0", "a.zip", "10", hash), m));
    CHECK(!update::ParseManifest(manifest("uno", "a.zip", "10", hash), m));
    CHECK(!update::ParseManifest(manifest("1.0.0", "../a.zip", "10", hash), m));
    CHECK(!update::ParseManifest(manifest("1.0.0", "https://x/a.zip", "10", hash), m));
    CHECK(!update::ParseManifest(manifest("1.0.0", "a.exe", "10", hash), m));
    CHECK(!update::ParseManifest(manifest("1.0.0", ".zip", "10", hash), m));
    CHECK(!update::ParseManifest(manifest("1.0.0", "a b.zip", "10", hash), m));
    CHECK(!update::ParseManifest(manifest("1.0.0", "a.zip", "0", hash), m));
    CHECK(!update::ParseManifest(manifest("1.0.0", "a.zip", "1.5", hash), m));
    CHECK(!update::ParseManifest(manifest("1.0.0", "a.zip", "999999999999", hash), m));  // > kMaxPackageBytes
    CHECK(!update::ParseManifest(manifest("1.0.0", "a.zip", "10", std::string(63, 'a')), m));
    CHECK(!update::ParseManifest(manifest("1.0.0", "a.zip", "10", std::string(64, 'g')), m));
    CHECK(!update::ParseManifest("no es json", m, &error));
    CHECK(!error.empty());
    CHECK(!update::ParseManifest("[1,2]", m));
}

TEST(UpdateUrls) {
    CHECK_EQ(update::ManifestUrl(),
             std::wstring(L"https://github.com/ChrisRC7/ColaDeImpresion/releases/latest/download/actualizacion.json"));
    update::Manifest m;
    m.version = {0, 5, 19};
    m.file = L"ColaDeImpresion-actualizacion.zip";
    // El paquete se pide por la etiqueta de la versión, no por "latest".
    CHECK_EQ(update::PackageUrl(m),
             std::wstring(
                 L"https://github.com/ChrisRC7/ColaDeImpresion/releases/download/v0.5.19/ColaDeImpresion-actualizacion.zip"));
}

// --- Sha256 / Inflate --------------------------------------------------------

TEST(Sha256KnownVectors) {
    CHECK_EQ(util::Sha256Hex(""), std::wstring(L"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(util::Sha256Hex("abc"), std::wstring(L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(util::Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::wstring(L"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

TEST(InflateBlocks) {
    const std::string text = "hola hola hola hola, cola de impresion";
    // Huffman fijo (zlib con Z_FIXED).
    const std::string fixed("\xcb\xc8\xcf\x49\x54\xc8\x40\x21\x74\x14\x92\x41\xcc\x94\x54\x85\xcc\xdc\x82\xa2\xd4\xe2"
                            "\xcc\xfc\x3c\x00",
                            26);
    // Bloque guardado (nivel 0): cabecera + LEN/NLEN + el texto tal cual.
    const std::string stored = std::string("\x01\x26\x00\xd9\xff", 5) + text;
    std::string out;
    CHECK(InflateText(fixed, text.size(), out));
    CHECK_EQ(out, text);
    CHECK(InflateText(stored, text.size(), out));
    CHECK_EQ(out, text);
    // Huffman dinámico con referencias hacia atrás.
    std::string lines;
    for (int i = 0; i < 200; ++i) lines += "Linea " + std::to_string(i) + " de la prueba de la cola de impresion del LAIMI.\n";
    CHECK(InflateText(kDynamicDeflate, lines.size(), out));
    CHECK(out == lines);
    CHECK(!InflateText(kDynamicDeflate.substr(0, kDynamicDeflate.size() / 2), lines.size(), out));
    // Entrada DEFLATE (Huffman fijo) del ZIP de prueba.
    std::vector<update::PackageEntry> entries;
    CHECK(update::ReadPackage(kPythonZip, entries));
    if (!entries.empty()) {
        CHECK_EQ(entries[0].method, std::uint16_t(8));
        CHECK(update::ExtractEntry(kPythonZip, entries[0], out));
        CHECK_EQ(out, "MZ" + Repeat("exe nuevo ", 20));
    }
    // Rechazos: tamaño esperado distinto, datos cortados y tipo de bloque 3.
    CHECK(!InflateText(fixed, text.size() + 1, out));
    CHECK(!InflateText(fixed, text.size() - 1, out));
    CHECK(!InflateText(fixed.substr(0, 10), text.size(), out));
    CHECK(!InflateText(stored.substr(0, 20), text.size(), out));
    CHECK(!InflateText(std::string("\x07\x00", 2), 4, out));
    CHECK(!InflateText(std::string("\x01\x26\x00\x00\x00", 5) + text, text.size(), out));  // NLEN incorrecto
}

// --- UpdatePackage -----------------------------------------------------------

TEST(UpdatePackagePathRules) {
    for (const wchar_t* ok : {L"ColaDeImpresion.exe", L"herramientas/cola_testjob.exe", L"a\\b\\c.txt", L"LEEME - PRUEBA.txt"})
        CHECK(update::IsSafeRelativePath(ok));
    for (const wchar_t* bad : {L"", L"/abs.txt", L"\\abs.txt", L"C:\\x.exe", L"C:x.exe", L"..\\x.exe", L"a/../../x",
                               L"a//b", L"./a", L"a/", L"con", L"NUL.txt", L"a/com1.log", L"x.", L"x ", L"a?b",
                               L"a*b", L"a|b", L"\\\\srv\\share\\x"})
        CHECK(!update::IsSafeRelativePath(bad));
    CHECK(!update::IsSafeRelativePath(std::wstring(200, L'a')));
    // Archivos propios de cada copia o usuario: nunca se tocan.
    for (const wchar_t* p : {L"config.json", L"Historial.json", L"app.log", L"app.log.1", L"portable.txt",
                             L"servidor-laimi.txt", L"sub/config.json", L"config.json.danado", L"historial.json.danado"})
        CHECK(update::IsProtectedFile(p));
    for (const wchar_t* p : {L"ColaDeImpresion.exe", L"README.md", L"configuracion.json", L"applog.txt"})
        CHECK(!update::IsProtectedFile(p));
}

TEST(UpdatePackageRead) {
    std::vector<update::PackageEntry> entries;
    std::wstring error;
    CHECK(update::ReadPackage(kPythonZip, entries, &error));
    CHECK_EQ(entries.size(), size_t(2));
    if (entries.size() == 2) {
        CHECK_EQ(entries[1].path, std::wstring(L"herramientas\\leeme.txt"));  // con "\"
        std::string out;
        CHECK(update::ExtractEntry(kPythonZip, entries[1], out));
        CHECK_EQ(out, std::string("hola"));
        // CRC alterado: la extracción lo detecta.
        update::PackageEntry broken = entries[1];
        broken.crc ^= 1;
        CHECK(!update::ExtractEntry(kPythonZip, broken, out));
    }
    // Un ZIP de util::ZipWriter (sin comprimir) también vale; las carpetas se omiten.
    util::ZipWriter zip;
    zip.Add("carpeta/", "");
    zip.Add("ColaDeImpresion.exe", "MZ");
    CHECK(update::ReadPackage(zip.Finish(), entries));
    CHECK_EQ(entries.size(), size_t(1));
    // Rechazos: rutas que salen de la carpeta, repetidas, vacío, basura y cortado.
    auto rejects = [&](std::initializer_list<const char*> names) {
        util::ZipWriter z;
        for (const char* n : names) z.Add(n, "x");
        return !update::ReadPackage(z.Finish(), entries, &error) && !error.empty();
    };
    CHECK(rejects({"../fuera.exe"}));
    CHECK(rejects({"C:/Windows/x.dll"}));
    CHECK(rejects({"a.txt", "A.TXT"}));
    CHECK(rejects({"a/nul.txt"}));
    CHECK(!update::ReadPackage(util::ZipWriter().Finish(), entries));
    CHECK(!update::ReadPackage("esto no es un zip, aunque sea largo de sobra", entries));
    CHECK(!update::ReadPackage(kPythonZip.substr(0, 60) + kPythonZip.substr(kPythonZip.size() - 22), entries));
}

TEST(UpdatePackageInstall) {
    const std::wstring dir = MakeTempDir(L"install");
    CreateDirectoryW((dir + L"\\herramientas").c_str(), nullptr);
    WriteText(dir + L"\\ColaDeImpresion.exe", "exe viejo");
    WriteText(dir + L"\\README.md", "igual");
    WriteText(dir + L"\\config.json", "{\"mio\":true}");
    WriteText(dir + L"\\herramientas\\cola_testjob.exe", "herramienta vieja");

    util::ZipWriter zip;
    zip.Add("ColaDeImpresion.exe", "exe nuevo");
    zip.Add("README.md", "igual");                       // idéntico: no se toca
    zip.Add("config.json", "{\"pisado\":true}");         // protegido: se ignora
    zip.Add("herramientas/cola_testjob.exe", "herramienta nueva");
    zip.Add("docs/nuevo.md", "archivo nuevo en carpeta nueva");

    update::InstallResult result = update::InstallPackage(zip.Finish(), dir);
    CHECK(result.ok);
    CHECK_EQ(result.replaced, 3);
    CHECK_EQ(result.unchanged, 1);
    CHECK_EQ(ReadText(dir + L"\\ColaDeImpresion.exe"), std::string("exe nuevo"));
    CHECK_EQ(ReadText(dir + L"\\README.md"), std::string("igual"));
    CHECK_EQ(ReadText(dir + L"\\config.json"), std::string("{\"mio\":true}"));
    CHECK_EQ(ReadText(dir + L"\\herramientas\\cola_testjob.exe"), std::string("herramienta nueva"));
    CHECK_EQ(ReadText(dir + L"\\docs\\nuevo.md"), std::string("archivo nuevo en carpeta nueva"));
    // Los archivos viejos que nadie usa se borran enseguida (el .exe en
    // ejecución quedaría como .cola-old hasta CleanupOldFiles).
    CHECK(!Exists(dir + L"\\ColaDeImpresion.exe.cola-old"));
    CHECK(!Exists(dir + L"\\herramientas\\cola_testjob.exe.cola-old"));
    CHECK(!Exists(dir + L"\\" + update::kStagingDir));
    CHECK(!Exists(dir + L"\\" + update::kLockFile));

    // Instalar otra vez lo mismo no cambia nada.
    result = update::InstallPackage(zip.Finish(), dir);
    CHECK(result.ok);
    CHECK_EQ(result.replaced, 0);
    CHECK_EQ(result.unchanged, 4);
    RemoveDirTree(dir);
}

TEST(UpdatePackageInstallRejectsAndRollsBack) {
    const std::wstring dir = MakeTempDir(L"rollback");
    WriteText(dir + L"\\ColaDeImpresion.exe", "exe viejo");
    WriteText(dir + L"\\b.dll", "b viejo");

    // Sin ColaDeImpresion.exe no es un paquete de esta app: no se toca nada.
    util::ZipWriter other;
    other.Add("b.dll", "b nuevo");
    update::InstallResult result = update::InstallPackage(other.Finish(), dir);
    CHECK(!result.ok);
    CHECK(!result.error.empty());
    CHECK_EQ(ReadText(dir + L"\\b.dll"), std::string("b viejo"));

    // b.dll abierto SIN permitir renombrarlo: el reemplazo falla después de
    // haber puesto el .exe nuevo, y todo vuelve a como estaba.
    HANDLE locked = CreateFileW((dir + L"\\b.dll").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                                nullptr);
    CHECK(locked != INVALID_HANDLE_VALUE);
    util::ZipWriter zip;
    zip.Add("ColaDeImpresion.exe", "exe nuevo");
    zip.Add("b.dll", "b nuevo");
    result = update::InstallPackage(zip.Finish(), dir);
    CHECK(!result.ok);
    CHECK(!result.error.empty());
    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
    CHECK_EQ(ReadText(dir + L"\\ColaDeImpresion.exe"), std::string("exe viejo"));
    CHECK_EQ(ReadText(dir + L"\\b.dll"), std::string("b viejo"));
    CHECK(!Exists(dir + L"\\ColaDeImpresion.exe.cola-old"));
    CHECK(!Exists(dir + L"\\" + update::kStagingDir));

    // Restos de una instalación interrumpida: los borra CleanupOldFiles.
    CreateDirectoryW((dir + L"\\" + update::kStagingDir).c_str(), nullptr);
    WriteText(dir + L"\\" + update::kStagingDir + L"\\x.exe", "a medias");
    WriteText(dir + L"\\viejo.exe.cola-old", "apartado");
    update::CleanupOldFiles(dir);
    CHECK(!Exists(dir + L"\\" + update::kStagingDir));
    CHECK(!Exists(dir + L"\\viejo.exe.cola-old"));
    CHECK(update::CanWriteTo(dir));
    RemoveDirTree(dir);
}
