#include "preview/ExternalRenderer.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>

#include "config/Paths.h"
#include "preview/ImageLoader.h"
#include "preview/SpoolFile.h"
#include "preview/TempFiles.h"
#include "util/Log.h"
#include "util/Strings.h"

namespace cola::preview {

namespace {

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::vector<std::wstring> ListSubdirs(const std::wstring& dir) {
    std::vector<std::wstring> result;
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return result;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.')
            result.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    // Orden descendente: la versión más nueva primero (gs10.05 > gs10.04 > gs9.56).
    std::sort(result.begin(), result.end(), [](const std::wstring& a, const std::wstring& b) {
        return StrCmpLogicalW(a.c_str(), b.c_str()) > 0;
    });
    return result;
}

std::wstring FindExecutable(const std::vector<std::wstring>& names) {
    std::vector<std::wstring> roots;
    roots.push_back(config::ExecutableDir() + L"\\renderers");
    wchar_t programFiles[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"ProgramW6432", programFiles, MAX_PATH) ||
        GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH)) {
        for (const auto& version : ListSubdirs(std::wstring(programFiles) + L"\\gs")) roots.push_back(version + L"\\bin");
        roots.push_back(std::wstring(programFiles) + L"\\gs");
    }
    for (const auto& root : roots)
        for (const auto& name : names)
            if (FileExists(root + L"\\" + name)) return root + L"\\" + name;

    for (const auto& name : names) {
        wchar_t found[MAX_PATH] = {};
        if (SearchPathW(nullptr, name.c_str(), nullptr, MAX_PATH, found, nullptr)) return found;
    }
    return L"";
}

bool WriteAll(const std::wstring& path, const std::uint8_t* data, size_t size) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    size_t offset = 0;
    bool ok = true;
    while (offset < size) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(size - offset, 16 * 1024 * 1024));
        DWORD written = 0;
        if (!WriteFile(file, data + offset, chunk, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        offset += written;
    }
    CloseHandle(file);
    return ok;
}

// Ejecuta un proceso oculto y espera. Devuelve el código de salida o -1.
int RunHidden(const std::wstring& commandLine, unsigned timeoutMs, std::wstring* error) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = commandLine;  // CreateProcessW puede modificar el buffer
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                        nullptr, nullptr, &si, &pi)) {
        if (error) *error = util::Win32ErrorMessage(GetLastError());
        return -1;
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = static_cast<DWORD>(-1);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        if (error) *error = L"timeout";
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

const wchar_t* ExtensionFor(DataFormat format) {
    switch (format) {
        case DataFormat::Pdf: return L".pdf";
        case DataFormat::PostScript: return L".ps";
        case DataFormat::Xps: return L".xps";
        default: return L".prn";
    }
}

}  // namespace

RendererPaths DetectRenderers(const RendererPaths& configured) {
    RendererPaths p = configured;
    if (p.ghostscript.empty() || !FileExists(p.ghostscript))
        p.ghostscript = FindExecutable({L"gswin64c.exe", L"gswin32c.exe"});
    if (p.ghostpcl.empty() || !FileExists(p.ghostpcl))
        p.ghostpcl = FindExecutable({L"gpcl6win64.exe", L"gpcl6win32.exe", L"gpcl6.exe"});
    if (p.ghostxps.empty() || !FileExists(p.ghostxps))
        p.ghostxps = FindExecutable({L"gxpswin64.exe", L"gxpswin32.exe", L"gxps.exe"});
    return p;
}

std::wstring RendererFor(DataFormat format, const RendererPaths& paths) {
    switch (format) {
        case DataFormat::Pdf:
        case DataFormat::PostScript: return paths.ghostscript;
        case DataFormat::Pcl:
        case DataFormat::PclXl: return paths.ghostpcl;
        case DataFormat::Xps: return paths.ghostxps;
        default: return L"";
    }
}

ExternalRenderResult RenderExternal(DataFormat format, const std::uint8_t* data, size_t size,
                                    const RendererPaths& paths, int dpi, int firstPage, int lastPage,
                                    unsigned timeoutMs) {
    ExternalRenderResult result;
    const std::wstring exe = RendererFor(format, paths);
    if (exe.empty()) {
        result.error = L"no-renderer";
        return result;
    }

    // Ghostscript no entiende PJL: se lo quitamos a PS/PDF. GhostPCL sí lo procesa.
    size_t skip = (format == DataFormat::PostScript || format == DataFormat::Pdf) ? SkipPjlHeader(data, size) : 0;

    // Carpeta privada del proceso (solo el usuario actual); los archivos se borran al terminar.
    const std::wstring base = NewTempPath(L"");
    if (base.empty()) {
        result.error = util::Win32ErrorMessage(ERROR_ACCESS_DENIED);
        return result;
    }
    const std::wstring input = base + ExtensionFor(format);
    const std::wstring outputPattern = base + L"_p%03d.png";
    if (!WriteAll(input, data + skip, size - skip)) {
        result.error = util::Win32ErrorMessage(GetLastError());
        return result;
    }

    std::wstring cmd = util::Format(
        L"\"%ls\" -dSAFER -dBATCH -dNOPAUSE -dQUIET -sDEVICE=png16m -r%d -dTextAlphaBits=4 -dGraphicsAlphaBits=4 "
        L"-dFirstPage=%d -dLastPage=%d -sOutputFile=\"%ls\" \"%ls\"",
        exe.c_str(), dpi, firstPage, lastPage, outputPattern.c_str(), input.c_str());
    std::wstring runError;
    int code = RunHidden(cmd, timeoutMs, &runError);

    // Recoge las páginas generadas (el patrón empieza en 1 aunque firstPage > 1).
    for (int i = 1; i <= lastPage - firstPage + 1; ++i) {
        std::wstring png = util::Format(L"%ls_p%03d.png", base.c_str(), i);
        if (!FileExists(png)) break;
        auto pages = LoadImageFile(png, 1, 20000);
        for (auto& p : pages) result.pages.push_back(p);
        DeleteFileW(png.c_str());
    }
    // Limpia archivos sobrantes (si lastPage excedía el documento no hay más).
    DeleteFileW(input.c_str());

    if (result.pages.empty()) {
        result.error = runError.empty() ? util::Format(L"exit code %d", code) : runError;
        util::LogError(L"Render externo falló (%ls): %ls", exe.c_str(), result.error.c_str());
    }
    return result;
}

}  // namespace cola::preview
