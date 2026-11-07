// Herramienta de diagnóstico y pruebas: muestra, por cada trabajo en cola, lo
// mismo que la ficha "Configuración" de la app (JOB_INFO_2, DEVMODE del
// trabajo, páginas / copias / caras / hojas con su fiabilidad) y, con --data,
// lee los datos con la API del spooler y evalúa la vista previa con el mismo
// código que la app. Sirve para la matriz de pruebas de docs/DATOS_DE_IMPRESION.md.
//
// Uso:
//   cola_jobinspect "Impresora" | "\\SERVIDOR" [opciones]
//     --data            lee los datos del trabajo (ReadPrinter) y evalúa la vista previa
//     --job N           solo el trabajo N
//     --hold S          vigila S segundos: pausa cada trabajo nuevo apenas aparece
//                       (para enviar desde Word/Acrobat/navegador sin que se imprima)
//                       y lo inspecciona cuando termina de recibirse
//     --csv ARCHIVO     agrega una fila por trabajo a ARCHIVO (matriz de pruebas)
//     --label TEXTO     texto libre para la columna "prueba" del CSV
//     --renderers DIR   carpeta con gswin64c/gpcl6win64/gxpswin64
//     --release         con --hold: reanuda cada trabajo después de inspeccionarlo (necesario
//                       si la aplicación imprime "a archivo": EndDoc espera a que termine)
//     --delete          cancela los trabajos inspeccionados al terminar (solo los propios)
//
// No guarda el contenido de los documentos en disco: solo metadatos.
//
// Advertencias (GUIA_IA §6):
//  - --hold pausa TODO trabajo nuevo de las colas vigiladas, también los de
//    estudiantes si se apunta al servidor real.
//  - --hold no comprueba la copia local de Client Side Rendering
//    (JOB_STATUS_RENDERING_LOCALLY): con el nombre de la conexión se vería y
//    pausaría esa copia (hojas en blanco, D39). Para un trabajo del servidor
//    usar el nombre DNS completo ("\\\\vtec-laimiprint.estudiantes.tec.cr\\Cola").
//  - Con "imprimir a archivo" un trabajo pausado bloquea EndDoc de la
//    aplicación: usar --release.
// Enlaza con los módulos de la app (core, preview, app/JobSheet) para que lo
// que muestra sea exactamente lo que vería el operador en la ficha.

#include <windows.h>
#include <objbase.h>
#include <winspool.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "app/JobSheet.h"
#include "core/PageAccounting.h"
#include "core/Spooler.h"
#include "i18n/Strings.h"
#include "preview/PreviewBuilder.h"
#include "util/Format.h"
#include "util/Strings.h"

using namespace cola;

