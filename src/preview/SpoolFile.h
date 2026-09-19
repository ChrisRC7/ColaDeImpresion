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

// Candidatos de ruta para el .SPL de un trabajo, en orden de preferencia.
std::vector<std::wstring> SpoolFileCandidates(const std::wstring& server, std::uint32_t jobId,
                                              const std::wstring& overrideDir);

// Convierte una ruta local del servidor ("C:\\X") a UNC ("\\\\SRV\\C$\\X").
std::wstring ToAdminShare(const std::wstring& server, const std::wstring& localPath);

// Lee el spool (como máximo maxBytes). Detecta el formato.
SpoolData ReadSpoolFile(const std::wstring& server, std::uint32_t jobId, const std::wstring& overrideDir,
                        std::uint64_t maxBytes);

// Lee un archivo cualquiera (hasta maxBytes) compartiendo lectura/escritura.
bool ReadFileShared(const std::wstring& path, std::uint64_t maxBytes, std::vector<std::uint8_t>& out,
                    std::uint64_t* fileSize, unsigned long* error);

// Detecta el lenguaje de los datos (EMF spool, XPS, PDF, PostScript, PCL, PCL XL...).
// Entiende el envoltorio PJL (@PJL ENTER LANGUAGE=...).
DataFormat DetectFormat(const std::uint8_t* data, size_t size);

// Posición donde empiezan los datos reales tras un encabezado PJL/UEL (0 si no hay).
size_t SkipPjlHeader(const std::uint8_t* data, size_t size);

}  // namespace cola::preview
