#include "core/PrintSettings.h"

#include <cstddef>
#include <cwchar>
#include <iterator>

namespace cola::core {

namespace {

// Tamaños DMPAPER_* habituales (wingdi.h), en décimas de milímetro.
const PaperInfo kPapers[] = {
    {DMPAPER_LETTER, L"Carta", L"Letter", 2159, 2794},
    {DMPAPER_LETTERSMALL, L"Carta pequeña", L"Letter Small", 2159, 2794},
    {DMPAPER_TABLOID, L"Tabloide", L"Tabloid", 2794, 4318},
    {DMPAPER_LEDGER, L"Ledger", L"Ledger", 4318, 2794},
    {DMPAPER_LEGAL, L"Oficio (Legal)", L"Legal", 2159, 3556},
    {DMPAPER_STATEMENT, L"Media carta (Statement)", L"Statement", 1397, 2159},
    {DMPAPER_EXECUTIVE, L"Ejecutivo", L"Executive", 1842, 2667},
    {DMPAPER_A3, L"A3", L"A3", 2970, 4200},
    {DMPAPER_A4, L"A4", L"A4", 2100, 2970},
    {DMPAPER_A4SMALL, L"A4 pequeño", L"A4 Small", 2100, 2970},
    {DMPAPER_A5, L"A5", L"A5", 1480, 2100},
    {DMPAPER_B4, L"B4 (JIS)", L"B4 (JIS)", 2570, 3640},
    {DMPAPER_B5, L"B5 (JIS)", L"B5 (JIS)", 1820, 2570},
    {DMPAPER_FOLIO, L"Folio", L"Folio", 2159, 3302},
    {DMPAPER_QUARTO, L"Cuarto", L"Quarto", 2150, 2750},
    {DMPAPER_10X14, L"10×14 pulg.", L"10×14 in", 2540, 3556},
    {DMPAPER_11X17, L"11×17 pulg.", L"11×17 in", 2794, 4318},
    {DMPAPER_NOTE, L"Nota", L"Note", 2159, 2794},
    {DMPAPER_ENV_9, L"Sobre n.º 9", L"Envelope #9", 984, 2254},
    {DMPAPER_ENV_10, L"Sobre n.º 10", L"Envelope #10", 1048, 2413},
    {DMPAPER_ENV_DL, L"Sobre DL", L"Envelope DL", 1100, 2200},
    {DMPAPER_ENV_C5, L"Sobre C5", L"Envelope C5", 1620, 2290},
    {DMPAPER_ENV_C4, L"Sobre C4", L"Envelope C4", 2290, 3240},
    {DMPAPER_ENV_C6, L"Sobre C6", L"Envelope C6", 1140, 1620},
    {DMPAPER_ENV_B5, L"Sobre B5", L"Envelope B5", 1760, 2500},
    {DMPAPER_ENV_MONARCH, L"Sobre Monarch", L"Envelope Monarch", 984, 1905},
    {DMPAPER_FANFOLD_US, L"Papel continuo EE. UU.", L"US Std Fanfold", 3778, 2794},
    {DMPAPER_ISO_B4, L"B4 (ISO)", L"B4 (ISO)", 2500, 3530},
    {DMPAPER_JAPANESE_POSTCARD, L"Postal japonesa", L"Japanese Postcard", 1000, 1480},
    {DMPAPER_LETTER_EXTRA, L"Carta extra", L"Letter Extra", 2413, 3048},
    {DMPAPER_LEGAL_EXTRA, L"Oficio extra", L"Legal Extra", 2413, 3810},
    {DMPAPER_A4_EXTRA, L"A4 extra", L"A4 Extra", 2354, 3223},
    {DMPAPER_LETTER_TRANSVERSE, L"Carta transversal", L"Letter Transverse", 2102, 2794},
    {DMPAPER_A4_TRANSVERSE, L"A4 transversal", L"A4 Transverse", 2100, 2970},
    {DMPAPER_A_PLUS, L"Super A", L"SuperA", 2270, 3560},
    {DMPAPER_B_PLUS, L"Super B (A3+)", L"SuperB (A3+)", 3050, 4870},
    {DMPAPER_A4_PLUS, L"A4 plus", L"A4 Plus", 2100, 3300},
    {DMPAPER_A3_EXTRA, L"A3 extra", L"A3 Extra", 3220, 4450},
    {DMPAPER_A2, L"A2", L"A2", 4200, 5940},
    {DMPAPER_A6, L"A6", L"A6", 1050, 1480},
    // Sin código DMPAPER estándar (cada controlador le da el suyo; en el Epson
    // AM-C6000 es 625): código 0, solo se reconoce por las medidas.
    {0, L"12 × 18", L"12 × 18 in", 3048, 4572},
};

// true si el campo [offset, offset+size) cabe en los bytes que declara dmSize.
template <typename Member>
bool Fits(std::uint16_t dmSize, std::size_t offset, const Member&) {
    return offset + sizeof(Member) <= dmSize;
}

}  // namespace

PrintSettings ParseDevMode(const DEVMODEW* dm, size_t available) {
    PrintSettings s;
    if (!dm) return s;
    // dmSize está en un desplazamiento fijo: hace falta poder leer al menos hasta ahí.
    constexpr size_t kMinHeader = offsetof(DEVMODEW, dmFields) + sizeof(DWORD);
    if (available && available < kMinHeader) return s;
    if (dm->dmSize < kMinHeader) return s;
    std::uint16_t size = dm->dmSize;
    if (available && available < size) size = static_cast<std::uint16_t>(available);

    s.present = true;
    s.size = dm->dmSize;
    s.driverExtra = dm->dmDriverExtra;
    s.specVersion = dm->dmSpecVersion;
    s.driverVersion = dm->dmDriverVersion;
    s.fields = dm->dmFields;
    s.deviceName.assign(dm->dmDeviceName, wcsnlen(dm->dmDeviceName, CCHDEVICENAME));

    const DWORD f = dm->dmFields;
    // Un campo es válido solo si su bit está en dmFields y cabe en dmSize.
#define COLA_DM_FIELD(flag, member, target)                                          \
    if ((f & (flag)) && Fits(size, offsetof(DEVMODEW, member), dm->member)) \
        s.target.Set(dm->member)

    COLA_DM_FIELD(DM_ORIENTATION, dmOrientation, orientation);
    COLA_DM_FIELD(DM_PAPERSIZE, dmPaperSize, paperSize);
    COLA_DM_FIELD(DM_PAPERLENGTH, dmPaperLength, paperLength);
    COLA_DM_FIELD(DM_PAPERWIDTH, dmPaperWidth, paperWidth);
    COLA_DM_FIELD(DM_SCALE, dmScale, scale);
    COLA_DM_FIELD(DM_COPIES, dmCopies, copies);
    COLA_DM_FIELD(DM_DEFAULTSOURCE, dmDefaultSource, defaultSource);
    COLA_DM_FIELD(DM_PRINTQUALITY, dmPrintQuality, printQuality);
    COLA_DM_FIELD(DM_COLOR, dmColor, color);
    COLA_DM_FIELD(DM_DUPLEX, dmDuplex, duplex);
    COLA_DM_FIELD(DM_YRESOLUTION, dmYResolution, yResolution);
    COLA_DM_FIELD(DM_COLLATE, dmCollate, collate);
    COLA_DM_FIELD(DM_NUP, dmNup, nup);
    COLA_DM_FIELD(DM_MEDIATYPE, dmMediaType, mediaType);
#undef COLA_DM_FIELD

    if ((f & DM_FORMNAME) && Fits(size, offsetof(DEVMODEW, dmFormName), dm->dmFormName)) {
        std::wstring form(dm->dmFormName, wcsnlen(dm->dmFormName, CCHFORMNAME));
        if (!form.empty()) s.formName.Set(std::move(form));
    }
    // Valores fuera de rango = el campo no es utilizable aunque el bit esté puesto.
    if (s.copies.valid && s.copies.value < 1) s.copies.valid = false;
    if (s.duplex.valid && (s.duplex.value < DMDUP_SIMPLEX || s.duplex.value > DMDUP_HORIZONTAL)) s.duplex.valid = false;
    if (s.orientation.valid && s.orientation.value != DMORIENT_PORTRAIT && s.orientation.value != DMORIENT_LANDSCAPE)
        s.orientation.valid = false;
    if (s.color.valid && s.color.value != DMCOLOR_MONOCHROME && s.color.value != DMCOLOR_COLOR) s.color.valid = false;
    if (s.collate.valid && s.collate.value != DMCOLLATE_FALSE && s.collate.value != DMCOLLATE_TRUE)
        s.collate.valid = false;
    if (s.paperWidth.valid && s.paperWidth.value <= 0) s.paperWidth.valid = false;
    if (s.paperLength.valid && s.paperLength.value <= 0) s.paperLength.valid = false;
    return s;
}

const PaperInfo* FindPaper(short code) {
    for (const auto& p : kPapers)
        if (p.code != 0 && p.code == code) return &p;
    return nullptr;
}

const PaperInfo* FindPaperBySize(short width, short length) {
    auto close = [](short a, short b) { return a - b <= 20 && b - a <= 20; };
    for (const auto& p : kPapers) {
        if ((close(p.width, width) && close(p.length, length)) || (close(p.width, length) && close(p.length, width)))
            return &p;
    }
    return nullptr;
}

PaperSize EffectivePaper(const PrintSettings& s) {
    PaperSize out;
    if (s.paperWidth.valid && s.paperLength.valid) {
        out.known = true;
        out.width = s.paperWidth.value;
        out.length = s.paperLength.value;
        const PaperInfo* p = s.paperSize.valid ? FindPaper(s.paperSize.value) : nullptr;
        out.custom = !p || p->width != out.width || p->length != out.length;
        return out;
    }
    if (s.paperSize.valid) {
        if (const PaperInfo* p = FindPaper(s.paperSize.value)) {
            out.known = true;
            out.width = p->width;
            out.length = p->length;
        } else if (s.paperSize.value == DMPAPER_USER) {
            out.custom = true;  // personalizado sin medidas en el DEVMODE
        }
    }
    return out;
}

bool SameVisibleSettings(const PrintSettings& a, const PrintSettings& b) {
    if (!a.present || !b.present) return false;
    auto same = [](const auto& x, const auto& y) { return x.valid == y.valid && (!x.valid || x.value == y.value); };
    return same(a.paperSize, b.paperSize) && same(a.orientation, b.orientation) && same(a.duplex, b.duplex) &&
           same(a.copies, b.copies) && same(a.color, b.color) && same(a.collate, b.collate) &&
           same(a.defaultSource, b.defaultSource) && same(a.paperWidth, b.paperWidth) &&
           same(a.paperLength, b.paperLength);
}

}  // namespace cola::core