namespace {

// Opciones de la línea de comandos (ver el uso al inicio del archivo).
struct Options {
    std::wstring target;     // impresora o "\\\\SERVIDOR" (todas sus colas)
    bool data = false;       // --data: leer y analizar los datos del trabajo
    std::uint32_t job = 0;   // --job: solo ese id (0 = todos)
    int holdSeconds = 0;     // --hold: segundos de vigilancia (0 = inspección única)
    std::wstring csv;        // --csv: archivo donde agregar filas
    std::wstring label;      // --label: columna "prueba" del CSV
    std::wstring renderers;  // --renderers: carpeta de Ghostscript/GhostPCL/GhostXPS
    bool del = false;        // --delete: cancelar los inspeccionados al final
    bool release = false;    // --release: reanudar cada trabajo tras inspeccionarlo
};

// Escribe en la consola en UTF-8 (wmain pone la página de códigos CP_UTF8) y
// vacía el búfer para que la salida aparezca enseguida durante --hold.
void Out(const std::wstring& text) {
    std::string utf8 = util::ToUtf8(text);
    fwrite(utf8.data(), 1, utf8.size(), stdout);
    fflush(stdout);
}

// Impresoras a revisar: si target es solo un servidor ("\\\\SRV", sin otra
// barra) todas sus colas; si no, la impresora indicada.
std::vector<std::wstring> PrintersOf(const std::wstring& target) {
    if (target.size() > 2 && target.rfind(L"\\\\", 0) == 0 && target.find(L'\\', 2) == std::wstring::npos) {
        std::vector<std::wstring> names;
        for (const auto& p : core::spooler::EnumeratePrinters(target)) names.push_back(p.name);
        return names;
    }
    return {target};
}

// Trabajos actuales de una impresora (EnumJobs nivel 2) y, en *info, sus
// datos (GetPrinter). Vacío si no se pudo abrir (el motivo se imprime).
// Usa el nombre tal cual: por la conexión responde la caché del cliente (D28).
std::vector<core::JobInfo> Jobs(const std::wstring& printer, core::PrinterInfo* info) {
    core::PrinterHandle h;
    if (!h.OpenBest(printer)) {
        Out(util::Format(L"No se pudo abrir '%ls': %ls\n", printer.c_str(), util::Win32ErrorMessage(GetLastError()).c_str()));
        return {};
    }
    // Servidor = la parte "\\\\SRV" del nombre (vacío si la impresora es local).
    std::wstring server;
    if (printer.rfind(L"\\\\", 0) == 0) server = printer.substr(0, printer.find(L'\\', 2));
    info->name = printer;
    core::spooler::QueryPrinter(h.get(), server, *info);
    DWORD error = 0;
    return core::spooler::EnumerateJobs(h.get(), printer, info->problems, &error);
}

// Valor de texto para CSV: entre comillas, duplicando las comillas internas.
std::wstring Csv(const std::wstring& v) {
    std::wstring out = L"\"";
    for (wchar_t c : v) out += c == L'"' ? std::wstring(L"\"\"") : std::wstring(1, c);
    return out + L"\"";
}

// Magnitud para el CSV: "valor (fiabilidad)", con "?" si no se determinó.
std::wstring QuantityCsv(const core::Quantity& q) {
    std::wstring value = q.Known() ? std::to_wstring(q.value) : L"?";
    return value + L" (" + app::ReliabilityText(q.reliability) + L")";
}

// Imprime la ficha de un trabajo y, si se pidió, agrega su fila al CSV.
// Con --data arma la vista previa con el mismo código que la app (lee los
// datos con ReadJobData, que exige que el trabajo esté en pausa, D39).
void Inspect(const core::JobInfo& job, const core::PrinterInfo& printer, const Options& o) {
    preview::DocumentPtr doc;
    if (o.data) {
        // Solo las primeras 3 páginas: basta para evaluar si hay vista previa.
        preview::PreviewRequest request;
        request.job = job;
        request.options.maxPages = 3;
        preview::RendererPaths paths;
        if (!o.renderers.empty()) {
            paths.ghostscript = o.renderers + L"\\gswin64c.exe";
            paths.ghostpcl = o.renderers + L"\\gpcl6win64.exe";
            paths.ghostxps = o.renderers + L"\\gxpswin64.exe";
        }
        request.options.renderers = preview::DetectRenderers(paths);
        doc = preview::BuildPreview(request);
    }
    // Ficha "Configuración" (app/JobSheet) en texto: secciones entre corchetes,
    // valor con su marca de fiabilidad y el detalle (motivo) debajo.
    Out(L"\n==================================================================\n");
    for (const auto& row : app::BuildJobSheet(job, &printer, doc.get())) {
        if (row.kind == ui::InfoRow::Kind::Section) {
            Out(L"\n[" + row.label + L"]\n");
            continue;
        }
        std::wstring line = L"  " + row.label + L": " + row.value;
        if (!row.badge.empty()) line += L"  [" + row.badge + L"]";
        Out(line + L"\n");
        if (!row.detail.empty()) Out(L"      " + row.detail + L"\n");
    }
    if (doc && !doc->HasPages() && !doc->message.empty()) Out(L"\n  Vista previa: " + doc->message + L"\n");

    // Fila de la matriz de pruebas (docs/DATOS_DE_IMPRESION.md): separador ";"
    // y UTF-8; el encabezado solo si el archivo es nuevo.
    if (!o.csv.empty()) {
        const core::ContentFacts* facts = doc && doc->facts.analyzed ? &doc->facts : nullptr;
        core::PageAccounting acc = core::ComputePageAccounting(job, facts);
        bool exists = GetFileAttributesW(o.csv.c_str()) != INVALID_FILE_ATTRIBUTES;
        FILE* f = _wfopen(o.csv.c_str(), L"ab");
        if (!f) return;
        auto write = [&](const std::wstring& line) {
            std::string utf8 = util::ToUtf8(line);
            fwrite(utf8.data(), 1, utf8.size(), f);
        };
        if (!exists)
            write(L"prueba;impresora;id;documento;pDatatype;procesador;formato;TotalPages;dmCopies;dmDuplex;papel;"
                  L"orientacion;opciones_en_datos;paginas_datos;paginas_logicas;copias;caras;hojas;vista_previa\r\n");
        const auto& dm = job.settings;
        std::wstring preview = !doc ? L"(sin --data)" : doc->HasPages() ? util::Format(L"sí (%u págs.)", static_cast<unsigned>(doc->pages.size())) : L"no";
        write(Csv(o.label) + L";" + Csv(printer.DisplayName()) + L";" + std::to_wstring(job.id) + L";" + Csv(job.document) +
              L";" + Csv(job.datatype) + L";" + Csv(job.printProcessor) + L";" +
              Csv(facts ? facts->language : L"") + L";" + std::to_wstring(job.totalPages) + L";" +
              (dm.copies.valid ? std::to_wstring(dm.copies.value) : L"-") + L";" +
              (dm.duplex.valid ? std::to_wstring(dm.duplex.value) : L"-") + L";" + Csv(app::PaperText(dm)) + L";" +
              (dm.orientation.valid ? app::OrientationText(dm.orientation.value) : L"-") + L";" +
              Csv(facts ? facts->rawSettings : L"") + L";" + QuantityCsv(acc.dataPages) + L";" +
              QuantityCsv(acc.logicalPages) + L";" + QuantityCsv(acc.copies) + L";" + QuantityCsv(acc.sides) + L";" +
              QuantityCsv(acc.sheets) + L";" + Csv(preview) + L"\r\n");
        fclose(f);
    }
}

// Pausa un trabajo con la API de la app e informa el resultado en la consola.
void PauseJob(const std::wstring& printer, std::uint32_t id) {
    auto result = core::spooler::PauseJob(printer, id);
    Out(util::Format(L"  -> trabajo %u pausado: %ls\n", id, result.ok ? L"sí" : util::Win32ErrorMessage(result.error).c_str()));
}

// Ejecuta la herramienta: inspección única o vigilancia (--hold) y, al final,
// la cancelación opcional (--delete). Devuelve el código de salida.
int Run(const Options& o) {
    const auto printers = PrintersOf(o.target);
    // (impresora, id) de los trabajos ya inspeccionados (para --delete y para
    // no repetir en --hold).
    std::vector<std::pair<std::wstring, std::uint32_t>> inspected;
    auto matches = [&](const core::JobInfo& j) { return o.job == 0 || j.id == o.job; };

    // Modo único: inspecciona lo que hay ahora y termina.
    if (o.holdSeconds <= 0) {
        for (const auto& name : printers) {
            core::PrinterInfo info;
            for (const auto& job : Jobs(name, &info)) {
                if (!matches(job)) continue;
                Inspect(job, info, o);
                inspected.push_back({name, job.id});
            }
        }
    } else {
        // Modo vigilancia: los trabajos que ya estaban no se tocan.
        // known = vistos alguna vez; fresh = llegaron durante la vigilancia
        // (los únicos que se pausan e inspeccionan); done = ya inspeccionados.
        std::set<std::wstring> known, fresh, done;
        for (const auto& name : printers) {
            core::PrinterInfo info;
            for (const auto& job : Jobs(name, &info)) known.insert(job.Key());
        }
        Out(util::Format(L"Vigilando %zu impresora(s) durante %d s. Envíe ahora los trabajos de prueba.\n",
                         printers.size(), o.holdSeconds));
        // Sondeo cada 30 ms (sin notificaciones) para pausar el trabajo lo
        // antes posible, mientras todavía se recibe.
        const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(o.holdSeconds) * 1000;
        while (GetTickCount64() < end) {
            for (const auto& name : printers) {
                core::PrinterInfo info;
                for (const auto& job : Jobs(name, &info)) {
                    if (!known.count(job.Key())) {
                        known.insert(job.Key());
                        fresh.insert(job.Key());
                        Out(util::Format(L"Nuevo trabajo %u '%ls'\n", job.id, job.document.c_str()));
                        PauseJob(name, job.id);
                    }
                    // Se inspecciona cuando terminó de recibirse.
                    // (Sin SPOOLING y en pausa: leerlo antes haría esperar a
                    // ReadPrinter, GUIA_IA §6, y CanReadJobData lo rechazaría.)
                    if (!done.count(job.Key()) && fresh.count(job.Key()) && !(job.rawStatus & JOB_STATUS_SPOOLING) &&
                        (job.rawStatus & JOB_STATUS_PAUSED)) {
                        bool isNew = std::none_of(inspected.begin(), inspected.end(), [&](const auto& p) {
                            return p.first == name && p.second == job.id;
                        });
                        if (!isNew) continue;
                        done.insert(job.Key());
                        Inspect(job, info, o);
                        inspected.push_back({name, job.id});
                        if (o.release) {
                            auto r = core::spooler::ResumeJob(name, job.id);
                            Out(util::Format(L"  -> trabajo %u reanudado: %ls\n", job.id,
                                             r.ok ? L"sí" : util::Win32ErrorMessage(r.error).c_str()));
                        }
                    }
                }
            }
            Sleep(30);
        }
    }

    // Cancelación final (funciona con trabajos propios; los ajenos exigen
    // "Administrar documentos").
    if (o.del) {
        for (const auto& [printer, id] : inspected) {
            auto r = core::spooler::CancelJob(printer, id);
            Out(util::Format(L"Cancelado %u: %ls\n", id, r.ok ? L"sí" : util::Win32ErrorMessage(r.error).c_str()));
        }
    }
    return 0;
}

}  // namespace

// Punto de entrada: interpreta los argumentos (los desconocidos se ignoran),
// fija el idioma español de los textos de la ficha e inicializa COM (lo
// necesitan el XPS Object Model y WIC del análisis con --data).
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) {
        Out(L"Uso: cola_jobinspect \"Impresora\"|\\\\SERVIDOR [--data] [--job N] [--hold S] [--csv archivo] "
            L"[--label texto] [--renderers DIR] [--delete]\n");
        return 2;
    }
    Options o;
    o.target = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? argv[++i] : L""; };
        if (a == L"--data") o.data = true;
        else if (a == L"--job") o.job = static_cast<std::uint32_t>(_wtoi(next().c_str()));
        else if (a == L"--hold") o.holdSeconds = _wtoi(next().c_str());
        else if (a == L"--csv") o.csv = next();
        else if (a == L"--label") o.label = next();
        else if (a == L"--renderers") o.renderers = next();
        else if (a == L"--delete") o.del = true;
        else if (a == L"--release") o.release = true;
    }
    i18n::SetLanguage(i18n::Language::Spanish);
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // XPS Object Model, WIC
    int rc = Run(o);
    if (SUCCEEDED(com)) CoUninitialize();
    return rc;
}
