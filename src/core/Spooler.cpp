#include "core/Spooler.h"

#include <algorithm>

#include "core/JobStatus.h"
#include "util/Log.h"

namespace cola::core {

bool PrinterHandle::Open(const std::wstring& name, bool admin) {
    Close();
    PRINTER_DEFAULTSW defaults{};
    if (name.empty() || (name.size() > 2 && name.rfind(L"\\\\", 0) == 0 && name.find(L'\\', 2) == std::wstring::npos)) {
        // Handle de servidor.
        defaults.DesiredAccess = admin ? SERVER_ACCESS_ADMINISTER : SERVER_ACCESS_ENUMERATE;
    } else {
        defaults.DesiredAccess = admin ? (PRINTER_ACCESS_ADMINISTER | PRINTER_ACCESS_USE) : PRINTER_ACCESS_USE;
    }
    HANDLE h = nullptr;
    LPWSTR target = name.empty() ? nullptr : const_cast<LPWSTR>(name.c_str());
    if (!OpenPrinterW(target, &h, &defaults)) return false;
    handle_ = h;
    return true;
}

bool PrinterHandle::OpenBest(const std::wstring& name, bool* gotAdmin) {
    if (Open(name, true)) {
        if (gotAdmin) *gotAdmin = true;
        return true;
    }
    if (gotAdmin) *gotAdmin = false;
    return Open(name, false);
}

void PrinterHandle::Close() {
    if (handle_) {
        ClosePrinter(handle_);
        handle_ = nullptr;
    }
}

namespace spooler {

namespace {

std::wstring Str(const wchar_t* s) { return s ? std::wstring(s) : std::wstring(); }

// Llama a una función del estilo "consulta tamaño + llena buffer" de winspool.
template <typename Fn>
bool FillBuffer(std::vector<BYTE>& buffer, Fn&& call) {
    DWORD needed = 0;
    if (call(nullptr, 0, &needed)) return true;
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        buffer.resize(needed);
        if (call(buffer.data(), needed, &needed)) return true;
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
    }
    return false;
}

void FillPrinterInfo(const PRINTER_INFO_2W& src, const std::wstring& server, const std::wstring& defaultName,
                     PrinterInfo& out) {
    out.name = Str(src.pPrinterName);
    // Algunos drivers devuelven el nombre sin el prefijo del servidor.
    if (!server.empty() && out.name.rfind(L"\\\\", 0) != 0) out.name = server + L"\\" + out.name;
    out.server = server;
    out.shareName = Str(src.pShareName);
    out.driver = Str(src.pDriverName);
    out.port = Str(src.pPortName);
    out.location = Str(src.pLocation);
    out.comment = Str(src.pComment);
    out.rawStatus = src.Status;
    out.problems = PrinterProblemsFromFlags(src.Status, src.Attributes);
    out.jobCount = src.cJobs;
    out.isShared = (src.Attributes & PRINTER_ATTRIBUTE_SHARED) != 0;
    out.isDefault = !defaultName.empty() && _wcsicmp(out.name.c_str(), defaultName.c_str()) == 0;
}

OpResult ControlJob(const std::wstring& printer, std::uint32_t jobId, DWORD command) {
    PrinterHandle h;
    if (!h.OpenBest(printer)) return OpResult::FromLastError();
    if (!SetJobW(h.get(), jobId, 0, nullptr, command)) return OpResult::FromLastError();
    return OpResult::Success();
}

OpResult ControlPrinter(const std::wstring& printer, DWORD command) {
    PrinterHandle h;
    if (!h.Open(printer, true)) return OpResult::FromLastError();
    if (!SetPrinterW(h.get(), 0, nullptr, command)) return OpResult::FromLastError();
    return OpResult::Success();
}

// Modifica un trabajo usando el nivel 1 (posición/prioridad).
template <typename Mutator>
OpResult ModifyJobLevel1(const std::wstring& printer, std::uint32_t jobId, Mutator&& mutate) {
    PrinterHandle h;
    if (!h.OpenBest(printer)) return OpResult::FromLastError();
    std::vector<BYTE> buffer;
    bool ok = FillBuffer(buffer, [&](BYTE* data, DWORD size, DWORD* needed) {
        return GetJobW(h.get(), jobId, 1, data, size, needed) != FALSE;
    });
    if (!ok) return OpResult::FromLastError();
    auto* info = reinterpret_cast<JOB_INFO_1W*>(buffer.data());
    info->pStatus = nullptr;  // no sobrescribir el texto de estado del monitor de puerto
    info->Position = JOB_POSITION_UNSPECIFIED;
    mutate(*info);
    if (!SetJobW(h.get(), jobId, 1, reinterpret_cast<BYTE*>(info), 0)) return OpResult::FromLastError();
    return OpResult::Success();
}

}  // namespace

std::wstring NormalizeServerName(const std::wstring& server) {
    std::wstring s = server;
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/' || s.back() == L' ')) s.pop_back();
    while (!s.empty() && s.front() == L' ') s.erase(s.begin());
    if (s.empty() || s == L"." || _wcsicmp(s.c_str(), L"localhost") == 0) return L"";
    std::replace(s.begin(), s.end(), L'/', L'\\');
    while (!s.empty() && s.front() == L'\\') s.erase(s.begin());
    return L"\\\\" + s;
}

std::vector<PrinterInfo> EnumeratePrinters(const std::wstring& serverIn, DWORD* error) {
    const std::wstring server = NormalizeServerName(serverIn);
    std::vector<PrinterInfo> printers;
    std::vector<BYTE> buffer;
    DWORD count = 0;
    DWORD flags = server.empty() ? (PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS) : PRINTER_ENUM_NAME;
    LPWSTR name = server.empty() ? nullptr : const_cast<LPWSTR>(server.c_str());

    bool ok = FillBuffer(buffer, [&](BYTE* data, DWORD size, DWORD* needed) {
        return EnumPrintersW(flags, name, 2, data, size, needed, &count) != FALSE;
    });
    if (!ok) {
        DWORD err = GetLastError();
        if (error) *error = err;
        util::LogError(L"EnumPrinters(%ls) falló: %lu", server.c_str(), err);
        return printers;
    }
    if (error) *error = ERROR_SUCCESS;

    const std::wstring defaultName = server.empty() ? GetDefaultPrinterName() : L"";
    auto* infos = reinterpret_cast<PRINTER_INFO_2W*>(buffer.data());
    for (DWORD i = 0; i < count; ++i) {
        PrinterInfo p;
        FillPrinterInfo(infos[i], server, defaultName, p);
        printers.push_back(std::move(p));
    }
    std::sort(printers.begin(), printers.end(), [](const PrinterInfo& a, const PrinterInfo& b) {
        return _wcsicmp(a.DisplayName().c_str(), b.DisplayName().c_str()) < 0;
    });
    return printers;
}

bool QueryPrinter(HANDLE printer, const std::wstring& server, PrinterInfo& out) {
    std::vector<BYTE> buffer;
    bool ok = FillBuffer(buffer, [&](BYTE* data, DWORD size, DWORD* needed) {
        return GetPrinterW(printer, 2, data, size, needed) != FALSE;
    });
    if (!ok) return false;
    bool wasDefault = out.isDefault;
    FillPrinterInfo(*reinterpret_cast<PRINTER_INFO_2W*>(buffer.data()), server, L"", out);
    out.isDefault = wasDefault;
    return true;
}

std::vector<JobInfo> EnumerateJobs(HANDLE printer, const std::wstring& printerName, std::uint32_t problems,
                                   DWORD* error) {
    std::vector<JobInfo> jobs;
    std::vector<BYTE> buffer;
    DWORD count = 0;
    bool ok = FillBuffer(buffer, [&](BYTE* data, DWORD size, DWORD* needed) {
        return EnumJobsW(printer, 0, 0xFFFF, 2, data, size, needed, &count) != FALSE;
    });
    if (!ok) {
        if (error) *error = GetLastError();
        return jobs;
    }
    if (error) *error = ERROR_SUCCESS;

    auto* infos = reinterpret_cast<JOB_INFO_2W*>(buffer.data());
    jobs.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        const JOB_INFO_2W& src = infos[i];
        JobInfo j;
        j.id = src.JobId;
        j.printer = printerName;
        j.document = Str(src.pDocument);
        j.user = Str(src.pUserName);
        j.machine = Str(src.pMachineName);
        j.datatype = Str(src.pDatatype);
        j.driver = Str(src.pDriverName);
        j.statusText = Str(src.pStatus);
        j.rawStatus = src.Status;
        j.priority = src.Priority;
        j.position = src.Position;
        j.totalPages = src.TotalPages;
        j.pagesPrinted = src.PagesPrinted;
        j.sizeBytes = src.Size;
        j.submitted = src.Submitted;
        j.state = JobStateFromFlags(src.Status, problems);
        jobs.push_back(std::move(j));
    }
    return jobs;
}

