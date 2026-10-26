#include "app/JobSheet.h"

#include <winspool.h>

#include <algorithm>
#include <cstdlib>

#include "app/RowBuilder.h"
#include "i18n/Strings.h"
#include "util/Format.h"
#include "util/Strings.h"

namespace cola::app {

using core::Basis;
using core::Reason;
using core::Reliability;
using i18n::S;
using i18n::Tr;

namespace {

// Décimas de milímetro -> "215,9" (coma decimal en español).
std::wstring Millimeters(short tenths) {
    const wchar_t sep = i18n::GetLanguage() == i18n::Language::Spanish ? L',' : L'.';
    return util::Format(L"%d%lc%d", tenths / 10, sep, std::abs(tenths % 10));
}

std::wstring SizeText(short width, short length) {
    return Millimeters(width) + L" × " + Millimeters(length) + L" mm";
}

ui::Tone ToneFor(Reliability r) {
    switch (r) {
        case Reliability::Reliable: return ui::Tone::Success;
        case Reliability::Estimated: return ui::Tone::Warning;
        default: return ui::Tone::Neutral;
    }
}

std::wstring Join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + L" " + b;
}

std::wstring ContentSourceName(const core::ContentFacts* facts) {
    if (!facts) return L"";
    if (facts->settingsSource == core::ContentFacts::SettingsSource::Ejl && !facts->verifiedDriver.empty())
        return util::Format(Tr(S::SourceEjlVerified), facts->verifiedDriver.c_str());
    switch (facts->settingsSource) {
        case core::ContentFacts::SettingsSource::Pjl: return Tr(S::SourcePjl);
        case core::ContentFacts::SettingsSource::Ejl: return Tr(S::SourceEjl);
        case core::ContentFacts::SettingsSource::PrintTicket: return Tr(S::SourcePrintTicket);
        case core::ContentFacts::SettingsSource::PostScriptDsc: return Tr(S::SourceDsc);
        default: return L"";
    }
}

// Texto de origen para una opción según de dónde salió.
std::wstring OriginText(Basis basis, const std::wstring& dmFields, const core::ContentFacts* facts) {
    switch (basis) {
        case Basis::DevMode: return util::Format(Tr(S::OriginDevMode), dmFields.c_str());
        case Basis::SpoolDevMode: return util::Format(Tr(S::OriginSpoolDevMode), dmFields.c_str());
        case Basis::DevModeAndContent:
            return util::Format(Tr(S::OriginDevModeAndContent), dmFields.c_str(), ContentSourceName(facts).c_str());
        case Basis::Content: return util::Format(Tr(S::OriginContent), ContentSourceName(facts).c_str());
        case Basis::SpoolerTotalPages: return Tr(S::OriginSpoolerPages);
        case Basis::ContentPages: return util::Format(Tr(S::OriginContentPages), facts ? facts->language.c_str() : L"");
        case Basis::ContentRepeat: return Tr(S::OriginContentRepeat);
        case Basis::DriverSheets: return Tr(S::OriginDriverSheets);
        case Basis::AssumedDefault: return Tr(S::OriginAssumed);
        case Basis::SinglePage: return Tr(S::OriginSinglePage);
        default: return L"";
    }
}

ui::InfoRow Section(S label) {
    ui::InfoRow row;
    row.kind = ui::InfoRow::Kind::Section;
    row.label = Tr(label);
    return row;
}

ui::InfoRow Field(S label, std::wstring value, std::wstring detail = L"") {
    ui::InfoRow row;
    row.label = Tr(label);
    row.value = std::move(value);
    row.detail = std::move(detail);
    return row;
}

void SetBadge(ui::InfoRow& row, Reliability reliability) {
    row.badge = ReliabilityText(reliability);
    row.badgeTone = ToneFor(reliability);
}

// Fila de una cantidad calculada (páginas, copias, caras, hojas).
ui::InfoRow QuantityRow(S label, const core::Quantity& q, const std::wstring& origin) {
    ui::InfoRow row = Field(label, q.Known() ? util::Format(L"%u", q.value) : std::wstring(Tr(S::ValueUndetermined)));
    // Con una repetición detectada el motivo ya explica el origen.
    std::wstring detail = q.reason == Reason::RepeatDetected && q.basis == Basis::ContentRepeat ? L"" : origin;
    if (q.reason != Reason::None) detail = Join(detail.empty() ? L"" : detail + L".", ReasonText(q.reason));
    row.detail = detail;
    SetBadge(row, q.reliability);
    return row;
}

// Fila de una opción (papel, orientación...) evaluada con AssessSetting.
ui::InfoRow SettingRow(S label, std::wstring value, const core::SettingAssessment& a, const std::wstring& dmFields,
                       const core::ContentFacts* facts, const std::wstring& extra = L"") {
    std::wstring detail = OriginText(a.basis, dmFields, facts);
    if (!extra.empty()) detail = Join(detail.empty() ? L"" : detail + L".", extra);
    if (a.reason != Reason::None) detail = Join(detail.empty() ? L"" : detail + L".", ReasonText(a.reason));
    ui::InfoRow row = Field(label, std::move(value), detail);
    SetBadge(row, a.reliability);
    return row;
}

std::wstring SourceText(short bin) {
    switch (bin) {
        case DMBIN_UPPER: return Tr(S::BinUpper);
        case DMBIN_LOWER: return Tr(S::BinLower);
        case DMBIN_MIDDLE: return Tr(S::BinMiddle);
        case DMBIN_MANUAL: return Tr(S::BinManual);
        case DMBIN_ENVELOPE: return Tr(S::BinEnvelope);
        case DMBIN_ENVMANUAL: return Tr(S::BinEnvManual);
        case DMBIN_AUTO: return Tr(S::BinAuto);
        case DMBIN_TRACTOR: return Tr(S::BinTractor);
        case DMBIN_LARGECAPACITY: return Tr(S::BinLargeCapacity);
        case DMBIN_CASSETTE: return Tr(S::BinCassette);
        case DMBIN_FORMSOURCE: return Tr(S::BinFormSource);
        default: return util::Format(Tr(S::BinDriver), bin);
    }
}

// Papel propio del controlador (código >= 257, p. ej. 625 = 12 x 18 en el Epson AM-C6000).
const core::DriverPaper* DriverPaperFor(short code, const core::PrinterInfo* printer) {
    if (!printer || code <= DMPAPER_USER) return nullptr;
    for (const auto& p : printer->papers)
        if (p.code == code) return &p;
    return nullptr;
}

// "12 x 18 pulg. (305 x 457 mm)" -> "12 x 18 pulg."
std::wstring ShortDriverName(const std::wstring& name) {
    size_t paren = name.find(L" (");
    return paren == std::wstring::npos ? name : name.substr(0, paren);
}

std::wstring QualityText(short quality) {
    switch (quality) {
        case DMRES_DRAFT: return Tr(S::QualityDraft);
        case DMRES_LOW: return Tr(S::QualityLow);
        case DMRES_MEDIUM: return Tr(S::QualityMedium);
        case DMRES_HIGH: return Tr(S::QualityHigh);
        default: return util::Format(Tr(S::QualityDpi), quality);
    }
}

std::wstring MediaText(std::uint32_t media) {
    switch (media) {
        case DMMEDIA_STANDARD: return Tr(S::MediaStandard);
        case DMMEDIA_TRANSPARENCY: return Tr(S::MediaTransparency);
        case DMMEDIA_GLOSSY: return Tr(S::MediaGlossy);
        default: return util::Format(Tr(S::MediaDriver), static_cast<unsigned long>(media));
    }
}

std::wstring ColorText(short color) { return color == DMCOLOR_COLOR ? Tr(S::ValueColor) : Tr(S::ValueMono); }
std::wstring CollateText(short collate) {
    return collate == DMCOLLATE_TRUE ? Tr(S::ValueCollateYes) : Tr(S::ValueCollateNo);
}

// true si el papel declarado en los datos coincide con el del DEVMODE (±2 mm,
// en cualquier orientación: el PrintTicket da el tamaño en vertical).
bool SamePaper(const core::PaperSize& dm, const core::ContentFacts& facts) {
    if (!dm.known || !facts.paperWidth.valid || !facts.paperLength.valid) return false;
    auto close = [](short a, short b) { return std::abs(a - b) <= 20; };
    return (close(dm.width, facts.paperWidth.value) && close(dm.length, facts.paperLength.value)) ||
           (close(dm.width, facts.paperLength.value) && close(dm.length, facts.paperWidth.value));
}

std::wstring PaperFields(const core::PrintSettings& s) {
    std::wstring out;
    auto add = [&](const std::wstring& part) { out += (out.empty() ? L"" : L", ") + part; };
    if (s.paperSize.valid) add(util::Format(L"dmPaperSize=%d", s.paperSize.value));
    if (s.formName.valid) add(L"dmFormName=" + s.formName.value);
    if (s.paperWidth.valid) add(util::Format(L"dmPaperWidth=%d", s.paperWidth.value));
    if (s.paperLength.valid) add(util::Format(L"dmPaperLength=%d", s.paperLength.value));
    return out;
}

const core::ContentFacts* FactsOf(const preview::PreviewDocument* doc) {
    return doc && doc->facts.analyzed ? &doc->facts : nullptr;
}

}  // namespace

