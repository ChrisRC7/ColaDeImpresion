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

struct Options {
    std::wstring target;
    bool data = false;
    std::uint32_t job = 0;
    int holdSeconds = 0;
    std::wstring csv;
    std::wstring label;
    std::wstring renderers;
    bool del = false;
    bool release = false;
};

void Out(const std::wstring& text) {
    std::string utf8 = util::ToUtf8(text);
    fwrite(utf8.data(), 1, utf8.size(), stdout);
    fflush(stdout);
}

std::vector<std::wstring> PrintersOf(const std::wstring& target) {
    if (target.size() > 2 && target.rfind(L"\\\\", 0) == 0 && target.find(L'\\', 2) == std::wstring::npos) {
        std::vector<std::wstring> names;
        for (const auto& p : core::spooler::EnumeratePrinters(target)) names.push_back(p.name);
        return names;
    }
    return {target};
}

std::vector<core::JobInfo> Jobs(const std::wstring& printer, core::PrinterInfo* info) {
    core::PrinterHandle h;
    if (!h.OpenBest(printer)) {
        Out(util::Format(L"No se pudo abrir '%ls': %ls\n", printer.c_str(), util::Win32ErrorMessage(GetLastError()).c_str()));
        return {};
    }
    std::wstring server;
    if (printer.rfind(L"\\\\", 0) == 0) server = printer.substr(0, printer.find(L'\\', 2));
    info->name = printer;
    core::spooler::QueryPrinter(h.get(), server, *info);
    DWORD error = 0;
    return core::spooler::EnumerateJobs(h.get(), printer, info->problems, &error);
}

std::wstring Csv(const std::wstring& v) {
    std::wstring out = L"\"";
    for (wchar_t c : v) out += c == L'"' ? std::wstring(L"\"\"") : std::wstring(1, c);
    return out + L"\"";
}

std::wstring QuantityCsv(const core::Quantity& q) {
    std::wstring value = q.Known() ? std::to_wstring(q.value) : L"?";
    return value + L" (" + app::ReliabilityText(q.reliability) + L")";
}

void Inspect(const core::JobInfo& job, const core::PrinterInfo& printer, const Options& o) {
    preview::DocumentPtr doc;
    if (o.data) {
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

void PauseJob(const std::wstring& printer, std::uint32_t id) {
    auto result = core::spooler::PauseJob(printer, id);
    Out(util::Format(L"  -> trabajo %u pausado: %ls\n", id, result.ok ? L"sí" : util::Win32ErrorMessage(result.error).c_str()));
}

int Run(const Options& o) {
    const auto printers = PrintersOf(o.target);
    std::vector<std::pair<std::wstring, std::uint32_t>> inspected;
    auto matches = [&](const core::JobInfo& j) { return o.job == 0 || j.id == o.job; };

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
        std::set<std::wstring> known, fresh, done;
        for (const auto& name : printers) {
            core::PrinterInfo info;
            for (const auto& job : Jobs(name, &info)) known.insert(job.Key());
        }
        Out(util::Format(L"Vigilando %zu impresora(s) durante %d s. Envíe ahora los trabajos de prueba.\n",
                         printers.size(), o.holdSeconds));
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

    if (o.del) {
        for (const auto& [printer, id] : inspected) {
            auto r = core::spooler::CancelJob(printer, id);
            Out(util::Format(L"Cancelado %u: %ls\n", id, r.ok ? L"sí" : util::Win32ErrorMessage(r.error).c_str()));
        }
    }
    return 0;
}

}  // namespace

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