OpResult PauseJob(const std::wstring& printer, std::uint32_t jobId) {
    return ControlJob(printer, jobId, JOB_CONTROL_PAUSE);
}

OpResult ResumeJob(const std::wstring& printer, std::uint32_t jobId) {
    return ControlJob(printer, jobId, JOB_CONTROL_RESUME);
}

OpResult CancelJob(const std::wstring& printer, std::uint32_t jobId) {
    // JOB_CONTROL_DELETE elimina el trabajo; es lo que hace la cola nativa de Windows.
    return ControlJob(printer, jobId, JOB_CONTROL_DELETE);
}

OpResult RestartJob(const std::wstring& printer, std::uint32_t jobId) {
    return ControlJob(printer, jobId, JOB_CONTROL_RESTART);
}

OpResult SetJobPosition(const std::wstring& printer, std::uint32_t jobId, std::uint32_t position) {
    if (position == 0) position = 1;
    return ModifyJobLevel1(printer, jobId, [&](JOB_INFO_1W& info) { info.Position = position; });
}

OpResult SetJobPriority(const std::wstring& printer, std::uint32_t jobId, std::uint32_t priority) {
    priority = std::clamp<std::uint32_t>(priority, MIN_PRIORITY, MAX_PRIORITY);
    return ModifyJobLevel1(printer, jobId, [&](JOB_INFO_1W& info) { info.Priority = priority; });
}