namespace {

// Valor para la lista: sin marcas (la fiabilidad se ve en la pestaña Configuración).
std::wstring Marked(core::Reliability reliability, const std::wstring& text) {
    if (text.empty() || reliability == Reliability::Unknown) return L"—";
    return text;
}

// Controlador del trabajo: el que declaran los datos (EJL) o el de JOB_INFO_2.
std::wstring JobDriver(const core::JobInfo& job, const core::ContentFacts* facts) {
    return facts && !facts->verifiedDriver.empty() ? facts->verifiedDriver : job.driver;
}

// Nombre corto de un papel: el del tamaño estándar, el del controlador o las medidas.
std::wstring ShortPaper(short code, bool custom, short width, short length, const std::wstring& fallback,
                        const core::PrinterInfo* printer = nullptr) {
    if (const core::DriverPaper* driver = DriverPaperFor(code, printer)) {
        // Con medidas conocidas, el mismo nombre que en el resto de la lista ("12 × 18").
        if (const core::PaperInfo* known = core::FindPaperBySize(driver->width, driver->length))
            return i18n::GetLanguage() == i18n::Language::Spanish ? known->nameEs : known->nameEn;
        return ShortDriverName(driver->name);
    }
    const core::PaperInfo* info = !custom && code ? core::FindPaper(code) : nullptr;
    if (!info && width > 0 && length > 0 && !custom) info = core::FindPaperBySize(width, length);
    if (info) return i18n::GetLanguage() == i18n::Language::Spanish ? info->nameEs : info->nameEn;
    if (width > 0 && length > 0) return SizeText(width, length);
    return fallback;
}

}  // namespace

