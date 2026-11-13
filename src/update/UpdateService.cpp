// Servicio de actualización automática. Ver UpdateService.h.
#include "update/UpdateService.h"

#include <windows.h>

#include <vector>

#include "config/Paths.h"
#include "update/UpdatePackage.h"
#include "util/Log.h"
#include "util/Sha256.h"
#include "util/Strings.h"

namespace cola::update {

bool InstalledVersion(Version& out) {
    const std::wstring exe = config::ExecutablePath();
    DWORD ignored = 0;
    DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
    if (!size) return false;
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, data.data())) return false;
    // Bloque raíz: la versión numérica (FILEVERSION de res/app.rc).
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info || length < sizeof(*info))
        return false;
    out = Version{HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS)};
    return true;
}

UpdateService::UpdateService(Callback onStatus) : onStatus_(std::move(onStatus)) {
    // Restos de la actualización anterior (el .exe viejo renombrado, etc.).
    worker_.Post([] { CleanupOldFiles(config::ExecutableDir()); });
}

UpdateService::~UpdateService() {
    // Antes de que el destructor de worker_ espere a la tarea en curso.
    abort_.Abort();
}

void UpdateService::Check(bool manual, bool install) {
    worker_.Post([this, manual, install] { Run(manual, install); });
}

void UpdateService::Run(bool manual, bool install) {
    UpdateStatus status;
    status.manual = manual;
    // Envía un aviso con el estado indicado (copia de `status`).
    auto report = [&](UpdateState state) {
        status.state = state;
        if (onStatus_) onStatus_(status);
    };
    const Version current = CurrentVersion();

    // ¿Ya hay en disco una versión más nueva que la que corre? (La instaló
    // esta copia antes, u otra sesión de Windows que usa la misma carpeta.)
    Version onDisk;
    if (InstalledVersion(onDisk) && onDisk > current) {
        status.version = onDisk;
        report(UpdateState::Installed);
        return;
    }

    if (manual) report(UpdateState::Checking);
    HttpResult response = HttpGet(ManifestUrl(), kMaxManifestBytes, &abort_);
    if (abort_.aborted()) return;  // la app se está cerrando
    if (!response.ok) {
        // 404 = todavía no hay ninguna publicación con manifiesto: no es un error.
        if (response.status == 404) {
            util::LogInfo(L"Actualización: no hay ninguna versión publicada con manifiesto");
            report(UpdateState::UpToDate);
            return;
        }
        util::LogError(L"Actualización: no se pudo consultar la última versión: %ls", response.error.c_str());
        status.error = response.error;
        report(UpdateState::Failed);
        return;
    }
    Manifest manifest;
    std::wstring problem;
    if (!ParseManifest(response.body, manifest, &problem)) {
        util::LogError(L"Actualización: manifiesto inválido: %ls", problem.c_str());
        status.error = problem;
        report(UpdateState::Failed);
        return;
    }
    if (!(manifest.version > current)) {
        util::LogInfo(L"Actualización: al día (publicada %ls, en uso %ls)", manifest.version.ToString().c_str(),
                      current.ToString().c_str());
        report(UpdateState::UpToDate);
        return;
    }

    status.version = manifest.version;
    status.notes = manifest.notes;
    util::LogInfo(L"Actualización: hay una versión nueva, %ls (en uso %ls)", manifest.version.ToString().c_str(),
                  current.ToString().c_str());
    if (!install) {
        report(UpdateState::Available);
        return;
    }
    // Sin permiso de escritura no tiene sentido descargar.
    const std::wstring target = config::ExecutableDir();
    if (!CanWriteTo(target)) {
        util::LogError(L"Actualización: no se puede escribir en %ls; la versión %ls no se instala", target.c_str(),
                       manifest.version.ToString().c_str());
        report(UpdateState::NotWritable);
        return;
    }

    report(UpdateState::Downloading);
    response = HttpGet(PackageUrl(manifest), manifest.size, &abort_);
    if (abort_.aborted()) return;
    if (!response.ok) {
        util::LogError(L"Actualización: no se pudo descargar el paquete: %ls", response.error.c_str());
        status.error = response.error;
        report(UpdateState::Failed);
        return;
    }
    // El paquete debe ser exactamente el publicado: mismo tamaño y misma huella.
    if (response.body.size() != manifest.size || util::Sha256Hex(response.body) != manifest.sha256) {
        util::LogError(L"Actualización: el paquete descargado no coincide con el manifiesto (%zu bytes)",
                       response.body.size());
        status.error = L"el paquete descargado no coincide con el publicado";
        report(UpdateState::Failed);
        return;
    }

    InstallResult installed = InstallPackage(response.body, target);
    if (!installed.ok) {
        util::LogError(L"Actualización: no se instaló la versión %ls: %ls", manifest.version.ToString().c_str(),
                       installed.error.c_str());
        status.error = installed.error;
        report(installed.accessDenied ? UpdateState::NotWritable : UpdateState::Failed);
        return;
    }
    util::LogInfo(L"Actualización: versión %ls instalada (%d archivos cambiados, %d iguales); se usará al reiniciar",
                  manifest.version.ToString().c_str(), installed.replaced, installed.unchanged);
    report(UpdateState::Installed);
}

}  // namespace cola::update
