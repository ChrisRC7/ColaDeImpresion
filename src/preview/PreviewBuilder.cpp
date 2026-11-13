// Construcción de la vista previa de un trabajo y análisis de sus datos.
// Ver PreviewBuilder.h para el orden de las fuentes y los formatos.
// Todo corre en hilos de fondo (PreviewService; AnalyzeSpoolData también en
// JobFactsService). Los textos para el operador salen de i18n (Tr).
#include "preview/PreviewBuilder.h"

#include <windows.h>
#include <winspool.h>

#include <algorithm>

#include "core/JobStatus.h"
#include "i18n/Strings.h"
#include "preview/EmfSpool.h"
#include "preview/ImageLoader.h"
#include "preview/PdlSettings.h"
#include "preview/SpoolFile.h"
#include "preview/TempFiles.h"
#include "preview/XpsPackage.h"
#include "preview/ZipReader.h"
#include "util/Log.h"
#include "util/Strings.h"

namespace cola::preview {

using i18n::S;
using i18n::Tr;

std::wstring PreviewRequest::Key() const {
    // El estado "recibiendo" forma parte de la clave: al terminar la recepción
    // se vuelve a generar (antes no se leen los datos).
    const unsigned spooling = (job.rawStatus & JOB_STATUS_SPOOLING) ? 1u : 0u;
    // Igual con "se puede leer": un trabajo que se vuelve a pausar se lee entonces.
    const unsigned readable = core::CanReadJobData(job.rawStatus) ? 1u : 0u;
    // El tamaño y TotalPages también entran: si el trabajo cambia, la vista
    // previa guardada ya no corresponde.
    return util::Format(L"%ls|%ls|%u|%llu|%u|%u|%u", server.c_str(), job.printer.c_str(), job.id,
                        static_cast<unsigned long long>(job.sizeBytes), job.totalPages, spooling, readable);
}

namespace {

// Lista de vías probadas, acortada para que quepa en el panel.
// Cada línea "ruta — error" se recorta por el medio a 90 caracteres (el
// principio y el final de la ruta son lo que más informa). Líneas vacías fuera.
std::wstring ShortDiagnostics(const std::wstring& diagnostics) {
    std::wstring out;
    for (const auto& line : util::Split(diagnostics, L'\n')) {
        if (line.empty()) continue;
        if (!out.empty()) out += L"\n";
        out += util::EllipsisMiddle(line, 90);
    }
    return out;
}

// Nombre de equipo sin las barras iniciales ("\\PC" -> "PC").
std::wstring BareMachineName(const std::wstring& machine) {
    const size_t start = machine.find_first_not_of(L'\\');
    return start == std::wstring::npos ? std::wstring() : machine.substr(start);
}

// Nombre NetBIOS de este equipo (el mismo formato de JOB_INFO_2.pMachineName
// sin las barras). "" si no se pudo obtener: entonces no se usa el respaldo.
std::wstring ThisMachineName() {
    wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    return GetComputerNameW(name, &size) ? std::wstring(name, size) : std::wstring();
}

// Clasificación gruesa que usa core::PageAccounting para decidir de dónde
// salen páginas y copias (D24): EMF y XPS tienen su propia lógica; todo lo
// demás son datos ya generados para la impresora (PDL).
core::DataKind KindOf(DataFormat format) {
    switch (format) {
        case DataFormat::EmfSpool: return core::DataKind::Emf;
        case DataFormat::Xps: return core::DataKind::Xps;
        case DataFormat::Unknown: return core::DataKind::Unknown;
        default: return core::DataKind::Pdl;  // PCL, PS, PDF, ESC/Page, texto... ya generados para la impresora
    }
}

}  // namespace

bool CanUseOriginalFile(const std::wstring& document, const std::wstring& jobMachine,
                        const std::wstring& thisMachine) {
    // Solo "X:\algo": letra de unidad, dos puntos y barra. Una ruta UNC
    // ("\\equipo\recurso\...") nunca: abrirla conecta esta PC por SMB al
    // equipo que eligió quien imprime.
    const bool drivePath = document.size() > 3 && document[1] == L':' && document[2] == L'\\' &&
                           ((document[0] >= L'A' && document[0] <= L'Z') || (document[0] >= L'a' && document[0] <= L'z'));
    if (!drivePath) return false;
    // El trabajo tiene que haberse enviado desde este mismo equipo: en otra
    // PC esa ruta es otro archivo (o ninguno).
    const std::wstring origin = BareMachineName(jobMachine);
    const std::wstring local = BareMachineName(thisMachine);
    return !origin.empty() && !local.empty() && util::EqualsInsensitive(origin, local);
}

void AnalyzeSpoolData(const SpoolData& spool, core::ContentFacts& facts) {
    facts.analyzed = true;
    // "complete" = se tienen todos los bytes: solo entonces valen los
    // conteos de páginas y la detección de repeticiones.
    facts.complete = !spool.truncated;
    facts.kind = KindOf(spool.format);
    facts.language = DataFormatName(spool.format);
    const std::uint8_t* data = spool.bytes.data();
    const size_t size = spool.bytes.size();
    switch (spool.format) {
        case DataFormat::EmfSpool: {
            // Páginas = metarchivos EMF completos (maxPages = 0: solo contar,
            // sin copiar bytes) y una huella por página para detectar copias
            // hechas por la aplicación repitiendo páginas (D24).
            size_t found = 0;
            std::vector<std::uint64_t> fingerprints;
            ExtractEmfPages(data, size, 0, &found, &fingerprints);
            facts.pageSource = core::ContentFacts::PageSource::EmfRecords;
            facts.pages = static_cast<std::uint32_t>(found);
            // DEVMODE efectivo dentro del spool (el que aplica el procesador de impresión).
            // Manda sobre JOB_INFO_2.pDevMode (D23).
            EmfSpoolDevMode spoolDevMode = ReadEmfSpoolDevMode(data, size);
            if (!spoolDevMode.firstPage.empty())
                facts.spoolDevMode = core::ParseDevMode(
                    reinterpret_cast<const DEVMODEW*>(spoolDevMode.firstPage.data()), spoolDevMode.firstPage.size());
            facts.spoolDevModeChanges = spoolDevMode.changesBetweenPages;
            // Con datos recortados faltan páginas: no se puede saber si se repiten.
            if (facts.complete) {
                facts.repeatChecked = true;
                facts.repeatPeriod = core::RepeatPeriod(fingerprints);
            }
            break;
        }
        case DataFormat::Xps: {
            // PrintTicket y páginas con el XPS Object Model de Windows (D25).
            std::wstring error;
            if (!AnalyzeXpsPackage(data, size, facts, &error)) {
                // Paquete que el XPS OM no abre (incompleto, piezas...): al menos las páginas.
                util::LogError(L"No se pudo analizar el paquete XPS: %ls", error.c_str());
                facts.pageSource = core::ContentFacts::PageSource::XpsFixedPages;
                facts.pages = CountXpsPages(data, size);
            }
            // Repeticiones: solo si hay una huella por cada página contada
            // (si no, las listas no son comparables).
            if (facts.complete) {
                auto fingerprints = XpsPageFingerprints(data, size);
                if (!fingerprints.empty() && fingerprints.size() == facts.pages) {
                    facts.repeatChecked = true;
                    facts.repeatPeriod = core::RepeatPeriod(fingerprints);
                }
            }
            break;
        }
        case DataFormat::Pcl:
        case DataFormat::PclXl:
        case DataFormat::PostScript:
        case DataFormat::Pdf:
        case DataFormat::EscPage:
        case DataFormat::EscPr:
        case DataFormat::Unknown:
            // Lenguajes de impresora: opciones PJL/EJL/DSC y, en ESC/Page, el
            // conteo de páginas hecho sobre todos los datos si lo hay (D53).
            // Unknown también pasa por aquí por si trae un encabezado PJL/EJL.
            ParsePdlSettings(data, size, facts.complete, facts,
                             spool.escPageAllData ? &*spool.escPageAllData : nullptr);
            break;
        default: break;  // Image / Text: no declaran opciones
    }
}

namespace {

// Vista previa vectorial de un spool EMF: hasta maxPages páginas EmfPage.
// totalPages queda en el mayor entre lo que dice el spooler y lo contado.
void FromEmf(const SpoolData& spool, const PreviewOptions& opt, PreviewDocument& doc) {
    size_t found = 0;
    auto emfs = ExtractEmfPages(spool.bytes.data(), spool.bytes.size(), static_cast<size_t>(opt.maxPages), &found);
    for (auto& bytes : emfs) doc.pages.push_back(std::make_shared<EmfPage>(std::move(bytes)));
    doc.totalPages = std::max<std::uint32_t>(doc.totalPages, static_cast<std::uint32_t>(found));
}

// Vista previa rasterizada con el intérprete externo del formato (páginas 1
// a maxPages). Sin intérprete, o si falla, deja el motivo en doc.message.
void FromExternal(const SpoolData& spool, const PreviewOptions& opt, PreviewDocument& doc) {
    const wchar_t* formatName = DataFormatName(spool.format);
    if (RendererFor(spool.format, opt.renderers).empty()) {
        doc.message = util::Format(Tr(S::PreviewNeedsRenderer), formatName);
        return;
    }
    auto result = RenderExternal(spool.format, spool.bytes.data(), spool.bytes.size(), opt.renderers, opt.rasterDpi,
                                 1, opt.maxPages);
    doc.pages = std::move(result.pages);
    if (doc.pages.empty()) doc.message = util::Format(Tr(S::PreviewRenderFailed), result.error.c_str());
}

// XPS sin GhostXPS: intentamos la miniatura del Shell sobre una copia temporal.
void FromShellTemp(const SpoolData& spool, const wchar_t* extension, PreviewDocument& doc) {
    // Copia temporal en la carpeta privada del proceso; se borra enseguida.
    std::wstring path = NewTempPath(extension);
    if (path.empty()) return;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    // Una sola escritura (máximo 2 GB): para una miniatura basta; si quedara
    // incompleta, el proveedor simplemente no devuelve imagen.
    DWORD written = 0;
    WriteFile(file, spool.bytes.data(), static_cast<DWORD>(std::min<size_t>(spool.bytes.size(), 0x7fffffff)), &written,
              nullptr);
    CloseHandle(file);
    // Miniatura de 1024 px: con ella se reemplaza el mensaje de "falta
    // intérprete" que dejó FromExternal.
    if (auto page = LoadShellThumbnail(path, 1024)) {
        doc.pages.push_back(page);
        doc.source = Tr(S::PreviewSourceThumbnail);
        doc.message.clear();
    }
    DeleteFileW(path.c_str());
}

// Respaldo con el archivo original del estudiante (cuando el nombre del
// documento es una ruta accesible desde este equipo). Una imagen se carga
// completa (hasta 2400 px por lado); cualquier otro tipo, solo su miniatura.
void FromOriginalFile(const std::wstring& path, const PreviewOptions& opt, PreviewDocument& doc) {
    if (IsImageExtension(path)) {
        std::uint32_t frames = 0;
        doc.pages = LoadImageFile(path, static_cast<size_t>(opt.maxPages), 2400, &frames);
        if (!doc.pages.empty()) {
            doc.format = DataFormat::Image;
            doc.source = Tr(S::PreviewSourceOriginal);
            doc.totalPages = std::max(doc.totalPages, frames);
            return;
        }
    }
    if (auto page = LoadShellThumbnail(path, 1024)) {
        doc.pages.push_back(page);
        doc.source = Tr(S::PreviewSourceThumbnail);
    }
}

}  // namespace

DocumentPtr BuildPreview(const PreviewRequest& request) {
    auto doc = std::make_shared<PreviewDocument>();
    const auto& opt = request.options;
    // Punto de partida: lo que informa el spooler (TotalPages); se ajusta
    // hacia arriba con lo que se cuente en los datos.
    doc->totalPages = request.job.totalPages;

    // 1. Datos del trabajo. Cada rama de abajo corresponde a un resultado
    //    distinto de la lectura, con su mensaje para el operador.
    SpoolSearch search{request.server, request.job.printer, opt.spoolOverrideDir};
    SpoolData spool = ReadJobSpool(search, request.job, opt.maxSpoolBytes);
    if (spool.stillSpooling) {
        doc->message = Tr(S::PreviewStillSpooling);
    } else if (spool.notHeld) {
        doc->message = Tr(S::PreviewNotHeld);
    } else if (spool.incomplete) {
        // builtTick permite que la caché olvide este resultado pasado un rato
        // y se vuelva a intentar (D48).
        doc->message = Tr(S::PreviewReadIncomplete);
        doc->readIncomplete = true;
        doc->builtTick = GetTickCount64();
    } else if (spool.error == 0 && spool.bytes.empty()) {
        // Los datos existen pero todavía están vacíos: el trabajo sigue llegando.
        doc->message = Tr(S::PreviewSpoolEmpty);
    } else if (spool.error == 0) {
        // Datos leídos: se analizan y luego se dibujan según el formato.
        doc->format = spool.format;
        doc->dataBytes = spool.fileSize;
        doc->dataOrigin = spool.origin == SpoolOrigin::SpoolerApi ? L"api" : spool.path;
        doc->source = util::Format(spool.origin == SpoolOrigin::SpoolerApi ? Tr(S::PreviewSourceApi)
                                                                            : Tr(S::PreviewSourceSpool),
                                   DataFormatName(spool.format));
        AnalyzeSpoolData(spool, doc->facts);
        switch (spool.format) {
            case DataFormat::EmfSpool: FromEmf(spool, opt, *doc); break;
            case DataFormat::Xps:
                doc->totalPages = std::max(doc->totalPages, doc->facts.pages);
                FromExternal(spool, opt, *doc);
                if (doc->pages.empty()) FromShellTemp(spool, L".xps", *doc);
                break;
            case DataFormat::Pdf:
                FromExternal(spool, opt, *doc);
                if (doc->pages.empty()) FromShellTemp(spool, L".pdf", *doc);
                break;
            case DataFormat::PostScript:
            case DataFormat::Pcl:
            case DataFormat::PclXl: FromExternal(spool, opt, *doc); break;
            case DataFormat::Image: {
                std::uint32_t frames = 0;
                doc->pages = LoadImageMemory(spool.bytes.data(), spool.bytes.size(),
                                             static_cast<size_t>(opt.maxPages), 2400, &frames);
                doc->totalPages = std::max(doc->totalPages, frames);
                break;
            }
            case DataFormat::EscPage:
            case DataFormat::EscPr:
                // Lenguajes propios de Epson: no hay intérprete; no se intenta "adivinar".
                doc->message = util::Format(Tr(S::PreviewNotAvailableFormat), DataFormatName(spool.format));
                break;
            case DataFormat::Unknown: doc->message = Tr(S::PreviewNotAvailableUnknown); break;
            default:
                doc->message = util::Format(Tr(S::PreviewUnsupported), DataFormatName(spool.format));
                break;
        }
        if (spool.truncated) doc->truncated = true;
    } else {
        // No se pudo leer: acceso denegado tiene su propio texto; cualquier
        // otro error se presenta como "no se encontró el spool".
        doc->message = spool.error == ERROR_ACCESS_DENIED ? std::wstring(Tr(S::PreviewAccessDenied))
                                                          : util::Format(Tr(S::PreviewSpoolMissing), request.job.id);
        // Se indica qué se intentó y por qué falló cada vía.
        if (!spool.diagnostics.empty())
            doc->message += L"\n\n" + std::wstring(Tr(S::PreviewSearchedIn)) + L"\n" +
                            ShortDiagnostics(spool.diagnostics);
    }

    // Respaldo: el archivo original. El nombre del documento lo pone quien
    // imprime, así que solo se toca el sistema de archivos si es una ruta de
    // unidad local Y el trabajo salió de este equipo (F5): antes una ruta UNC
    // hacía que esta PC se conectara y autenticara contra el equipo indicado,
    // y una ruta "C:\..." de otra PC mostraba un archivo del operador.
    if (doc->pages.empty() && CanUseOriginalFile(request.job.document, request.job.machine, ThisMachineName()) &&
        GetFileAttributesW(request.job.document.c_str()) != INVALID_FILE_ATTRIBUTES) {
        FromOriginalFile(request.job.document, opt, *doc);
        if (!doc->pages.empty()) doc->message.clear();
    }

    // Ajustes finales: el total nunca es menor que lo preparado, y la vista
    // es "parcial" si se llegó al tope de páginas y quedan más.
    doc->totalPages = std::max<std::uint32_t>(doc->totalPages, static_cast<std::uint32_t>(doc->pages.size()));
    if (doc->pages.size() < doc->totalPages && !doc->pages.empty() &&
        doc->pages.size() >= static_cast<size_t>(opt.maxPages))
        doc->truncated = true;
    return doc;
}

}  // namespace cola::preview