bool IsVerifiedEpsonDriver(const std::wstring& driver) {
    std::wstring upper = driver;
    for (auto& c : upper) c = static_cast<wchar_t>(towupper(c));
    return upper.find(L"EPSON AM-C6000") != std::wstring::npos;
}

std::wstring BinName(short bin, const std::wstring& driver, const core::PrinterInfo* printer) {
    // Bandejas de las Epson del LAIMI con los nombres que usa el personal
    // (códigos verificados imprimiendo a archivo con cada bandeja).
    if (IsVerifiedEpsonDriver(driver) || (printer && IsVerifiedEpsonDriver(printer->driver))) {
        switch (bin) {
            case DMBIN_AUTO: return Tr(S::BinAuto);
            case 258: return Tr(S::BinTray1);
            case 259: return Tr(S::BinTray2);
            case 261: return Tr(S::BinFeeder);
            default: break;
        }
    }
    if (printer)
        for (const auto& [code, name] : printer->bins)
            if (code == bin && !name.empty()) return name;
    return SourceText(bin);
}

ListSettings ResolveListSettings(const core::JobInfo& job, const core::ContentFacts* factsIn,
                                 const core::PrinterInfo* printer) {
    ListSettings out;
    short orientation = 0;  // para la presentación: 0 = no se conoce
    const core::ContentFacts* facts = factsIn && factsIn->analyzed ? factsIn : nullptr;
    const core::PrintSettings& dm = core::EffectiveDevMode(job, facts);
    const core::PageAccounting acc = core::ComputePageAccounting(job, facts);
    const bool documented = facts && facts->settingsDocumented;
    const bool dataRead = facts != nullptr;

    // Papel.
    {
        core::PaperSize size = core::EffectivePaper(dm);
        const bool inDm = dm.paperSize.valid || dm.formName.valid || size.known;
        const bool inContent = facts && (facts->paperName.valid || facts->paperWidth.valid);
        const bool agree = inContent && SamePaper(size, *facts);
        auto a = core::AssessSetting(acc.kind, inDm, inContent, documented, agree, dataRead);
        std::wstring text;
        if (inContent && (documented || !inDm))
            text = ShortPaper(0, false, facts->paperWidth.valid ? facts->paperWidth.value : 0,
                              facts->paperLength.valid ? facts->paperLength.value : 0,
                              facts->paperName.valid ? facts->paperName.value : L"");
        else if (inDm)
            text = ShortPaper(dm.paperSize.valid ? dm.paperSize.value : 0, size.custom, size.width, size.length,
                              dm.formName.valid ? dm.formName.value : L"", printer);
        out.paper = Marked(a.reliability, text);
    }
    // Orientación.
    {
        const bool inDm = dm.orientation.valid;
        const bool inContent = facts && facts->orientation.valid;
        const bool agree = inDm && inContent && dm.orientation.value == facts->orientation.value;
        auto a = core::AssessSetting(acc.kind, inDm, inContent, documented, agree, dataRead);
        short value = inContent && (documented || !inDm) ? facts->orientation.value : dm.orientation.value;
        if ((inDm || inContent) && a.reliability != Reliability::Unknown) orientation = value;
        out.orientation = Marked(a.reliability, inDm || inContent ? std::wstring(value == DMORIENT_LANDSCAPE
                                                                                     ? Tr(S::ValueLandscapeShort)
                                                                                     : Tr(S::ValuePortraitShort))
                                                                 : L"");
    }
    // Doble cara (incluye el supuesto "una cara" de la contabilidad).
    {
        const wchar_t* text = acc.duplex.value == DMDUP_VERTICAL     ? Tr(S::ValueDuplexLongShort)
                              : acc.duplex.value == DMDUP_HORIZONTAL ? Tr(S::ValueDuplexShortShort)
                                                                     : Tr(S::ValueSimplex);
        // Epson con datos sin leer (o ilegibles): el DEVMODE del servidor es el
        // predeterminado de la cola (D29), no lo que eligió el usuario: "—".
        const bool queueDefault = acc.kind == core::DataKind::Pdl && !(facts && facts->duplex.valid) &&
                                  IsVerifiedEpsonDriver(job.driver);
        const Reliability reliability = queueDefault ? Reliability::Unknown : acc.duplex.reliability;
        out.duplex = Marked(reliability, text);
        const short duplex = reliability == Reliability::Unknown ? 0
                             : acc.duplex.value == DMDUP_VERTICAL || acc.duplex.value == DMDUP_HORIZONTAL
                                 ? static_cast<short>(acc.duplex.value)
                                 : static_cast<short>(DMDUP_SIMPLEX);
        out.binding = BindingText(orientation, duplex);
    }
    // Origen del papel: en los datos (ESC/Page "iuE") o en el DEVMODE (en RAW
    // este puede ser el predeterminado de la cola). Con el nombre del controlador.
    {
        const bool inDm = dm.defaultSource.valid;
        const bool inContent = facts && facts->sourceBin.valid;
        const bool agree = inDm && inContent && dm.defaultSource.value == facts->sourceBin.value;
        auto a = core::AssessSetting(acc.kind, inDm, inContent, documented, agree, dataRead);
        short bin = inContent && (documented || !inDm) ? facts->sourceBin.value : dm.defaultSource.value;
        out.source = Marked(a.reliability, inDm || inContent ? BinName(bin, JobDriver(job, facts), printer) : L"");
    }
    return out;
}

