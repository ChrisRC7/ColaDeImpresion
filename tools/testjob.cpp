// Herramienta de desarrollo: crea trabajos de impresión reales para probar la
// aplicación sin gastar papel. El trabajo queda en la cola (estado
// "Recibiendo") mientras la herramienta espera, y al final se aborta, así que
// la impresora nunca lo imprime.
//
// Uso:
//   cola_testjob.exe "Nombre impresora" [--pages N] [--seconds S] [--name "Doc"]
//                    [--jobs J] [--raw archivo] [--pause] [--output archivo]
//
//   --output  archivo de salida; necesario con "Microsoft Print to PDF" para
//             que no abra el diálogo "Guardar como".
//   --fake-spool DIR  además escribe DIR\NNNNN.SPL (spool EMF simulado) para
//             probar la vista previa sin ser administrador: configurar
//             "spoolDirOverrides": {"": "DIR"} en config.json.
//   --raw   envía el archivo tal cual (PCL/PS/PDF) con datatype RAW en lugar de GDI.
//   --pause pausa el trabajo apenas se crea.
//   --finish termina el documento normalmente (se imprimirá de verdad).

#include <windows.h>
#include <winspool.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Options {
    std::wstring printer;
    std::wstring name = L"Trabajo de prueba";
    std::wstring rawFile;
    std::wstring output;  // archivo de salida (evita el diálogo de impresoras virtuales)
    std::wstring fakeSpoolDir;
    int pages = 3;
    int seconds = 60;
    int jobs = 1;
    bool pause = false;
    bool finish = false;
};

