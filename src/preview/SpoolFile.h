#pragma once
// Acceso al archivo de spool (.SPL) de un trabajo y detección de su formato.
//
// El spooler guarda los datos de cada trabajo en <carpeta de spool>\NNNNN.SPL
// (NNNNN = id del trabajo con 5 dígitos). En un servidor remoto la carpeta se
// alcanza por el recurso administrativo \\SERVIDOR\C$\... (requiere ser
// administrador del servidor) o por una carpeta alternativa configurada.

#include <cstdint>
#include <string>
#include <vector>

#include "preview/PreviewTypes.h"

namespace cola::preview {

struct SpoolData {
    std::wstring path;               // archivo leído
    std::vector<std::uint8_t> bytes;
    std::uint64_t fileSize = 0;      // tamaño real (bytes puede estar recortado)
    bool truncated = false;
    DataFormat format = DataFormat::Unknown;
    unsigned long error = 0;         // error Win32 si no se pudo leer
};

// Carpetas donde buscar el spool, en orden de preferencia (alternativa
// configurada, carpeta del servidor vía C$, vía ADMIN$).
std::vector<std::wstring> SpoolDirectories(const std::wstring& server, const std::wstring& overrideDir);

// Rutas directas "NNNNN.SPL" (id del trabajo con 5 dígitos) en cada carpeta.
std::vector<std::wstring> SpoolFileCandidates(const std::wstring& server, std::uint32_t jobId,
                                              const std::wstring& overrideDir);

// Convierte una ruta local del servidor ("C:\\X") a UNC ("\\\\SRV\\C$\\X").
std::wstring ToAdminShare(const std::wstring& server, const std::wstring& localPath);

// true si el contenido de un .SHD (archivo sombra) corresponde al trabajo.
bool ShadowMatchesJob(const std::uint8_t* data, size_t size, std::uint32_t jobId, const std::wstring& document);

// Con "spool file pooling" los archivos se llaman FPnnnnn.SPL: busca el del
// trabajo revisando los .SHD de la carpeta. Devuelve "" si no lo encuentra.
std::wstring FindPooledSpoolFile(const std::wstring& dir, std::uint32_t jobId, const std::wstring& document,
                                 unsigned long* error);

// Lee el spool del trabajo (como máximo maxBytes) y detecta el formato.
// Prueba primero NNNNN.SPL y luego los archivos FPnnnnn del pooling.
SpoolData ReadSpoolFile(const std::wstring& server, std::uint32_t jobId, const std::wstring& document,
                        const std::wstring& overrideDir, std::uint64_t maxBytes);

// Lee un archivo cualquiera (hasta maxBytes) compartiendo lectura/escritura.
bool ReadFileShared(const std::wstring& path, std::uint64_t maxBytes, std::vector<std::uint8_t>& out,
                    std::uint64_t* fileSize, unsigned long* error);

// Detecta el lenguaje de los datos (EMF spool, XPS, PDF, PostScript, PCL, PCL XL...).
// Entiende el envoltorio PJL (@PJL ENTER LANGUAGE=...).
DataFormat DetectFormat(const std::uint8_t* data, size_t size);

// Posición donde empiezan los datos reales tras un encabezado PJL/UEL (0 si no hay).
size_t SkipPjlHeader(const std::uint8_t* data, size_t size);

}  // namespace cola::preview