std::wstring BindingText(short orientation, short duplex) {
    if (duplex == DMDUP_SIMPLEX) return Tr(S::ValueBindingNone);
    if ((duplex != DMDUP_VERTICAL && duplex != DMDUP_HORIZONTAL) ||
        (orientation != DMORIENT_PORTRAIT && orientation != DMORIENT_LANDSCAPE))
        return L"—";
    const bool longEdge = duplex == DMDUP_VERTICAL;
    const bool portrait = orientation == DMORIENT_PORTRAIT;
    return Tr(longEdge == portrait ? S::ValueBindingBook : S::ValueBindingNotepad);
}

std::wstring PaperText(const core::PrintSettings& s, const core::PrinterInfo* printer) {
    // Papel propio del controlador: su nombre y sus medidas.
    if (s.paperSize.valid)
        if (const core::DriverPaper* driver = DriverPaperFor(s.paperSize.value, printer))
            return ShortDriverName(driver->name) + L" — " + SizeText(driver->width, driver->length);
    core::PaperSize size = core::EffectivePaper(s);
    const core::PaperInfo* info = s.paperSize.valid ? core::FindPaper(s.paperSize.value) : nullptr;
    std::wstring name;
    if (size.custom) name = Tr(S::ValueCustomPaper);
    else if (info) name = i18n::GetLanguage() == i18n::Language::Spanish ? info->nameEs : info->nameEn;
    else if (s.formName.valid) name = s.formName.value;
    else if (s.paperSize.valid) name = util::Format(Tr(S::ValuePaperCode), s.paperSize.value);
    if (name.empty() && !size.known) return Tr(S::ValueNotDeclared);
    if (size.known) name += (name.empty() ? L"" : L" — ") + SizeText(size.width, size.length);
    return name;
}

std::wstring OrientationText(short orientation) {
    return orientation == DMORIENT_LANDSCAPE ? Tr(S::ValueLandscape) : Tr(S::ValuePortrait);
}

std::wstring DuplexText(short duplex) {
    switch (duplex) {
        case DMDUP_VERTICAL: return Tr(S::ValueDuplexLong);
        case DMDUP_HORIZONTAL: return Tr(S::ValueDuplexShort);
        default: return Tr(S::ValueSimplex);
    }
}

std::wstring ReliabilityText(Reliability r) {
    switch (r) {
        case Reliability::Reliable: return Tr(S::BadgeReliable);
        case Reliability::Estimated: return Tr(S::BadgeEstimated);
        default: return Tr(S::BadgeUnknown);
    }
}

std::wstring ReasonText(Reason reason) {
    switch (reason) {
        case Reason::NoDevMode: return Tr(S::ReasonNoDevMode);
        case Reason::FieldNotDeclared: return Tr(S::ReasonFieldNotDeclared);
        case Reason::StillSpooling: return Tr(S::ReasonStillSpooling);
        case Reason::PagesUnknown: return Tr(S::ReasonPagesUnknown);
        case Reason::PossibleAppCopies: return Tr(S::ReasonPossibleAppCopies);
        case Reason::RepeatDetected: return Tr(S::ReasonRepeatDetected);
        case Reason::RawUnverified: return Tr(S::ReasonRawUnverified);
        case Reason::XpsTicketUnread: return Tr(S::ReasonXpsTicketUnread);
        case Reason::Conflict: return Tr(S::ReasonConflict);
        case Reason::UndocumentedHint: return Tr(S::ReasonUndocumentedHint);
        case Reason::HintAgrees: return Tr(S::ReasonHintAgrees);
        case Reason::DataNotRead: return Tr(S::ReasonDataNotRead);
        case Reason::NupPrivate: return Tr(S::ReasonNupPrivate);
        case Reason::KindUnknown: return Tr(S::ReasonKindUnknown);
        case Reason::DependsOnInputs: return Tr(S::ReasonDependsOnInputs);
        case Reason::PagesMismatch: return Tr(S::ReasonPagesMismatch);
        case Reason::BlankBackCounted: return Tr(S::ReasonBlankBackCounted);
        default: return L"";
    }
}

