// Interpretación de las opciones declaradas dentro de los datos RAW (PJL, EJL,
// DSC de PostScript) y conteo de páginas ESC/Page. Ver PdlSettings.h.
//
// Ejemplos de lo que se interpreta:
//   PJL : ESC%-12345X@PJL SET COPIES=2 / @PJL SET DUPLEX=ON /
//         @PJL SET BINDING=LONGEDGE / @PJL ENTER LANGUAGE=PCLXL
//   EJL : ESC 01 @EJL SET PS=A4 DX=ON BD=LE QT=3 ... / @EJL JI DRIVER="..." /
//         @EJL EN LA=ESC/PAGE-COLOR (y al final @EJL JI PAGES="2")
//   DSC : %%Orientation: Portrait / %%BeginFeature: *PageSize A4 / %%Pages: 3
//   ESC/Page: GS <n>[;<m>...] <minúsculas>[{] <mayúscula>, FF = fin de página.
#include "preview/PdlSettings.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "preview/SpoolFile.h"
#include "util/Strings.h"

namespace cola::preview {

namespace {

// Bytes del principio de los datos donde se buscan los comandos PJL/EJL
// (y la línea que entra a ESC/Page): los encabezados nunca son más largos.
constexpr size_t kHeaderWindow = 64 * 1024;
// Bytes del final de los datos donde se buscan los comandos de cierre
// (p. ej. "@EJL JI PAGES=" que escribe el controlador Epson al terminar).
constexpr size_t kTrailerWindow = 16 * 1024;

// Copia en mayúsculas ASCII (las claves PJL/EJL no distinguen mayúsculas).
std::string Upper(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

// Quita espacios, tabuladores, CR y comillas de ambos extremos
// ("  \"A4\"\r" -> "A4"): los valores PJL/EJL pueden ir entre comillas.
std::string TrimAscii(const std::string& text) {
    size_t a = text.find_first_not_of(" \t\r\"");
    if (a == std::string::npos) return "";
    size_t b = text.find_last_not_of(" \t\r\"");
    return text.substr(a, b - a + 1);
}

// Líneas "@PJL ..." / "@EJL ..." de una ventana de los datos, en orden.
// stopAtEnter = no seguir después de la línea que entra al lenguaje de página
// (lo que sigue es el documento, no opciones).
// Cada línea se devuelve en mayúsculas, sin el CR final, empezando en "@PJL"
// o "@EJL" (lo que haya antes en la misma línea, como un UEL o un ESC 01, se
// descarta). Varias órdenes EJL pueden ir en la misma línea física separadas
// por ESC 01; aquí se toma cada "@EJL" encontrado hasta el siguiente LF.
std::vector<std::string> CommandLines(const std::uint8_t* data, size_t size, bool stopAtEnter) {
    std::vector<std::string> lines;
    std::string window(reinterpret_cast<const char*>(data), size);
    size_t pos = 0;
    while (pos < window.size()) {
        // El siguiente comando, sea PJL o EJL (npos es el mayor size_t, así
        // que min elige el que aparezca primero).
        size_t pjl = window.find("@PJL", pos);
        size_t ejl = window.find("@EJL", pos);
        size_t at = std::min(pjl, ejl);
        if (at == std::string::npos) break;
        size_t end = window.find('\n', at);
        if (end == std::string::npos) end = window.size();
        std::string line = Upper(window.substr(at, end - at));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        pos = end;
        if (stopAtEnter && (line.find("ENTER LANGUAGE") != std::string::npos || line.rfind("@EJL EN ", 0) == 0))
            break;
    }
    return lines;
}

// "A4" -> DMPAPER_A4, etc. (nombres PJL y abreviaturas EJL observadas).
// name en mayúsculas. Devuelve 0 si el nombre no se conoce (entonces solo se
// muestra el nombre, sin medidas).
short PaperCode(const std::string& name) {
    struct Entry {
        const char* name;
        short code;
    };
    static const Entry kNames[] = {
        {"LETTER", DMPAPER_LETTER}, {"LT", DMPAPER_LETTER},       {"LEGAL", DMPAPER_LEGAL},
        {"LG", DMPAPER_LEGAL},      {"A4", DMPAPER_A4},           {"A3", DMPAPER_A3},
        {"A5", DMPAPER_A5},         {"A6", DMPAPER_A6},           {"EXECUTIVE", DMPAPER_EXECUTIVE},
        {"EX", DMPAPER_EXECUTIVE},  {"LEDGER", DMPAPER_LEDGER},   {"B4", DMPAPER_B4},
        {"JISB4", DMPAPER_B4},      {"B5", DMPAPER_B5},           {"JISB5", DMPAPER_B5},
        {"COM10", DMPAPER_ENV_10},  {"DL", DMPAPER_ENV_DL},       {"C5", DMPAPER_ENV_C5},
        {"MONARCH", DMPAPER_ENV_MONARCH},
    };
    for (const auto& e : kNames)
        if (name == e.name) return e.code;
    return 0;
}

// Guarda en facts el papel declarado: nombre tal como viene y, si se conoce,
// su ancho/alto en décimas de milímetro (unidades de DEVMODE) desde la tabla
// de papeles de core.
void SetPaper(const std::string& name, core::ContentFacts& facts) {
    if (name.empty()) return;
    if (name == "ARCHB") {
        // 12 x 18 pulgadas (ANSI Arch B): así lo escribe el controlador Epson AM-C6000.
        // 12" = 304,8 mm y 18" = 457,2 mm (en décimas de mm: 3048 x 4572).
        facts.paperName.Set(L"ARCHB");
        facts.paperWidth.Set(3048);
        facts.paperLength.Set(4572);
        return;
    }
    facts.paperName.Set(util::FromUtf8(name));
    if (const core::PaperInfo* p = core::FindPaper(PaperCode(name))) {
        facts.paperWidth.Set(p->width);
        facts.paperLength.Set(p->length);
    }
}

// Agrega una opción al texto crudo que se muestra en el panel (separado por
// espacios), para que el operador vea exactamente lo que declararon los datos.
void AppendRaw(core::ContentFacts& facts, const std::string& text) {
    if (!facts.rawSettings.empty()) facts.rawSettings += L" ";
    facts.rawSettings += util::FromUtf8(text);
}

// Interpreta las líneas "@PJL SET CLAVE=VALOR" del encabezado (sintaxis
// documentada por HP). Las demás órdenes PJL (JOB, COMMENT, ENTER...) se
// ignoran. Si hay al menos un SET, la fuente de las opciones pasa a ser PJL
// y se considera documentada (D25).
void ParsePjl(const std::vector<std::string>& lines, core::ContentFacts& facts) {
    std::uint32_t copies = 0, qty = 0;  // 0 = no declarado
    std::string duplex, binding;        // se combinan al final (DUPLEX + BINDING)
    bool any = false;                   // hubo al menos un SET aceptado
    for (const auto& line : lines) {
        if (line.rfind("@PJL SET ", 0) != 0) continue;
        // "@PJL SET " ocupa 9 caracteres; lo que sigue es "CLAVE = VALOR".
        std::string rest = line.substr(9);
        size_t eq = rest.find('=');
        if (eq == std::string::npos) continue;
        std::string key = TrimAscii(rest.substr(0, eq));
        std::string value = TrimAscii(rest.substr(eq + 1));
        if (key.find(':') != std::string::npos) continue;  // LPARM:PCL ... (específicas del lenguaje)
        any = true;
        AppendRaw(facts, key + "=" + value);
        if (key == "COPIES") copies = static_cast<std::uint32_t>(std::max(0, atoi(value.c_str())));
        else if (key == "QTY") qty = static_cast<std::uint32_t>(std::max(0, atoi(value.c_str())));
        else if (key == "DUPLEX") duplex = value;
        else if (key == "BINDING") binding = value;
        else if (key == "PAPER") SetPaper(value, facts);
        else if (key == "ORIENTATION") {
            if (value == "PORTRAIT") facts.orientation.Set(DMORIENT_PORTRAIT);
            else if (value == "LANDSCAPE") facts.orientation.Set(DMORIENT_LANDSCAPE);
        } else if (key == "RENDERMODE") {
            if (value == "COLOR") facts.color.Set(DMCOLOR_COLOR);
            else if (value == "GRAYSCALE") facts.color.Set(DMCOLOR_MONOCHROME);
        } else if (key == "PLANESINUSE" && value == "1") {
            // Un solo plano de color = impresión en escala de grises.
            facts.color.Set(DMCOLOR_MONOCHROME);
        }
    }
    if (!any) return;
    facts.settingsSource = core::ContentFacts::SettingsSource::Pjl;
    facts.settingsDocumented = true;
    // QTY = copias intercaladas del trabajo; COPIES = copias sin intercalar de cada página.
    // Si vienen ambas, la impresora hace QTY juegos con COPIES copias de cada
    // página: el total es el producto. Intercalado: QTY > 1 implica juegos
    // completos; solo COPIES > 1 implica 1,1,2,2,...
    if (copies > 0 || qty > 0) {
        facts.copies.Set(std::max(copies, 1u) * std::max(qty, 1u));
        if (qty > 1) facts.collate.Set(DMCOLLATE_TRUE);
        else if (copies > 1) facts.collate.Set(DMCOLLATE_FALSE);
    }
    // DUPLEX=ON sin BINDING se toma como borde largo (el valor predeterminado
    // de PJL). DMDUP_VERTICAL = borde largo, DMDUP_HORIZONTAL = borde corto.
    if (duplex == "OFF") facts.duplex.Set(DMDUP_SIMPLEX);
    else if (duplex == "ON") facts.duplex.Set(binding == "SHORTEDGE" ? DMDUP_HORIZONTAL : DMDUP_VERTICAL);
}

// Controladores con los que se verificó el significado de las claves EJL
// (generando sus datos "a archivo" con distintas opciones, 2026-10-02):
//   QT = copias (CO no cambia), PS = papel (LT/A4/LGL), DX/BD = doble cara y
//   borde, "JI PAGES" = hojas físicas (ya incluye copias y doble cara), y en
//   ESC/Page "GS n poE" = orientación (0 vertical, 1 horizontal), "GS n;m iuE" =
//   bandeja, y PS=ARCHB = 12 x 18 pulgadas.
// driverUpper: valor de DRIVER="..." de "@EJL JI", en mayúsculas. Ver D29.
bool VerifiedEpsonDriver(const std::string& driverUpper) {
    return driverUpper.find("EPSON AM-C6000") != std::string::npos;
}

// Primer parámetro de un comando ESC/Page "GS <n>[;<m>...] <cmd>" al comienzo
// del documento (-1 si no está).
// command es el nombre del comando, p. ej. "poE" (orientación) o "iuE"
// (bandeja). Se busca su primera aparición en los primeros 64 KB, a partir
// de "LA=ESC/PAGE" para no confundirse con texto del encabezado EJL.
int EscPageParam(const std::uint8_t* data, size_t size, const char* command) {
    std::string_view text(reinterpret_cast<const char*>(data), std::min<size_t>(size, 64 * 1024));
    size_t lang = text.find("LA=ESC/PAGE");
    size_t at = text.find(command, lang == std::string_view::npos ? 0 : lang);
    if (at == std::string_view::npos || at < 2) return -1;
    // Retrocede sobre los parámetros (dígitos y ';') que preceden al nombre.
    size_t start = at;
    while (start > 0 && ((text[start - 1] >= '0' && text[start - 1] <= '9') || text[start - 1] == ';')) --start;
    // Debe haber al menos un parámetro y, justo antes, el byte GS (0x1D); si
    // no, la coincidencia era casual (p. ej. dentro de datos de imagen).
    if (start == at || start == 0 || text[start - 1] != 0x1d) return -1;
    // atoi se detiene en el primer ';': devuelve solo el primer parámetro.
    return atoi(std::string(text.substr(start, at - start)).c_str());
}

// Bandeja de "GS n;m iuE" como el código DMBIN que usa el controlador AM-C6000
// (verificado imprimiendo a archivo con cada bandeja): 0 = Auto selección (7),
// 1 = Bandeja de papel 1 (258), 2 = Bandeja de papel 2 (259), 15 = Alimentador de papel (261).
// Los códigos >= 256 son propios del controlador (DMBIN_USER en adelante);
// los nombres que ve el personal se resuelven en core (D34). 0 = desconocida.
short EpsonBin(int unit) {
    switch (unit) {
        case 0: return DMBIN_AUTO;
        case 1: return 258;
        case 2: return 259;
        case 15: return 261;
        default: return 0;
    }
}

// Interpreta un trabajo con encabezado EJL (Epson).
//   data/size : datos del trabajo (para buscar comandos ESC/Page y contar páginas).
//   header    : líneas EJL del principio (CommandLines con stopAtEnter).
//   trailer   : líneas EJL del final (vacío si no se tienen los datos completos).
//   allData   : conteo ESC/Page hecho sobre todos los datos, o nullptr.
// Pasos: 1) leer DRIVER y las claves SET; 2) si hubo SET, fijar la fuente
// EJL y aplicar las opciones; 3) si es ESC/Page, contar páginas; 4) leer las
// hojas físicas del EJL final.
void ParseEjl(const std::uint8_t* data, size_t size, const std::vector<std::string>& header,
              const std::vector<std::string>& trailer, core::ContentFacts& facts, const EscPageCount* allData) {
    std::string dx, bd, driver;  // doble cara (ON/OFF), borde (LE/SE) y controlador
    std::uint32_t quantity = 0;  // QT = copias (verificado con AM-C6000)
    bool any = false;            // hubo al menos una línea "@EJL SET"
    for (const auto& line : header) {
        // "@EJL JI ID=... DRIVER="EPSON AM-C6000 Series" ..." (Job Information).
        if (line.rfind("@EJL JI ", 0) == 0) {
            size_t at = line.find("DRIVER=\"");
            if (at != std::string::npos) driver = line.substr(at + 8, line.find('"', at + 8) - at - 8);
        }
        if (line.rfind("@EJL SET ", 0) != 0) continue;
        any = true;
        // "@EJL SET " (9 caracteres) seguido de pares CLAVE=VALOR separados
        // por espacios; la línea completa se muestra cruda en el panel.
        std::string rest = line.substr(9);
        AppendRaw(facts, TrimAscii(rest));
        size_t pos = 0;
        while (pos < rest.size()) {
            size_t end = rest.find(' ', pos);
            if (end == std::string::npos) end = rest.size();
            std::string token = rest.substr(pos, end - pos);
            pos = end + 1;
            size_t eq = token.find('=');
            if (eq == std::string::npos) continue;
            std::string key = token.substr(0, eq), value = TrimAscii(token.substr(eq + 1));
            // PS=LGL (Epson) es el mismo papel que "LG" de la tabla (Legal).
            if (key == "PS") SetPaper(value == "LGL" ? std::string("LG") : value, facts);
            else if (key == "DX") dx = value;
            else if (key == "BD") bd = value;
            else if (key == "QT") quantity = static_cast<std::uint32_t>(std::max(0, atoi(value.c_str())));
        }
    }
    if (any) {
        facts.settingsSource = core::ContentFacts::SettingsSource::Ejl;
        // La sintaxis EJL no tiene documentación pública: solo se considera
        // confirmada con un controlador cuyo comportamiento se verificó.
        facts.settingsDocumented = VerifiedEpsonDriver(driver);
        if (facts.settingsDocumented) facts.verifiedDriver = util::FromUtf8(driver);
        // BD=SE (short edge) = borde corto; cualquier otro valor con DX=ON se
        // toma como borde largo.
        if (dx == "OFF") facts.duplex.Set(DMDUP_SIMPLEX);
        else if (dx == "ON") facts.duplex.Set(bd == "SE" ? DMDUP_HORIZONTAL : DMDUP_VERTICAL);
        if (quantity > 0) facts.copies.Set(quantity);
        // Orientación y bandeja no van en el EJL sino en los comandos
        // ESC/Page del principio del documento: "GS n poE" y "GS n;m iuE".
        int orientation = EscPageParam(data, size, "poE");
        if (short bin = EpsonBin(EscPageParam(data, size, "iuE")); bin != 0) facts.sourceBin.Set(bin);
        if (orientation == 0) facts.orientation.Set(DMORIENT_PORTRAIT);
        else if (orientation == 1) facts.orientation.Set(DMORIENT_LANDSCAPE);
    }
    // Páginas: a doble cara (la configuración de las colas del LAIMI) el
    // JOB_INFO_2.TotalPages del servidor cuenta el dorso en blanco que completa
    // la última hoja (1 página => 2, 3 => 4), así que se cuentan en los datos.
    const bool escPage = std::any_of(header.begin(), header.end(), [](const std::string& l) {
        return l.rfind("@EJL EN LA=ESC/PAGE", 0) == 0;
    });
    if (escPage) {
        // Con los datos cortados por tamaño, el conteo hecho sobre todos al leerlos.
        const EscPageCount count = allData ? *allData : CountEscPagePages(data, size);
        std::uint32_t pages = count.pages;
        // A doble cara el controlador completa la última hoja con un FF de
        // una página vacía: si el total es par y la última está vacía, se
        // descuenta (3 páginas del documento => 4 FF => 3).
        if (dx == "ON" && count.lastPageBlank && pages >= 2 && pages % 2 == 0) --pages;  // dorso de relleno
        // Solo se confía en el conteo si se llegó al EJL del final (D46).
        if (count.end == EscPageCount::End::Complete && pages > 0) {
            facts.pageSource = core::ContentFacts::PageSource::EscPageFormFeeds;
            facts.pages = pages;
            facts.pagesFromAllData = allData != nullptr;
        }
        // Datos cortados por tamaño (no inválidos): TotalPages puede traer ese dorso.
        facts.spoolerCountsBlankBack = VerifiedEpsonDriver(driver) && count.end == EscPageCount::End::Truncated;
    }
    // "@EJL JI PAGES="2"" al final: hojas físicas que calculó el controlador
    // (1 pág. x 10 copias = 10; 2 págs. a doble cara = 1).
    // Si hay varias líneas así, la última manda.
    for (const auto& line : trailer) {
        size_t at = line.find("PAGES=");
        if (line.rfind("@EJL JI", 0) != 0 || at == std::string::npos) continue;
        int sheets = atoi(TrimAscii(line.substr(at + 6)).c_str());
        if (sheets > 0) facts.sheetsCounter.Set(static_cast<std::uint32_t>(sheets));
    }
}

// Valor de un comentario DSC: "%%Pages: 2" -> "2".
// prefix = largo de la parte que se descarta (p. ej. 14 para "%%Orientation:").
std::string DscValue(const std::string& line, size_t prefix) { return TrimAscii(line.substr(prefix)); }

// Comentarios DSC (Adobe Document Structuring Conventions 3.0) y funciones PPD
// que escriben los controladores PostScript (PSCRIPT5) y las aplicaciones que
// generan PostScript (Acrobat): sintaxis documentada.
// data apunta al "%!" (sin PJL ni Ctrl-D delante). complete: se tienen todos
// los datos (necesario para un "%%Pages: (atend)", cuyo valor está al final).
void ParseDsc(const std::uint8_t* data, size_t size, bool complete, core::ContentFacts& facts) {
    // Las opciones van en el prólogo / setup, antes de la primera página ("%%Page:").
    // Se limita a 8 MB por si el prólogo incrusta fuentes grandes.
    const size_t window = std::min<size_t>(size, 8 * 1024 * 1024);
    std::string text(reinterpret_cast<const char*>(data), window);
    size_t firstPage = text.find("\n%%Page: ");
    if (firstPage != std::string::npos) text.resize(firstPage);
    bool any = false;
    std::string duplex;
    size_t pos = 0;
    // Recorre cada comentario "%%..." hasta el fin de su línea.
    while ((pos = text.find("%%", pos)) != std::string::npos) {
        size_t end = text.find_first_of("\r\n", pos);
        std::string line = text.substr(pos, (end == std::string::npos ? text.size() : end) - pos);
        pos = end == std::string::npos ? text.size() : end;
        // Valor de una función PPD en esta línea:
        //   "%%BeginFeature: *PageSize A4"         -> "A4"
        //   "%%BeginNonPPDFeature: NumCopies 2"     -> "2"
        // "" si la línea no es esa función.
        auto feature = [&](const char* name) -> std::string {
            for (const char* prefix : {"%%BeginFeature: *", "%%BeginNonPPDFeature: "}) {
                std::string key = std::string(prefix) + name + " ";
                if (line.rfind(key, 0) == 0) return DscValue(line, key.size());
            }
            return "";
        };
        bool matched = true;
        if (line.rfind("%%Orientation:", 0) == 0) {
            std::string v = DscValue(line, 14);
            if (v == "Portrait") facts.orientation.Set(DMORIENT_PORTRAIT);
            else if (v == "Landscape") facts.orientation.Set(DMORIENT_LANDSCAPE);
        } else if (std::string v = feature("PageSize"); !v.empty()) {
            SetPaper(Upper(v), facts);
            // (SetPaper siempre fija el nombre; esto es solo una garantía.)
            if (!facts.paperName.valid) facts.paperName.Set(util::FromUtf8(v));
        } else if (std::string c = feature("NumCopies"); !c.empty()) {
            int copies = atoi(c.c_str());
            if (copies > 0) facts.copies.Set(static_cast<std::uint32_t>(copies));
        } else if (std::string d = feature("Duplex"); !d.empty()) {
            duplex = d;
        } else if (std::string k = feature("Collate"); !k.empty()) {
            facts.collate.Set(k == "True" ? DMCOLLATE_TRUE : DMCOLLATE_FALSE);
        } else {
            matched = false;
        }
        if (matched) {
            any = true;
            AppendRaw(facts, line.substr(2));
        }
    }
    // Valores PPD estándar de *Duplex: None, DuplexNoTumble (borde largo) y
    // DuplexTumble (borde corto).
    if (duplex == "None") facts.duplex.Set(DMDUP_SIMPLEX);
    else if (duplex == "DuplexNoTumble") facts.duplex.Set(DMDUP_VERTICAL);
    else if (duplex == "DuplexTumble") facts.duplex.Set(DMDUP_HORIZONTAL);

    // "%%Pages: N" (en el encabezado o, con "(atend)", en el final).
    std::string_view all(reinterpret_cast<const char*>(data), size);
    std::uint32_t pages = 0;
    for (size_t at = all.find("%%Pages:"); at != std::string::npos; at = all.find("%%Pages:", at + 8)) {
        int value = atoi(std::string(all.substr(at + 8, 12)).c_str());
        if (value > 0) pages = static_cast<std::uint32_t>(value);  // el último válido (el del final manda)
    }
    // Con "(atend)" el número real está al final: solo vale si se leyó todo
    // (si no, el valor encontrado podría ser de un documento incrustado).
    if (pages > 0 && (complete || all.find("(atend)") == std::string::npos)) {
        facts.pageSource = core::ContentFacts::PageSource::PostScriptDsc;
        facts.pages = pages;
    }
    if (any || pages) {
        facts.settingsSource = core::ContentFacts::SettingsSource::PostScriptDsc;
        facts.settingsDocumented = true;
    }
}

}  // namespace

EscPageCount CountEscPagePages(const std::uint8_t* data, size_t size) {
    EscPageCounter counter;
    if (data) counter.Feed(data, size);
    return counter.Result();
}

// Fase 1 (header_): acumula en head_ hasta ver la línea completa
// "@EJL EN LA=ESC/PAGE...\n"; lo que sigue a esa línea ya es cuerpo y pasa a
// FeedBody junto con el resto del bloque. Fase 2: todo va directo a FeedBody.
void EscPageCounter::Feed(const std::uint8_t* data, size_t size) {
    fed_ += size;
    if (done_) {
        // Terminado: solo interesa el EJL de cierre para el trailer.
        if (count_.end == EscPageCount::End::Complete) KeepTrailer(data, size);
        return;
    }
    if (!header_) {
        FeedBody(data, size);
        return;
    }
    // Los comandos empiezan después de la línea "@EJL EN LA=ESC/PAGE...", que
    // tiene que estar en los primeros kHeaderWindow bytes.
    // Se aceptan 1 KB extra para que esa línea pueda terminar después del
    // límite. 'take' = lo que de este bloque entra en head_.
    constexpr size_t kMaxHead = kHeaderWindow + 1024;
    const size_t take = std::min(size, kMaxHead - head_.size());
    head_.insert(head_.end(), data, data + take);
    std::string_view text(reinterpret_cast<const char*>(head_.data()), head_.size());
    const size_t enter = text.find("@EJL EN LA=ESC/PAGE");
    const size_t line = enter < kHeaderWindow ? text.find('\n', enter) : std::string_view::npos;
    if (line == std::string_view::npos) {
        // Aún no aparece: se espera otro bloque, salvo que ya se pasó del límite.
        if (head_.size() >= kMaxHead) Finish(EscPageCount::End::Invalid);
        return;
    }
    // Encabezado encontrado: el cuerpo es lo acumulado tras la línea más lo
    // que quedó del bloque fuera de head_.
    header_ = false;
    std::vector<std::uint8_t> body(head_.begin() + static_cast<std::ptrdiff_t>(line + 1), head_.end());
    head_.clear();
    FeedBody(body.data(), body.size());
    if (!done_) FeedBody(data + take, size - take);
    else if (count_.end == EscPageCount::End::Complete) KeepTrailer(data + take, size - take);
}

void EscPageCounter::FeedBody(const std::uint8_t* data, size_t size) {
    size_t pos = 0;
    if (!head_.empty()) {
        // Comando partido entre dos bloques: se completa con el principio de este.
        constexpr size_t kMaxCommand = 128;  // GS + 64 parámetros + 8 letras + "{" + mayúscula
        const size_t old = head_.size();
        const size_t take = std::min(size, kMaxCommand - old);
        head_.insert(head_.end(), data, data + take);
        const size_t used = Parse(head_.data(), head_.size());
        if (done_) {
            if (count_.end == EscPageCount::End::Complete) {
                KeepTrailer(head_.data(), old);
                KeepTrailer(data, size);
            }
            head_.clear();
            return;
        }
        if (used == kNeedMore) {
            if (take < size) Finish(EscPageCount::End::Invalid);  // no puede pasar: el comando no es tan largo
            // Si el bloque entero cupo y aún falta, head_ conserva todo y se
            // espera el bloque siguiente.
            return;
        }
        // El comando terminó dentro de este bloque: se sigue desde ahí.
        pos = used - old;
        head_.clear();
    }
    while (pos < size) {
        // Datos binarios de un comando "{" (p. ej. una banda de imagen):
        // se saltan sin mirarlos; pueden abarcar varios bloques.
        if (skip_ > 0) {
            const size_t n = static_cast<size_t>(std::min<std::uint64_t>(skip_, size - pos));
            pos += n;
            skip_ -= n;
            continue;
        }
        const size_t used = Parse(data + pos, size - pos);
        if (done_) {
            if (count_.end == EscPageCount::End::Complete) KeepTrailer(data + pos, size - pos);
            return;
        }
        if (used == kNeedMore) {
            // Comando cortado al final del bloque: se guarda para completarlo.
            head_.assign(data + pos, data + size);
            return;
        }
        pos += used;
    }
}

// Formato de un comando ESC/Page (deducido de los datos del AM-C6000):
//   GS (0x1D) + parámetros [0-9;.-]* + nombre en minúsculas (1-8) +
//   '{' opcional (lleva datos) + una mayúscula final.
//   Ej.: "GS 1poE" (orientación), "GS 1;2iuE" (bandeja),
//        "GS 1234;...{I" + 1234 bytes de datos (banda raster).
// Además: FF (0x0C) termina una página y ESC 01 @EJL vuelve al EJL final.

size_t EscPageCounter::Parse(const std::uint8_t* data, size_t size) {
    auto isLower = [](std::uint8_t c) { return c >= 'a' && c <= 'z'; };
    auto isUpper = [](std::uint8_t c) { return c >= 'A' && c <= 'Z'; };
    auto isParam = [](std::uint8_t c) { return (c >= '0' && c <= '9') || c == ';' || c == '.' || c == '-'; };
    const std::uint8_t c = data[0];
    if (c == 0x0c) {  // FF: fin de página
        ++count_.pages;
        count_.lastPageBlank = !pageHasData_;
        pageHasData_ = false;
        return 1;
    }
    if (c == 0x1b) {  // ESC 01 @EJL: vuelve al EJL del final del trabajo
        // Hacen falta 6 bytes (ESC, 01, "@EJL") para decidir.
        if (size < 6) return kNeedMore;
        Finish(std::memcmp(data + 1, "\x01@EJL", 5) == 0 ? EscPageCount::End::Complete : EscPageCount::End::Invalid);
        return 0;
    }
    if (c != 0x1d) {  // ni comando ni FF: no es lo que se conoce
        Finish(EscPageCount::End::Invalid);
        return 0;
    }
    // GS <parámetros> <minúsculas> [{] <mayúscula>
    // Los límites (64 caracteres de parámetros, 8 letras de nombre) evitan
    // recorrer basura indefinidamente y acotan el tamaño de head_ (128).
    size_t p = 1;
    const size_t params = p;
    while (p < size && p - params < 64 && isParam(data[p])) ++p;
    const size_t paramsEnd = p;
    const size_t name = p;
    while (p < size && p - name < 8 && isLower(data[p])) ++p;
    const bool hasData = p < size && data[p] == '{';
    if (hasData) ++p;
    // Se acabó el bloque antes de la mayúscula final: falta el resto.
    if (p >= size) return kNeedMore;
    // Sin nombre o sin mayúscula de cierre: no es un comando ESC/Page.
    if (p == name || !isUpper(data[p])) {
        Finish(EscPageCount::End::Invalid);
        return 0;
    }
    ++p;
    if (hasData) {
        // El primer parámetro es el largo de los datos que siguen (pueden
        // continuar en los bloques siguientes).
        // El tope de 2^48 evita desbordar con datos corruptos.
        std::uint64_t length = 0;
        size_t q = params;
        for (; q < paramsEnd && data[q] >= '0' && data[q] <= '9' && length < (1ull << 48); ++q)
            length = length * 10 + (data[q] - '0');
        if (q == params) {
            Finish(EscPageCount::End::Invalid);
            return 0;
        }
        skip_ = length;
        // Un comando con datos marca la página como "no vacía" (para detectar
        // el dorso de relleno de la doble cara).
        pageHasData_ = true;
    }
    return p;
}

void EscPageCounter::Finish(EscPageCount::End end) {
    done_ = true;
    count_.end = end;
}

void EscPageCounter::KeepTrailer(const std::uint8_t* data, size_t size) {
    const size_t take = std::min(size, kTrailerWindow - std::min(kTrailerWindow, count_.trailer.size()));
    count_.trailer.insert(count_.trailer.end(), data, data + take);
}

// Si no terminó: Invalid si nunca se pasó del encabezado (no es ESC/Page o
// no llegó lo suficiente), Truncated si se contaba pero faltan datos.
EscPageCount EscPageCounter::Result() const {
    EscPageCount out = count_;
    if (!done_) out.end = header_ ? EscPageCount::End::Invalid : EscPageCount::End::Truncated;
    return out;
}

void ParsePdlSettings(const std::uint8_t* data, size_t size, bool complete, core::ContentFacts& facts,
                      const EscPageCount* allData) {
    if (!data || size == 0) return;
    // 1. Comandos del principio (hasta el ENTER LANGUAGE / EN LA=).
    auto header = CommandLines(data, std::min(size, kHeaderWindow), true);
    // 2. Comandos del final: de los propios datos si están completos, o del
    //    trailer que guardó el conteo hecho sobre todos los datos (D53).
    std::vector<std::string> trailer;
    const bool counted = allData && allData->end == EscPageCount::End::Complete;
    if (complete) {
        size_t start = size > kTrailerWindow ? size - kTrailerWindow : 0;
        trailer = CommandLines(data + start, size - start, false);
    } else if (counted) {
        trailer = CommandLines(allData->trailer.data(), allData->trailer.size(), false);
    }
    // 3. EJL (Epson) o PJL: son excluyentes; si aparece alguna línea EJL se
    //    trata todo como Epson.
    const bool isEjl = std::any_of(header.begin(), header.end(), [](const std::string& l) { return l.rfind("@EJL", 0) == 0; });
    if (isEjl) ParseEjl(data, size, header, trailer, facts, counted ? allData : nullptr);
    else ParsePjl(header, facts);
    // PostScript (directo o tras el encabezado PJL): comentarios DSC.
    const size_t start = SkipPjlHeader(data, size);
    const std::uint8_t* body = data + start;
    const size_t bodySize = size - start;
    if (bodySize >= 2 && (body[0] == '%' && body[1] == '!')) ParseDsc(body, bodySize, complete, facts);
    else if (bodySize >= 3 && body[0] == 4 && body[1] == '%' && body[2] == '!') ParseDsc(body + 1, bodySize - 1, complete, facts);
}

}  // namespace cola::preview