OpResult PausePrinter(const std::wstring& printer) { return ControlPrinter(printer, PRINTER_CONTROL_PAUSE); }
OpResult ResumePrinter(const std::wstring& printer) { return ControlPrinter(printer, PRINTER_CONTROL_RESUME); }
OpResult PurgePrinter(const std::wstring& printer) { return ControlPrinter(printer, PRINTER_CONTROL_PURGE); }

bool CanAdminister(const std::wstring& printer) {
    PrinterHandle h;
    return h.Open(printer, true);
}

std::wstring GetSpoolDirectory(const std::wstring& serverIn) {
    const std::wstring server = NormalizeServerName(serverIn);
    PrinterHandle h;
    if (!h.Open(server, false)) return L"";
    DWORD type = 0;
    DWORD needed = 0;
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD rc = GetPrinterDataW(h.get(), const_cast<LPWSTR>(SPLREG_DEFAULT_SPOOL_DIRECTORY), &type,
                               reinterpret_cast<BYTE*>(buffer), sizeof(buffer) - sizeof(wchar_t), &needed);
    if (rc != ERROR_SUCCESS) {
        util::LogError(L"No se pudo leer la carpeta de spool de %ls: %lu", server.c_str(), rc);
        return L"";
    }
    return buffer;
}

std::wstring GetDefaultPrinterName() {
    DWORD size = 0;
    GetDefaultPrinterW(nullptr, &size);
    if (size == 0) return L"";
    std::wstring name(size, L'\0');
    if (!GetDefaultPrinterW(name.data(), &size)) return L"";
    name.resize(size > 0 ? size - 1 : 0);
    return name;
}

}  // namespace spooler
}  // namespace cola::core