std::vector<ui::InfoRow> BuildJobSheet(const core::JobInfo& job, const core::PrinterInfo* printer,
                                       const preview::PreviewDocument* doc) {
    std::vector<ui::InfoRow> rows;
    const core::ContentFacts* facts = FactsOf(doc);
    bool fromSpool = false;
    const core::PrintSettings& dm = core::EffectiveDevMode(job, facts, &fromSpool);
    const core::PageAccounting acc = core::ComputePageAccounting(job, facts);
    const bool documented = facts && facts->settingsDocumented;
    // En EMF con DEVMODE en el spool, "DEVMODE" significa el del spool.
    auto fixBasis = [&](core::SettingAssessment a) {
        if (fromSpool && a.basis == Basis::DevMode) a.basis = Basis::SpoolDevMode;
        return a;
    };
    auto devModeOrigin = [&](const std::wstring& fields) {
        return util::Format(fromSpool ? Tr(S::OriginSpoolDevMode) : Tr(S::OriginDevMode), fields.c_str());
    };

    // --- Trabajo (JOB_INFO_2) ---
    rows.push_back(Section(S::SheetSectionJob));
    rows.push_back(Field(S::SheetId, util::Format(L"%u", job.id), L"JOB_INFO_2.JobId"));
    rows.push_back(Field(S::SheetDocument, job.document, L"JOB_INFO_2.pDocument"));
    rows.push_back(Field(S::SheetUser, job.user, L"JOB_INFO_2.pUserName"));
    rows.push_back(Field(S::SheetMachine, MachineName(job.machine), L"JOB_INFO_2.pMachineName"));
    core::PrinterInfo printerName;
    printerName.name = job.printer;
    rows.push_back(Field(S::SheetPrinter, printerName.DisplayName(), L"JOB_INFO_2.pPrinterName"));
    std::wstring state = JobStatusText(job);
    if (!job.statusText.empty()) state += L" — " + job.statusText;
    rows.push_back(Field(S::SheetStatus, state, util::Format(L"JOB_INFO_2.Status = 0x%08lX",
                                                            static_cast<unsigned long>(job.rawStatus))));
    rows.push_back(Field(S::SheetSubmitted, util::FormatTimeIso(job.submitted), L"JOB_INFO_2.Submitted"));
    rows.push_back(Field(S::SheetSize,
                         util::FormatBytes(job.sizeBytes) + util::Format(L" (%llu B)",
                                                                         static_cast<unsigned long long>(job.sizeBytes)),
                         Tr(S::OriginSize)));
    rows.push_back(Field(S::SheetDatatype, job.datatype.empty() ? Tr(S::ValueNotDeclared) : job.datatype,
                         Tr(S::OriginDatatype)));
    rows.push_back(Field(S::SheetDriver, job.driver, L"JOB_INFO_2.pDriverName"));
    std::wstring processor = job.printProcessor;
    if (!job.parameters.empty()) processor += L" (" + job.parameters + L")";
    rows.push_back(Field(S::SheetProcessor, processor, L"JOB_INFO_2.pPrintProcessor / pParameters"));
    rows.push_back(Field(S::SheetPriority, util::Format(Tr(S::DetailPosition), job.position, job.priority),
                         L"JOB_INFO_2.Position / Priority"));

    // --- Configuración solicitada ---
    rows.push_back(Section(S::SheetSectionSettings));
    if (!dm.present) {
        ui::InfoRow row = Field(S::SheetDevMode, Tr(S::ValueNoDevMode), Tr(S::ReasonNoDevMode));
        SetBadge(row, Reliability::Unknown);
        rows.push_back(row);
    }
    {
        core::PaperSize size = core::EffectivePaper(dm);
        const bool inDm = dm.paperSize.valid || dm.formName.valid || size.known;
        const bool inContent = facts && (facts->paperName.valid || facts->paperWidth.valid);
        const bool agree = inContent && SamePaper(size, *facts);
        auto a = fixBasis(core::AssessSetting(acc.kind, inDm, inContent, documented, agree, facts != nullptr));
        std::wstring contentText;
        if (inContent) {
            // Nombre legible si las medidas son de un tamaño estándar ("LT", "ISOA4" -> Carta, A4).
            const core::PaperInfo* known = facts->paperWidth.valid && facts->paperLength.valid
                                               ? core::FindPaperBySize(facts->paperWidth.value, facts->paperLength.value)
                                               : nullptr;
            if (known) contentText = i18n::GetLanguage() == i18n::Language::Spanish ? known->nameEs : known->nameEn;
            else if (facts->paperName.valid) contentText = facts->paperName.value;
            if (facts->paperWidth.valid && facts->paperLength.valid)
                contentText += (contentText.empty() ? L"" : L" — ") +
                               SizeText(facts->paperWidth.value, facts->paperLength.value);
        }
        std::wstring value = inDm ? PaperText(dm, printer) : std::wstring(Tr(S::ValueNotDeclared));
        std::wstring extra;
        if (inContent && !inDm) {
            value = contentText;
        } else if (inContent && !agree) {
            // Documentado (PJL/PrintTicket): manda lo que dicen los datos. Si no, solo se informa.
            if (documented) {
                extra = util::Format(Tr(S::NoteDevModeSays), value.c_str());
                value = contentText;
            } else {
                extra = util::Format(Tr(S::NoteContentSays), ContentSourceName(facts).c_str(), contentText.c_str());
            }
        }
        rows.push_back(SettingRow(S::SheetPaper, value, a, PaperFields(dm), facts, extra));
    }
    auto optionRow = [&](S label, const core::Field<short>& dmField, const core::Field<short>& contentField,
                         const wchar_t* fieldName, std::wstring (*text)(short)) {
        const bool inDm = dmField.valid;
        const bool inContent = facts && contentField.valid;
        const bool agree = inDm && inContent && dmField.value == contentField.value;
        auto a = fixBasis(core::AssessSetting(acc.kind, inDm, inContent, documented, agree, facts != nullptr));
        std::wstring value;
        if (inContent && (documented || !inDm)) value = text(contentField.value);
        else if (inDm) value = text(dmField.value);
        else value = Tr(S::ValueNotDeclared);
        std::wstring extra;
        if (inDm && inContent && !agree)
            extra = util::Format(Tr(S::NoteDevModeSays), text(dmField.value).c_str());
        rows.push_back(SettingRow(label, value, a, util::Format(L"%ls=%d", fieldName, dmField.value), facts, extra));
    };
    static const core::Field<short> kNone;
    optionRow(S::SheetOrientation, dm.orientation, facts ? facts->orientation : kNone, L"dmOrientation",
              &OrientationText);
    {
        // Doble cara: se usa el resultado de la contabilidad (incluye el supuesto "una cara").
        std::wstring detail = acc.duplex.basis == Basis::AssumedDefault
                                  ? std::wstring(Tr(S::OriginAssumed))
                                  : OriginText(acc.duplex.basis == Basis::DevMode && fromSpool ? Basis::SpoolDevMode
                                                                                               : acc.duplex.basis,
                                               util::Format(L"dmDuplex=%d", dm.duplex.value), facts);
        if (acc.duplexConflict)
            detail = Join(detail + L".", util::Format(Tr(S::NoteDevModeSays), DuplexText(dm.duplex.value).c_str()));
        if (acc.duplex.reason != Reason::None) detail = Join(detail + L".", ReasonText(acc.duplex.reason));
        ui::InfoRow row = Field(S::SheetDuplex, DuplexText(static_cast<short>(acc.duplex.value)), detail);
        SetBadge(row, acc.duplex.reliability);
        rows.push_back(row);
    }
    {
        std::wstring origin = OriginText(acc.copies.basis, util::Format(L"dmCopies=%d", dm.copies.value), facts);
        if (acc.copiesConflict && facts)
            origin = Join(origin + L".", util::Format(Tr(S::NoteCopiesConflict), dm.copies.value, facts->copies.value));
        rows.push_back(QuantityRow(S::SheetCopies, acc.copies, origin));
    }
    optionRow(S::SheetCollate, dm.collate, facts ? facts->collate : kNone, L"dmCollate", &CollateText);
    optionRow(S::SheetColor, dm.color, facts ? facts->color : kNone, L"dmColor", &ColorText);
    {
        // Origen del papel: datos (ESC/Page "iuE") o DEVMODE, con el nombre del controlador.
        const bool inDm = dm.defaultSource.valid;
        const bool inContent = facts && facts->sourceBin.valid;
        const bool agree = inDm && inContent && dm.defaultSource.value == facts->sourceBin.value;
        auto a = fixBasis(core::AssessSetting(acc.kind, inDm, inContent, documented, agree, facts != nullptr));
        if (inDm || inContent) {
            short bin = inContent && (documented || !inDm) ? facts->sourceBin.value : dm.defaultSource.value;
            std::wstring extra;
            if (inDm && inContent && !agree)
                extra = util::Format(Tr(S::NoteDevModeSays),
                                     BinName(dm.defaultSource.value, JobDriver(job, facts), printer).c_str());
            rows.push_back(SettingRow(S::SheetSource, BinName(bin, JobDriver(job, facts), printer), a,
                                      util::Format(L"dmDefaultSource=%d", dm.defaultSource.value), facts, extra));
        }
    }
    if (dm.printQuality.valid)
        rows.push_back(Field(S::SheetQuality, QualityText(dm.printQuality.value),
                             devModeOrigin(util::Format(L"dmPrintQuality=%d", dm.printQuality.value))));
    if (dm.scale.valid)
        rows.push_back(Field(S::SheetScale, util::Format(L"%d %%", dm.scale.value),
                             devModeOrigin(util::Format(L"dmScale=%d", dm.scale.value))));
    if (dm.mediaType.valid)
        rows.push_back(Field(S::SheetMedia, MediaText(dm.mediaType.value),
                             devModeOrigin(util::Format(L"dmMediaType=%lu",
                                                        static_cast<unsigned long>(dm.mediaType.value)))));
    rows.push_back(QuantityRow(S::SheetNup, acc.pagesPerSide,
                               acc.pagesPerSide.Known() ? OriginText(acc.pagesPerSide.basis, L"", facts) : L""));
    if (facts && !facts->rawSettings.empty())
        rows.push_back(Field(S::SheetInData, facts->rawSettings, ContentSourceName(facts)));
    if (facts && facts->sheetsCounter.valid)
        rows.push_back(Field(S::SheetDriverSheets, util::Format(L"%u", facts->sheetsCounter.value),
                             Tr(S::OriginDriverSheets)));
    if (acc.jobDevModeStale) {
        // JOB_INFO_2.pDevMode no es lo que se aplica: se muestra para que se vea la diferencia.
        std::wstring stale = PaperText(job.settings);
        if (job.settings.orientation.valid) stale += L" · " + OrientationText(job.settings.orientation.value);
        if (job.settings.copies.valid)
            stale += L" · " + util::Format(job.settings.copies.value == 1 ? Tr(S::SummaryCopy) : Tr(S::SummaryCopies),
                                           static_cast<unsigned>(job.settings.copies.value));
        rows.push_back(Field(S::SheetJobDevMode, util::Format(Tr(S::ValueJobDevModeStale), stale.c_str()),
                             Tr(S::DetailJobDevModeStale)));
    }
    if (facts && facts->spoolDevModeChanges)
        rows.push_back(Field(S::SheetDevModeChanges, Tr(S::ValueDevModeChanges), Tr(S::OriginSpoolDevModeRecords)));
    if (printer && printer->defaults.present && dm.present) {
        const bool same = core::SameVisibleSettings(dm, printer->defaults);
        rows.push_back(Field(S::SheetDefaults, same ? Tr(S::ValueDefaultsSame) : Tr(S::ValueDefaultsDiffer),
                             same ? Tr(S::DetailDefaultsSame) : Tr(S::OriginPrinterDevMode)));
    }
    if (dm.present)
        rows.push_back(Field(S::SheetDevMode,
                             util::Format(Tr(S::ValueDevModeTech), static_cast<unsigned long>(dm.fields), dm.size,
                                          dm.driverExtra, dm.specVersion, dm.driverVersion),
                             fromSpool ? Tr(S::OriginDevModeTechSpool) : Tr(S::OriginDevModeTech)));

    // --- Páginas, caras y hojas ---
    rows.push_back(Section(S::SheetSectionPages));
    {
        std::wstring origin = OriginText(acc.dataPages.basis, L"", facts);
        if (acc.pagesMismatch && facts)
            origin = Join(origin + L".", util::Format(Tr(S::NotePagesMismatch), acc.spoolerTotalPages, facts->pages));
        rows.push_back(QuantityRow(S::SheetDataPages, acc.dataPages, origin));
    }
    rows.push_back(QuantityRow(S::SheetLogical, acc.logicalPages, OriginText(acc.logicalPages.basis, L"", facts)));
    {
        std::wstring origin;
        if (acc.printPages.Known())
            origin = util::Format(Tr(S::OriginCalcPrintPages), acc.dataPages.value, acc.deviceCopies.value);
        rows.push_back(QuantityRow(S::SheetPrintPages, acc.printPages, origin));
    }
    if (acc.appCopies.Known() && acc.appCopies.value > 1)
        rows.push_back(QuantityRow(S::SheetAppCopies, acc.appCopies, OriginText(acc.appCopies.basis, L"", facts)));
    {
        std::wstring origin;
        if (acc.sides.basis == Basis::DriverSheets)
            origin = Tr(S::OriginSidesFromSheets);
        else if (acc.sides.Known() && acc.copies.value > 0)
            origin = acc.sides.basis == Basis::SinglePage
                         ? std::wstring(Tr(S::OriginSinglePage))
                         : util::Format(Tr(S::OriginCalcSides), acc.sides.value / acc.copies.value, acc.copies.value);
        rows.push_back(QuantityRow(S::SheetSides, acc.sides, origin));
    }
    {
        std::wstring origin;
        if (acc.sheets.basis == Basis::DriverSheets) {
            origin = Tr(S::OriginDriverSheets);
            if (acc.sheetsMismatch) origin += L". " + util::Format(Tr(S::NoteSheetsMismatch), acc.calculatedSheets);
        } else if (acc.sheets.Known() && acc.copies.value > 0) {
            origin = util::Format(Tr(S::OriginCalcSheets), acc.sheets.value / acc.copies.value, acc.copies.value,
                                  DuplexText(static_cast<short>(acc.duplex.value)).c_str());
            if (acc.sheetsMismatch && facts)
                origin += L". " + util::Format(Tr(S::NoteDriverSheetsHint), facts->sheetsCounter.value);
        }
        rows.push_back(QuantityRow(S::SheetSheets, acc.sheets, origin));
    }
    rows.push_back(Field(S::SheetProgress, util::Format(Tr(S::ValueProgress), job.pagesPrinted, job.totalPages),
                         Tr(S::NoteProgress)));

    // --- Datos y vista previa ---
    rows.push_back(Section(S::SheetSectionData));
    if (doc && doc->facts.analyzed) {
        rows.push_back(Field(S::SheetFormat, doc->facts.language,
                             doc->facts.complete ? Tr(S::OriginFormatDetected) : Tr(S::NoteDataTruncated)));
        rows.push_back(Field(S::SheetDataOrigin,
                             doc->dataOrigin == L"api" ? std::wstring(Tr(S::ValueReadApi))
                                                       : util::Format(Tr(S::ValueReadFile), doc->dataOrigin.c_str()),
                             util::FormatBytes(doc->dataBytes)));
    } else {
        rows.push_back(Field(S::SheetFormat, doc ? Tr(S::ValueDataNotRead) : Tr(S::PreviewLoading), L""));
    }
    if (doc) {
        std::wstring preview = doc->HasPages()
                                   ? util::Format(Tr(S::ValuePreviewYes), static_cast<unsigned>(doc->pages.size()))
                                   : std::wstring(Tr(S::ValuePreviewNo));
        std::wstring detail = doc->HasPages() ? doc->source : doc->message;
        // El mensaje puede traer la lista de vías probadas: en la ficha basta la primera línea.
        size_t nl = detail.find(L'\n');
        if (nl != std::wstring::npos) detail = detail.substr(0, nl);
        rows.push_back(Field(S::SheetPreview, preview, detail));
    }
    // Lo que más se consulta primero: la sección "Trabajo" (casi todo ya está
    // en el encabezado del panel) pasa al final.
    auto second = std::find_if(rows.begin() + 1, rows.end(),
                               [](const ui::InfoRow& r) { return r.kind == ui::InfoRow::Kind::Section; });
    std::rotate(rows.begin(), second, rows.end());
    return rows;
}

