#include "core/PageAccounting.h"

#include <winspool.h>

#include <algorithm>

#include "util/Strings.h"

namespace cola::core {

namespace {

int Rank(Reliability r) {
    switch (r) {
        case Reliability::Reliable: return 2;
        case Reliability::Estimated: return 1;
        default: return 0;
    }
}

Quantity Make(std::uint32_t value, Reliability reliability, Basis basis, Reason reason = Reason::None) {
    Quantity q;
    q.value = value;
    q.reliability = reliability;
    q.basis = basis;
    q.reason = reason;
    return q;
}

Quantity Unknown(Reason reason) { return Make(0, Reliability::Unknown, Basis::None, reason); }

std::uint32_t CeilDiv(std::uint32_t a, std::uint32_t b) { return b == 0 ? a : (a + b - 1) / b; }

// Copias según el DEVMODE y los datos, antes de considerar repeticiones de la aplicación.
Quantity ResolveCopies(DataKind kind, const PrintSettings& dm, bool fromSpool, const ContentFacts* facts,
                       bool* conflict) {
    const bool dmValid = dm.copies.valid;
    const std::uint32_t dmCopies = dmValid ? static_cast<std::uint32_t>(dm.copies.value) : 0;
    const bool hasContent = facts && facts->copies.valid && facts->copies.value > 0;
    const bool documented = hasContent && facts->settingsDocumented;
    const std::uint32_t contentCopies = hasContent ? facts->copies.value : 0;
    *conflict = dmValid && documented && dmCopies != contentCopies;

    if (!dm.present && !hasContent) return Unknown(Reason::NoDevMode);

    switch (kind) {
        case DataKind::Emf:
            // El procesador de impresión reproduce el EMF aplicando el DEVMODE del spool. Sin leer
            // los datos solo se tiene el de JOB_INFO_2, que puede estar desactualizado (Word).
            if (dmValid && !facts) return Make(dmCopies, Reliability::Estimated, Basis::DevMode, Reason::DataNotRead);
            if (dmValid) return Make(dmCopies, Reliability::Reliable, fromSpool ? Basis::SpoolDevMode : Basis::DevMode);
            return Make(1, Reliability::Estimated, Basis::AssumedDefault, Reason::FieldNotDeclared);
        case DataKind::Xps:
            // El PrintTicket es lo que aplica el camino XPS: manda aunque el DEVMODE diga otra cosa.
            if (documented)
                return Make(contentCopies, Reliability::Reliable,
                            dmValid && !*conflict ? Basis::DevModeAndContent : Basis::Content);
            if (dmValid) return Make(dmCopies, Reliability::Estimated, Basis::DevMode, Reason::XpsTicketUnread);
            return Make(1, Reliability::Estimated, Basis::AssumedDefault, Reason::FieldNotDeclared);
        case DataKind::Pdl:
            // RAW: el controlador ya generó los datos en el cliente; la impresora obedece lo que dicen los datos.
            // La impresora obedece a los datos. En los trabajos que llegan por Client Side
            // Rendering el DEVMODE del servidor resultó ser el predeterminado de la cola.
            if (documented)
                return Make(contentCopies, Reliability::Reliable,
                            dmValid && !*conflict ? Basis::DevModeAndContent : Basis::Content);
            if (dmValid) {
                Reason reason = hasContent ? Reason::UndocumentedHint : Reason::RawUnverified;
                return Make(dmCopies, Reliability::Estimated, Basis::DevMode, reason);
            }
            if (hasContent) return Make(contentCopies, Reliability::Estimated, Basis::Content, Reason::UndocumentedHint);
            return Unknown(Reason::RawUnverified);
        default:
            if (dmValid) return Make(dmCopies, Reliability::Estimated, Basis::DevMode, Reason::KindUnknown);
            return Unknown(Reason::KindUnknown);
    }
}

Quantity ResolveDuplex(DataKind kind, const PrintSettings& dm, const ContentFacts* facts, bool* conflict) {
    const bool dmValid = dm.duplex.valid;
    const bool hasContent = facts && facts->duplex.valid;
    const bool documented = hasContent && facts->settingsDocumented;
    *conflict = dmValid && documented && dm.duplex.value != facts->duplex.value;
    const bool agree = dmValid && hasContent && dm.duplex.value == facts->duplex.value;
    SettingAssessment a = AssessSetting(kind, dmValid, hasContent, documented, agree, facts != nullptr);
    if (a.reliability == Reliability::Unknown && !dmValid && !hasContent) {
        // Sin DM_DUPLEX el controlador normalmente no ofrece doble cara: se supone una cara.
        return Make(DMDUP_SIMPLEX, Reliability::Estimated, Basis::AssumedDefault,
                    dm.present ? Reason::FieldNotDeclared : Reason::NoDevMode);
    }
    // Si hay datos que lo declaran, la impresora obedece a los datos.
    std::uint32_t value = hasContent && (documented || !dmValid) ? static_cast<std::uint32_t>(facts->duplex.value)
                                                                 : static_cast<std::uint32_t>(dm.duplex.value);
    return Make(value, a.reliability, a.basis, a.reason);
}

// Hojas físicas que declaró el controlador (EJL "JI PAGES"). Con un controlador
// verificado es el dato más directo: ya incluye copias, doble cara y N-up.
void ApplyDriverSheets(PageAccounting& acc, const ContentFacts* facts) {
    if (!facts || !facts->sheetsCounter.valid || facts->sheetsCounter.value == 0) return;
    const std::uint32_t counter = facts->sheetsCounter.value;
    acc.calculatedSheets = acc.sheets.Known() ? acc.sheets.value : 0;
    acc.sheetsMismatch = acc.sheets.Known() && acc.sheets.value != counter;
    if (!facts->settingsDocumented) return;  // solo indicio: se deja el cálculo
    acc.sheets = Make(counter, Reliability::Reliable, Basis::DriverSheets);
    const bool simplex = acc.duplex.value == DMDUP_SIMPLEX && acc.duplex.reliability == Reliability::Reliable;
    if (simplex) {
        acc.sides = Make(counter, Reliability::Reliable, Basis::DriverSheets);  // una cara: caras = hojas
    } else if (!acc.sheetsMismatch && acc.sides.Known() && acc.copies.reliability == Reliability::Reliable &&
               acc.dataPages.reliability == Reliability::Reliable) {
        // El cálculo coincide con el controlador: las caras calculadas quedan confirmadas.
        acc.sides.reliability = Reliability::Reliable;
        acc.sides.reason = Reason::None;
    }
}

}  // namespace

Reliability Worst(Reliability a, Reliability b) { return Rank(a) <= Rank(b) ? a : b; }

const PrintSettings& EffectiveDevMode(const JobInfo& job, const ContentFacts* facts, bool* fromSpool) {
    const bool spool = facts && facts->analyzed && facts->kind == DataKind::Emf && facts->spoolDevMode.present;
    if (fromSpool) *fromSpool = spool;
    return spool ? facts->spoolDevMode : job.settings;
}

DataKind KindFromJob(const JobInfo& job) {
    const std::wstring dt = util::ToLower(job.datatype);
    const std::wstring pp = util::ToLower(job.printProcessor);
    if (dt.rfind(L"nt emf", 0) == 0) return DataKind::Emf;
    if (dt.find(L"xps") != std::wstring::npos || pp == L"ms_xps_proc") return DataKind::Xps;
    if (dt == L"raw" || dt.rfind(L"raw", 0) == 0 || dt == L"text") return DataKind::Pdl;
    return DataKind::Unknown;
}

SettingAssessment AssessSetting(DataKind kind, bool inDevMode, bool inContent, bool contentDocumented, bool agree,
                                bool dataRead) {
    SettingAssessment a;
    if (!inDevMode && !inContent) {
        a.reason = Reason::FieldNotDeclared;
        return a;
    }
    if (inDevMode && inContent && contentDocumented) {
        // Los datos (PrintTicket, PJL, DSC, EJL verificado) son lo que se aplica: mandan.
        a.basis = agree ? Basis::DevModeAndContent : Basis::Content;
        a.reliability = Reliability::Reliable;
        return a;
    }
    if (inContent && contentDocumented) {
        a.basis = Basis::Content;
        a.reliability = Reliability::Reliable;
        return a;
    }
    if (inContent && !inDevMode) {
        a.basis = Basis::Content;
        a.reliability = Reliability::Estimated;
        a.reason = Reason::UndocumentedHint;
        return a;
    }
    if (inContent && inDevMode && agree) {
        // DEVMODE e indicio no documentado coinciden: más confianza, pero sin confirmación formal.
        a.basis = Basis::DevModeAndContent;
        a.reliability = Reliability::Estimated;
        a.reason = Reason::HintAgrees;
        return a;
    }
    // Solo el DEVMODE (o el DEVMODE con un indicio no documentado).
    a.basis = Basis::DevMode;
    switch (kind) {
        case DataKind::Emf:
            a.reliability = dataRead ? Reliability::Reliable : Reliability::Estimated;
            if (!dataRead) a.reason = Reason::DataNotRead;
            break;
        case DataKind::Xps:
            a.reliability = Reliability::Estimated;
            a.reason = Reason::XpsTicketUnread;
            break;
        case DataKind::Pdl:
            a.reliability = Reliability::Estimated;
            a.reason = inContent ? Reason::UndocumentedHint : Reason::RawUnverified;
            break;
        default:
            a.reliability = Reliability::Estimated;
            a.reason = Reason::KindUnknown;
            break;
    }
    return a;
}

std::uint32_t RepeatPeriod(const std::vector<std::uint64_t>& hashes) {
    const size_t n = hashes.size();
    for (size_t period = 1; period <= n / 2; ++period) {
        if (n % period != 0) continue;
        bool repeats = true;
        for (size_t i = period; i < n && repeats; ++i) repeats = hashes[i] == hashes[i - period];
        if (repeats) return static_cast<std::uint32_t>(period);
    }
    return 0;
}

PageAccounting ComputePageAccounting(const JobInfo& job, const ContentFacts* factsIn) {
    PageAccounting acc;
    const ContentFacts* facts = factsIn && factsIn->analyzed ? factsIn : nullptr;
    acc.kind = facts && facts->kind != DataKind::Unknown ? facts->kind : KindFromJob(job);
    acc.spoolerTotalPages = job.totalPages;
    acc.pagesPrinted = job.pagesPrinted;
    const bool spooling = (job.rawStatus & JOB_STATUS_SPOOLING) != 0;
    const PrintSettings& dm = EffectiveDevMode(job, facts, &acc.devModeFromSpool);
    acc.jobDevModeStale = acc.devModeFromSpool && job.settings.present && !SameVisibleSettings(job.settings, dm);

    // 1. Páginas en los datos (tal como las envió la aplicación).
    const bool contentCounted = facts && (facts->complete || facts->pagesFromAllData) && facts->pages > 0 &&
                                (facts->pageSource == ContentFacts::PageSource::EmfRecords ||
                                 facts->pageSource == ContentFacts::PageSource::XpsFixedPages ||
                                 facts->pageSource == ContentFacts::PageSource::PostScriptDsc ||
                                 facts->pageSource == ContentFacts::PageSource::EscPageFormFeeds);
    if (contentCounted) {
        acc.dataPages = Make(facts->pages, Reliability::Reliable, Basis::ContentPages);
        acc.pagesMismatch = job.totalPages != 0 && job.totalPages != facts->pages && !spooling;
    } else if (job.totalPages > 1 && job.totalPages % 2 == 0 && !spooling && facts && facts->spoolerCountsBlankBack) {
        // Puede ser una página menos (impar + dorso en blanco): no se puede saber sin contarlas.
        acc.dataPages = Make(job.totalPages, Reliability::Estimated, Basis::SpoolerTotalPages,
                             Reason::BlankBackCounted);
    } else if (job.totalPages > 0 && !spooling) {
        acc.dataPages = Make(job.totalPages, Reliability::Reliable, Basis::SpoolerTotalPages);
    } else if (job.totalPages > 0) {
        acc.dataPages = Make(job.totalPages, Reliability::Estimated, Basis::SpoolerTotalPages, Reason::StillSpooling);
    } else {
        acc.dataPages = Unknown(spooling ? Reason::StillSpooling : Reason::PagesUnknown);
    }

    // 2. Páginas lógicas (una copia) y repeticiones hechas por la aplicación.
    acc.appCopies = Make(1, Reliability::Reliable, Basis::None);
    const std::uint32_t n = acc.dataPages.value;
    const bool repeat = acc.dataPages.Known() && facts && facts->repeatPeriod > 0 && n % facts->repeatPeriod == 0 &&
                        n / facts->repeatPeriod >= 2;
    if (!acc.dataPages.Known()) {
        acc.logicalPages = Unknown(acc.dataPages.reason);
        acc.appCopies = Unknown(acc.dataPages.reason);
    } else if (repeat) {
        acc.logicalPages = Make(facts->repeatPeriod, Reliability::Estimated, Basis::ContentRepeat, Reason::RepeatDetected);
        acc.appCopies = Make(n / facts->repeatPeriod, Reliability::Estimated, Basis::ContentRepeat, Reason::RepeatDetected);
    } else if (n == 1) {
        // Una sola página no puede contener copias repetidas.
        acc.logicalPages = Make(1, acc.dataPages.reliability, acc.dataPages.basis, acc.dataPages.reason);
    } else if (facts && facts->repeatChecked && contentCounted) {
        acc.logicalPages = Make(n, acc.dataPages.reliability, acc.dataPages.basis, acc.dataPages.reason);
    } else {
        acc.logicalPages = Make(n, Reliability::Estimated, acc.dataPages.basis, Reason::PossibleAppCopies);
        acc.appCopies = Make(1, Reliability::Estimated, Basis::AssumedDefault, Reason::PossibleAppCopies);
    }

    // 3. Copias solicitadas.
    acc.copies = ResolveCopies(acc.kind, dm, acc.devModeFromSpool, facts, &acc.copiesConflict);
    acc.deviceCopies = acc.copies;
    if (acc.dataPages.Known() && acc.deviceCopies.Known())
        acc.printPages = Make(acc.dataPages.value * acc.deviceCopies.value,
                              Worst(acc.dataPages.reliability, acc.deviceCopies.reliability), Basis::Calculated,
                              acc.dataPages.reliability != Reliability::Reliable ? acc.dataPages.reason
                                                                                  : acc.deviceCopies.reason);
    else
        acc.printPages = Unknown(!acc.dataPages.Known() ? acc.dataPages.reason : acc.deviceCopies.reason);
    if (acc.copies.Known() && repeat) {
        acc.copies = Make(acc.copies.value * acc.appCopies.value, Reliability::Estimated, Basis::ContentRepeat,
                          Reason::RepeatDetected);
    }

    // 4. Páginas por cara (N-up): no está en la parte pública del DEVMODE.
    if (facts && facts->pagesPerSheet.valid && facts->pagesPerSheet.value > 0) {
        acc.pagesPerSide = facts->settingsDocumented
                               ? Make(facts->pagesPerSheet.value, Reliability::Reliable, Basis::Content)
                               : Make(facts->pagesPerSheet.value, Reliability::Estimated, Basis::Content,
                                      Reason::UndocumentedHint);
    } else {
        acc.pagesPerSide = Unknown(Reason::NupPrivate);
    }

    // 5. Doble cara.
    acc.duplex = ResolveDuplex(acc.kind, dm, facts, &acc.duplexConflict);

    // 6. Caras físicas previstas.
    if (!acc.logicalPages.Known() || !acc.copies.Known()) {
        acc.sides = Unknown(!acc.logicalPages.Known() ? acc.logicalPages.reason : acc.copies.reason);
        acc.sheets = acc.sides;
        ApplyDriverSheets(acc, facts);  // el controlador puede declarar las hojas aunque no haya páginas
        return acc;
    }
    const std::uint32_t logical = acc.logicalPages.value;
    std::uint32_t perCopy = logical;
    Reliability sidesRel = Worst(acc.logicalPages.reliability, acc.copies.reliability);
    Basis sidesBasis = Basis::Calculated;
    if (logical == 1) {
        sidesBasis = Basis::SinglePage;  // 1 página ocupa 1 cara con cualquier N-up
    } else if (acc.pagesPerSide.Known()) {
        perCopy = CeilDiv(logical, acc.pagesPerSide.value);
        sidesRel = Worst(sidesRel, acc.pagesPerSide.reliability);
    } else {
        sidesRel = Worst(sidesRel, Reliability::Estimated);  // se supone 1 página por cara
    }
    Reason sidesReason = Reason::None;
    if (sidesRel != Reliability::Reliable)
        sidesReason = (logical > 1 && !acc.pagesPerSide.Known() &&
                       Worst(acc.logicalPages.reliability, acc.copies.reliability) == Reliability::Reliable)
                          ? Reason::NupPrivate
                          : Reason::DependsOnInputs;
    acc.sides = Make(perCopy * acc.copies.value, sidesRel, sidesBasis, sidesReason);

    // 7. Hojas físicas previstas (cada copia empieza en una hoja nueva).
    const bool twoSided = acc.duplex.value == DMDUP_VERTICAL || acc.duplex.value == DMDUP_HORIZONTAL;
    const std::uint32_t sheetsPerCopy = twoSided ? CeilDiv(perCopy, 2) : perCopy;
    Reliability sheetsRel = Worst(sidesRel, acc.duplex.reliability);
    acc.sheets = Make(sheetsPerCopy * acc.copies.value, sheetsRel, Basis::Calculated,
                      sheetsRel == Reliability::Reliable ? Reason::None
                                                         : (sidesRel == Reliability::Reliable ? acc.duplex.reason
                                                                                              : sidesReason));
    ApplyDriverSheets(acc, facts);
    return acc;
}

}  // namespace cola::core
