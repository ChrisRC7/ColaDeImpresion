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

constexpr size_t kHeaderWindow = 64 * 1024;
constexpr size_t kTrailerWindow = 16 * 1024;

std::string Upper(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

std::string TrimAscii(const std::string& text) {
    size_t a = text.find_first_not_of(" \t\r\"");
    if (a == std::string::npos) return "";
    size_t b = text.find_last_not_of(" \t\r\"");
    return text.substr(a, b - a + 1);
}

// Líneas "@PJL ..." / "@EJL ..." de una ventana de los datos, en orden.
// stopAtEnter = no seguir después de la línea que entra al lenguaje de página
// (lo que sigue es el documento, no opciones).
std::vector<std::string> CommandLines(const std::uint8_t* data, size_t size, bool stopAtEnter) {
    std::vector<std::string> lines;
    std::string window(reinterpret_cast<const char*>(data), size);
    size_t pos = 0;
    while (pos < window.size()) {
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

void SetPaper(const std::string& name, core::ContentFacts& facts) {
    if (name.empty()) return;
    if (name == "ARCHB") {
        // 12 x 18 pulgadas (ANSI Arch B): así lo escribe el controlador Epson AM-C6000.
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

void AppendRaw(core::ContentFacts& facts, const std::string& text) {
    if (!facts.rawSettings.empty()) facts.rawSettings += L" ";
    facts.rawSettings += util::FromUtf8(text);
}

void ParsePjl(const std::vector<std::string>& lines, core::ContentFacts& facts) {
    std::uint32_t copies = 0, qty = 0;
    std::string duplex, binding;
    bool any = false;
    for (const auto& line : lines) {
        if (line.rfind("@PJL SET ", 0) != 0) continue;
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
            facts.color.Set(DMCOLOR_MONOCHROME);
        }
    }
    if (!any) return;
    facts.settingsSource = core::ContentFacts::SettingsSource::Pjl;
    facts.settingsDocumented = true;
    // QTY = copias intercaladas del trabajo; COPIES = copias sin intercalar de cada página.
    if (copies > 0 || qty > 0) {
        facts.copies.Set(std::max(copies, 1u) * std::max(qty, 1u));
        if (qty > 1) facts.collate.Set(DMCOLLATE_TRUE);
        else if (copies > 1) facts.collate.Set(DMCOLLATE_FALSE);
    }
    if (duplex == "OFF") facts.duplex.Set(DMDUP_SIMPLEX);
    else if (duplex == "ON") facts.duplex.Set(binding == "SHORTEDGE" ? DMDUP_HORIZONTAL : DMDUP_VERTICAL);
}

// Controladores con los que se verificó el significado de las claves EJL
// (generando sus datos "a archivo" con distintas opciones, 2026-10-02):
//   QT = copias (CO no cambia), PS = papel (LT/A4/LGL), DX/BD = doble cara y
//   borde, "JI PAGES" = hojas físicas (ya incluye copias y doble cara), y en
//   ESC/Page "GS n poE" = orientación (0 vertical, 1 horizontal), "GS n;m iuE" =
//   bandeja, y PS=ARCHB = 12 x 18 pulgadas.
bool VerifiedEpsonDriver(const std::string& driverUpper) {
    return driverUpper.find("EPSON AM-C6000") != std::string::npos;
}

// Primer parámetro de un comando ESC/Page "GS <n>[;<m>...] <cmd>" al comienzo
// del documento (-1 si no está).
int EscPageParam(const std::uint8_t* data, size_t size, const char* command) {
    std::string_view text(reinterpret_cast<const char*>(data), std::min<size_t>(size, 64 * 1024));
    size_t lang = text.find("LA=ESC/PAGE");
    size_t at = text.find(command, lang == std::string_view::npos ? 0 : lang);
    if (at == std::string_view::npos || at < 2) return -1;
    size_t start = at;
    while (start > 0 && ((text[start - 1] >= '0' && text[start - 1] <= '9') || text[start - 1] == ';')) --start;
    if (start == at || start == 0 || text[start - 1] != 0x1d) return -1;
    return atoi(std::string(text.substr(start, at - start)).c_str());
}

// Bandeja de "GS n;m iuE" como el código DMBIN que usa el controlador AM-C6000
// (verificado imprimiendo a archivo con cada bandeja): 0 = Auto selección (7),
// 1 = Bandeja de papel 1 (258), 2 = Bandeja de papel 2 (259), 15 = Alimentador de papel (261).
short EpsonBin(int unit) {
    switch (unit) {
        case 0: return DMBIN_AUTO;
        case 1: return 258;
        case 2: return 259;
        case 15: return 261;
        default: return 0;
    }
}

void ParseEjl(const std::uint8_t* data, size_t size, const std::vector<std::string>& header,
              const std::vector<std::string>& trailer, core::ContentFacts& facts) {
    std::string dx, bd, driver;
    std::uint32_t quantity = 0;
    bool any = false;
    for (const auto& line : header) {
        if (line.rfind("@EJL JI ", 0) == 0) {
            size_t at = line.find("DRIVER=\"");
            if (at != std::string::npos) driver = line.substr(at + 8, line.find('"', at + 8) - at - 8);
        }
        if (line.rfind("@EJL SET ", 0) != 0) continue;
        any = true;
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
        if (dx == "OFF") facts.duplex.Set(DMDUP_SIMPLEX);
        else if (dx == "ON") facts.duplex.Set(bd == "SE" ? DMDUP_HORIZONTAL : DMDUP_VERTICAL);
        if (quantity > 0) facts.copies.Set(quantity);
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
        const EscPageCount count = CountEscPagePages(data, size);
        std::uint32_t pages = count.pages;
        if (dx == "ON" && count.lastPageBlank && pages >= 2 && pages % 2 == 0) --pages;  // dorso de relleno
        if (count.end == EscPageCount::End::Complete && pages > 0) {
            facts.pageSource = core::ContentFacts::PageSource::EscPageFormFeeds;
            facts.pages = pages;
        }
        // Datos cortados por tamaño (no inválidos): TotalPages puede traer ese dorso.
        facts.spoolerCountsBlankBack = VerifiedEpsonDriver(driver) && count.end == EscPageCount::End::Truncated;
    }
    // "@EJL JI PAGES="2"" al final: hojas físicas que calculó el controlador
    // (1 pág. x 10 copias = 10; 2 págs. a doble cara = 1).
    for (const auto& line : trailer) {
        size_t at = line.find("PAGES=");
        if (line.rfind("@EJL JI", 0) != 0 || at == std::string::npos) continue;
        int sheets = atoi(TrimAscii(line.substr(at + 6)).c_str());
        if (sheets > 0) facts.sheetsCounter.Set(static_cast<std::uint32_t>(sheets));
    }
}

// Valor de un comentario DSC: "%%Pages: 2" -> "2".
std::string DscValue(const std::string& line, size_t prefix) { return TrimAscii(line.substr(prefix)); }

// Comentarios DSC (Adobe Document Structuring Conventions 3.0) y funciones PPD
// que escriben los controladores PostScript (PSCRIPT5) y las aplicaciones que
// generan PostScript (Acrobat): sintaxis documentada.
void ParseDsc(const std::uint8_t* data, size_t size, bool complete, core::ContentFacts& facts) {
    // Las opciones van en el prólogo / setup, antes de la primera página ("%%Page:").
    const size_t window = std::min<size_t>(size, 8 * 1024 * 1024);
    std::string text(reinterpret_cast<const char*>(data), window);
    size_t firstPage = text.find("\n%%Page: ");
    if (firstPage != std::string::npos) text.resize(firstPage);
    bool any = false;
    std::string duplex;
    size_t pos = 0;
    while ((pos = text.find("%%", pos)) != std::string::npos) {
        size_t end = text.find_first_of("\r\n", pos);
        std::string line = text.substr(pos, (end == std::string::npos ? text.size() : end) - pos);
        pos = end == std::string::npos ? text.size() : end;
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
    using End = EscPageCount::End;
    EscPageCount out;
    if (!data) return out;
    // Los comandos empiezan después de la línea "@EJL EN LA=ESC/PAGE...".
    std::string_view text(reinterpret_cast<const char*>(data), std::min(size, kHeaderWindow));
    size_t enter = text.find("@EJL EN LA=ESC/PAGE");
    if (enter == std::string_view::npos) return out;
    size_t pos = text.find('\n', enter);
    if (pos == std::string_view::npos) return out;
    ++pos;
    auto isLower = [](std::uint8_t c) { return c >= 'a' && c <= 'z'; };
    auto isUpper = [](std::uint8_t c) { return c >= 'A' && c <= 'Z'; };
    auto isParam = [](std::uint8_t c) { return (c >= '0' && c <= '9') || c == ';' || c == '.' || c == '-'; };
    bool pageHasData = false;  // la página actual trajo algún comando con datos (imagen)
    while (pos < size) {
        const std::uint8_t c = data[pos];
        if (c == 0x0c) {  // FF: fin de página
            ++out.pages;
            out.lastPageBlank = !pageHasData;
            pageHasData = false;
            ++pos;
            continue;
        }
        if (c == 0x1b) {  // ESC 01 @EJL: vuelve al EJL del final del trabajo
            if (size - pos < 6) break;
            out.end = std::memcmp(data + pos + 1, "\x01@EJL", 5) == 0 ? End::Complete : End::Invalid;
            return out;
        }
        if (c != 0x1d) return out;  // ni comando ni FF: no es lo que se conoce
        // GS <parámetros> <minúsculas> [{] <mayúscula>
        size_t p = pos + 1;
        const size_t params = p;
        while (p < size && p - params < 64 && isParam(data[p])) ++p;
        const size_t paramsEnd = p;
        const size_t name = p;
        while (p < size && p - name < 8 && isLower(data[p])) ++p;
        const bool hasData = p < size && data[p] == '{';
        if (hasData) ++p;
        if (p >= size) break;
        if (p == name || !isUpper(data[p])) return out;
        ++p;
        if (hasData) {
            // El primer parámetro es el largo de los datos que siguen.
            std::uint64_t length = 0;
            size_t q = params;
            for (; q < paramsEnd && data[q] >= '0' && data[q] <= '9' && length <= size; ++q)
                length = length * 10 + (data[q] - '0');
            if (q == params) return out;
            if (length > size - p) break;  // los datos siguen más allá de lo leído
            p += static_cast<size_t>(length);
            pageHasData = true;
        }
        pos = p;
    }
    out.end = End::Truncated;
    return out;
}

void ParsePdlSettings(const std::uint8_t* data, size_t size, bool complete, core::ContentFacts& facts) {
    if (!data || size == 0) return;
    auto header = CommandLines(data, std::min(size, kHeaderWindow), true);
    std::vector<std::string> trailer;
    if (complete) {
        size_t start = size > kTrailerWindow ? size - kTrailerWindow : 0;
        trailer = CommandLines(data + start, size - start, false);
    }
    const bool isEjl = std::any_of(header.begin(), header.end(), [](const std::string& l) { return l.rfind("@EJL", 0) == 0; });
    if (isEjl) ParseEjl(data, size, header, trailer, facts);
    else ParsePjl(header, facts);
    // PostScript (directo o tras el encabezado PJL): comentarios DSC.
    const size_t start = SkipPjlHeader(data, size);
    const std::uint8_t* body = data + start;
    const size_t bodySize = size - start;
    if (bodySize >= 2 && (body[0] == '%' && body[1] == '!')) ParseDsc(body, bodySize, complete, facts);
    else if (bodySize >= 3 && body[0] == 4 && body[1] == '%' && body[2] == '!') ParseDsc(body + 1, bodySize - 1, complete, facts);
}

}  // namespace cola::preview