std::wstring SettingsSummary(const core::JobInfo& job, const preview::PreviewDocument* doc) {
    const core::ContentFacts* facts = FactsOf(doc);
    const core::PageAccounting acc = core::ComputePageAccounting(job, facts);
    const core::PrintSettings& dm = core::EffectiveDevMode(job, facts);
    // Sin marcas de estimado: la fiabilidad se ve en la pestaña Configuración.
    auto mark = [](const core::Quantity&) { return L""; };
    std::vector<std::wstring> parts;
    // Si los datos declaran el papel con una sintaxis documentada y no coincide, mandan los datos
    // (igual que en la ficha).
    const core::PaperInfo* contentPaper =
        facts && facts->settingsDocumented && facts->paperWidth.valid && facts->paperLength.valid
            ? core::FindPaperBySize(facts->paperWidth.value, facts->paperLength.value)
            : nullptr;
    if (contentPaper && !SamePaper(core::EffectivePaper(dm), *facts)) {
        parts.push_back(i18n::GetLanguage() == i18n::Language::Spanish ? contentPaper->nameEs : contentPaper->nameEn);
    } else if (dm.paperSize.valid || dm.formName.valid) {
        core::PaperSize size = core::EffectivePaper(dm);
        const core::PaperInfo* info = dm.paperSize.valid ? core::FindPaper(dm.paperSize.value) : nullptr;
        if (size.custom) parts.push_back(Tr(S::ValueCustomPaper));
        else if (info) parts.push_back(i18n::GetLanguage() == i18n::Language::Spanish ? info->nameEs : info->nameEn);
        else if (dm.formName.valid) parts.push_back(dm.formName.value);
    }
    if (dm.orientation.valid) parts.push_back(OrientationText(dm.orientation.value));
    parts.push_back(DuplexText(static_cast<short>(acc.duplex.value)));
    if (acc.copies.Known())
        parts.push_back(mark(acc.copies) + util::Format(acc.copies.value == 1 ? Tr(S::SummaryCopy) : Tr(S::SummaryCopies),
                                                        acc.copies.value));
    // Lo mismo que la columna "Páginas" de la lista: páginas x copias.
    if (acc.printPages.Known())
        parts.push_back(mark(acc.printPages) + util::Format(Tr(S::SummaryPrintPages), acc.printPages.value));
    else if (acc.sides.Known())
        parts.push_back(mark(acc.sides) + util::Format(acc.sides.value == 1 ? Tr(S::SummarySide) : Tr(S::SummarySides),
                                                       acc.sides.value));
    else
        parts.push_back(Tr(S::SummarySidesUnknown));
    std::wstring out;
    for (const auto& p : parts) out += (out.empty() ? L"" : L" · ") + p;
    return out;
}

}  // namespace cola::app