void DrawPage(HDC dc, int page, int total, const std::wstring& name) {
    int w = GetDeviceCaps(dc, HORZRES);
    int h = GetDeviceCaps(dc, VERTRES);
    HFONT big = CreateFontW(-h / 20, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT small = CreateFontW(-h / 60, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ old = SelectObject(dc, big);
    wchar_t text[256];
    swprintf(text, 256, L"%ls — página %d de %d", name.c_str(), page, total);
    TextOutW(dc, w / 12, h / 12, text, static_cast<int>(wcslen(text)));
    SelectObject(dc, small);
    for (int i = 0; i < 30; ++i) {
        swprintf(text, 256, L"Línea %02d · LAIMI · Cola de impresión · prueba de vista previa", i + 1);
        TextOutW(dc, w / 12, h / 6 + i * h / 45, text, static_cast<int>(wcslen(text)));
    }
    HBRUSH brush = CreateSolidBrush(RGB(36, 88, 214));
    RECT r{w / 12, h * 5 / 6, w / 12 + (w * 5 / 6) * page / total, h * 5 / 6 + h / 40};
    FillRect(dc, &r, brush);
    DeleteObject(brush);
    SelectObject(dc, old);
    DeleteObject(big);
    DeleteObject(small);
}

// Escribe DIR\NNNNN.SPL con formato EMFSPOOL (una EMF por página) para probar
// la vista previa sin permisos de administrador (config: spoolDirOverrides).
void WriteFakeSpool(const std::wstring& dir, DWORD jobId, HDC printerDc, int pages, const std::wstring& name) {
    std::vector<BYTE> spool;
    auto u32 = [&](DWORD v) { spool.insert(spool.end(), reinterpret_cast<BYTE*>(&v), reinterpret_cast<BYTE*>(&v) + 4); };
    u32(0x00010000);
    u32(16);
    u32(0);
    u32(0);
    for (int p = 1; p <= pages; ++p) {
        // Marco = página física completa en centésimas de mm (como un spool real).
        RECT frame{0, 0, GetDeviceCaps(printerDc, PHYSICALWIDTH) * 2540 / GetDeviceCaps(printerDc, LOGPIXELSX),
                   GetDeviceCaps(printerDc, PHYSICALHEIGHT) * 2540 / GetDeviceCaps(printerDc, LOGPIXELSY)};
        HDC emfDc = CreateEnhMetaFileW(printerDc, nullptr, &frame, nullptr);
        DrawPage(emfDc, p, pages, name);
        HENHMETAFILE emf = CloseEnhMetaFile(emfDc);
        UINT size = GetEnhMetaFileBits(emf, 0, nullptr);
        std::vector<BYTE> bytes(size);
        GetEnhMetaFileBits(emf, size, bytes.data());
        DeleteEnhMetaFile(emf);
        u32(0x0C);  // EMRI_METAFILE_DATA
        u32(size);
        spool.insert(spool.end(), bytes.begin(), bytes.end());
    }
    wchar_t file[64];
    swprintf(file, 64, L"\\%05lu.SPL", jobId);
    std::wstring path = dir + file;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, spool.data(), static_cast<DWORD>(spool.size()), &written, nullptr);
    CloseHandle(h);
    wprintf(L"Spool simulado: %ls\n", path.c_str());
}

bool PauseJobById(const std::wstring& printer, DWORD jobId) {
    HANDLE h = nullptr;
    if (!OpenPrinterW(const_cast<LPWSTR>(printer.c_str()), &h, nullptr)) return false;
    BOOL ok = SetJobW(h, jobId, 0, nullptr, JOB_CONTROL_PAUSE);
    ClosePrinter(h);
    return ok != FALSE;
}

int RunGdi(const Options& o) {
    std::vector<HDC> dcs;
    for (int j = 0; j < o.jobs; ++j) {
        HDC dc = CreateDCW(L"WINSPOOL", o.printer.c_str(), nullptr, nullptr);
        if (!dc) {
            fwprintf(stderr, L"CreateDC falló (%lu)\n", GetLastError());
            return 1;
        }
        std::wstring name = o.jobs > 1 ? o.name + L" #" + std::to_wstring(j + 1) : o.name;
        DOCINFOW di{sizeof(di)};
        di.lpszDocName = name.c_str();
        // Impresoras virtuales (p. ej. "Microsoft Print to PDF") abren un diálogo si no hay salida.
        std::wstring output = o.output.empty() ? L"" : o.output + L"." + std::to_wstring(j + 1);
        if (!output.empty()) di.lpszOutput = output.c_str();
        int jobId = StartDocW(dc, &di);
        if (jobId <= 0) {
            fwprintf(stderr, L"StartDoc falló (%lu)\n", GetLastError());
            return 1;
        }
        for (int p = 1; p <= o.pages; ++p) {
            StartPage(dc);
            DrawPage(dc, p, o.pages, name);
            EndPage(dc);
        }
        if (o.pause) PauseJobById(o.printer, static_cast<DWORD>(jobId));
        if (!o.fakeSpoolDir.empty()) WriteFakeSpool(o.fakeSpoolDir, static_cast<DWORD>(jobId), dc, o.pages, name);
        wprintf(L"Trabajo %d creado en '%ls' (%d páginas)\n", jobId, o.printer.c_str(), o.pages);
        if (o.finish) {
            EndDoc(dc);
            DeleteDC(dc);
        } else {
            dcs.push_back(dc);
        }
    }
    if (!dcs.empty()) {
        wprintf(L"Esperando %d s con los trabajos en cola...\n", o.seconds);
        fflush(stdout);
        Sleep(static_cast<DWORD>(o.seconds) * 1000);
        for (HDC dc : dcs) {
            AbortDoc(dc);  // nunca llega a imprimirse
            DeleteDC(dc);
        }
    }
    return 0;
}

int RunRaw(const Options& o) {
    HANDLE file = CreateFileW(o.rawFile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"No se pudo abrir %ls\n", o.rawFile.c_str());
        return 1;
    }
    DWORD size = GetFileSize(file, nullptr);
    std::vector<BYTE> data(size);
    DWORD read = 0;
    ReadFile(file, data.data(), size, &read, nullptr);
    CloseHandle(file);

    HANDLE printer = nullptr;
    if (!OpenPrinterW(const_cast<LPWSTR>(o.printer.c_str()), &printer, nullptr)) {
        fwprintf(stderr, L"OpenPrinter falló (%lu)\n", GetLastError());
        return 1;
    }
    DOC_INFO_1W doc{};
    doc.pDocName = const_cast<LPWSTR>(o.name.c_str());
    doc.pDatatype = const_cast<LPWSTR>(L"RAW");
    DWORD jobId = StartDocPrinterW(printer, 1, reinterpret_cast<BYTE*>(&doc));
    if (!jobId) {
        fwprintf(stderr, L"StartDocPrinter falló (%lu)\n", GetLastError());
        return 1;
    }
    DWORD written = 0;
    WritePrinter(printer, data.data(), read, &written);
    if (o.pause) SetJobW(printer, jobId, 0, nullptr, JOB_CONTROL_PAUSE);
    wprintf(L"Trabajo RAW %lu creado (%lu bytes)\n", jobId, written);
    if (o.finish) {
        EndDocPrinter(printer);
    } else {
        wprintf(L"Esperando %d s...\n", o.seconds);
        fflush(stdout);
        Sleep(static_cast<DWORD>(o.seconds) * 1000);
        SetJobW(printer, jobId, 0, nullptr, JOB_CONTROL_DELETE);
        EndDocPrinter(printer);
    }
    ClosePrinter(printer);
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        fwprintf(stderr,
                 L"Uso: cola_testjob \"Impresora\" [--pages N] [--seconds S] [--name Doc] [--jobs J] [--raw archivo] "
                 L"[--pause] [--finish]\n");
        return 2;
    }
    Options o;
    o.printer = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? argv[++i] : L""; };
        if (a == L"--pages") o.pages = _wtoi(next().c_str());
        else if (a == L"--seconds") o.seconds = _wtoi(next().c_str());
        else if (a == L"--name") o.name = next();
        else if (a == L"--jobs") o.jobs = _wtoi(next().c_str());
        else if (a == L"--raw") o.rawFile = next();
        else if (a == L"--output") o.output = next();
        else if (a == L"--fake-spool") o.fakeSpoolDir = next();
        else if (a == L"--pause") o.pause = true;
        else if (a == L"--finish") o.finish = true;
    }
    if (o.pages < 1) o.pages = 1;
    if (o.jobs < 1) o.jobs = 1;
    return o.rawFile.empty() ? RunGdi(o) : RunRaw(o);
}
