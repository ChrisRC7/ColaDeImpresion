// Búsqueda de los intérpretes Ghostscript/GhostPCL/GhostXPS y ejecución de
// uno de ellos sobre los datos de un trabajo. Ver ExternalRenderer.h.
//
// Flujo de RenderExternal: datos -> archivo temporal privado (D27) ->
// proceso oculto que escribe un PNG por página -> se cargan los PNG como
// páginas bitmap -> se borran todos los archivos intermedios.
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

// true si la ruta existe y es un archivo (no una carpeta).
bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

// Subcarpetas directas de 'dir' (rutas completas), sin "." ni "..".
// Se usa para "C:\Program Files\gs\gs10.05.0" y similares.
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

// Primer ejecutable encontrado de 'names' (en orden de preferencia: 64 bits
// primero). Orden de búsqueda:
//   1. carpeta "renderers" junto al .exe (la que trae el paquete);
//   2. "%ProgramFiles%\gs\<versión>\bin", de la versión más nueva a la más
//      vieja, y "%ProgramFiles%\gs";
//   3. el PATH del sistema (SearchPathW).
// "" si no aparece en ningún lado.
std::wstring FindExecutable(const std::vector<std::wstring>& names) {
    std::vector<std::wstring> roots;
    roots.push_back(config::ExecutableDir() + L"\\renderers");
    // ProgramW6432 apunta a "Program Files" de 64 bits aunque el proceso sea
    // de 32 bits; ProgramFiles es la alternativa.
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

// Escribe el búfer completo en un archivo nuevo (o lo reemplaza), sin
// compartirlo y con FILE_ATTRIBUTE_TEMPORARY (el sistema intenta mantenerlo
// en caché sin bajarlo a disco). WriteFile acepta un DWORD por llamada:
// se escribe en bloques de 16 MB. false si algo falla (GetLastError da el motivo).
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
// Sin ventana de consola (CREATE_NO_WINDOW) y con prioridad por debajo de lo
// normal para no frenar la UI ni el resto del equipo. Si se pasa de
// timeoutMs se termina el proceso y error recibe "timeout" (el código
// devuelto es entonces -1). No hereda handles.
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

// Extensión del archivo de entrada. Ghostscript decide algunos detalles por
// la extensión; ".prn" para PCL (GhostPCL detecta el lenguaje por contenido).
const wchar_t* ExtensionFor(DataFormat format) {
    switch (format) {
        case DataFormat::Pdf: return L".pdf";
        case DataFormat::PostScript: return L".ps";
        case DataFormat::Xps: return L".xps";
        default: return L".prn";
    }
}

}  // namespace

// Cada intérprete se busca por separado; se aceptan los nombres de 64 bits,
// de 32 bits y genéricos (compilaciones propias).
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
        result.failed = true;
        result.exitCode = -1;
        return result;
    }

    // Ghostscript no entiende PJL: se lo quitamos a PS/PDF. GhostPCL sí lo procesa.
    size_t skip = (format == DataFormat::PostScript || format == DataFormat::Pdf) ? SkipPjlHeader(data, size) : 0;

    // Carpeta privada del proceso (solo el usuario actual); los archivos se borran al terminar.
    const std::wstring base = NewTempPath(L"");
    if (base.empty()) {
        result.error = util::Win32ErrorMessage(ERROR_ACCESS_DENIED);
        result.failed = true;
        result.exitCode = -1;
        return result;
    }
    // Entrada: <base>.ps/.pdf/.xps/.prn. Salida: <base>_p001.png, _p002.png...
    // (%03d lo sustituye el propio Ghostscript por el número de página).
    const std::wstring input = base + ExtensionFor(format);
    const std::wstring outputPattern = base + L"_p%03d.png";
    if (!WriteAll(input, data + skip, size - skip)) {
        result.error = util::Win32ErrorMessage(GetLastError());
        result.failed = true;
        result.exitCode = -1;
        DeleteFileW(input.c_str());  // puede haber quedado a medias
        return result;
    }

    // Opciones (comunes a Ghostscript, GhostPCL y GhostXPS):
    //   -dSAFER          : el documento no puede leer/escribir archivos ajenos
    //                      (los datos vienen de terceros);
    //   -dBATCH -dNOPAUSE: sin interacción, salir al terminar;
    //   -dQUIET          : sin mensajes en la consola;
    //   -sDEVICE=png16m  : PNG a color de 24 bits, una imagen por página;
    //   -r<dpi>          : resolución;
    //   -dTextAlphaBits=4 -dGraphicsAlphaBits=4: suavizado de texto y gráficos;
    //   -dFirstPage/-dLastPage: solo las páginas pedidas (vista por partes).
    std::wstring cmd = util::Format(
        L"\"%ls\" -dSAFER -dBATCH -dNOPAUSE -dQUIET -sDEVICE=png16m -r%d -dTextAlphaBits=4 -dGraphicsAlphaBits=4 "
        L"-dFirstPage=%d -dLastPage=%d -sOutputFile=\"%ls\" \"%ls\"",
        exe.c_str(), dpi, firstPage, lastPage, outputPattern.c_str(), input.c_str());
    std::wstring runError;
    int code = RunHidden(cmd, timeoutMs, &runError);
    // No terminó bien: no arrancó o se agotó el tiempo (runError) o salió con
    // error. Las páginas que haya dejado se recogen igual (sirven para la
    // vista previa), pero quien necesite el documento completo debe mirar
    // 'failed' (F4: un lote corto por un fallo se tomaba por fin del documento).
    result.exitCode = code;
    result.timedOut = runError == L"timeout";
    result.failed = !runError.empty() || code != 0;

    // Recoge las páginas generadas (el patrón empieza en 1 aunque firstPage > 1).
    // Se cargan en orden hasta el primer número que falte (el documento tenía
    // menos páginas que las pedidas, o el intérprete se detuvo). Cada PNG se
    // carga limitado a 20000 px por lado y se borra enseguida (D27). Se
    // recogen aunque el código de salida sea de error: lo ya generado sirve.
    for (int i = 1; i <= lastPage - firstPage + 1; ++i) {
        std::wstring png = util::Format(L"%ls_p%03d.png", base.c_str(), i);
        if (!FileExists(png)) break;
        auto pages = LoadImageFile(png, 1, 20000);
        for (auto& p : pages) result.pages.push_back(p);
        DeleteFileW(png.c_str());
    }
    // Limpia archivos sobrantes (si lastPage excedía el documento no hay más).
    DeleteFileW(input.c_str());

    // 'error' se llena si no salió ninguna página o si el intérprete falló
    // (aunque haya páginas parciales). Sin páginas y sin fallo es simplemente
    // un rango más allá del final del documento: se informa "exit code 0".
    if (result.pages.empty() || result.failed) {
        result.error = runError.empty() ? util::Format(L"exit code %d", code) : runError;
        util::LogError(L"Render externo falló (%ls, páginas %d-%d): %ls; %zu página(s) generadas", exe.c_str(),
                       firstPage, lastPage, result.error.c_str(), result.pages.size());
    }
    return result;
}

}  // namespace cola::preview
