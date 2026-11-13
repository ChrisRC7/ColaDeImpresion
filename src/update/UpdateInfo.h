#pragma once
// Actualización automática (D58): versiones y manifiesto de la publicación.
//
// Cada versión se publica como un "release" del repositorio público de GitHub
// con dos archivos de nombre fijo (los arma scripts\build.ps1 -Package en la
// carpeta publicar\):
//   - actualizacion.json: el manifiesto (versión, nombre, tamaño y SHA-256 del
//     paquete, y un texto opcional con las novedades);
//   - ColaDeImpresion-actualizacion.zip: los archivos de la app (sin los
//     renderizadores ni los archivos propios de cada copia).
// La app pide el manifiesto de la última publicación por una dirección fija
// (.../releases/latest/download/actualizacion.json), que no pasa por la API de
// GitHub y por eso no tiene su límite de consultas por dirección IP (las PCs
// del TEC salen a Internet por las mismas direcciones).
//
// Este archivo es lógica pura (sin red ni disco) y tiene pruebas en tests/.

#include <compare>
#include <cstdint>
#include <string>

namespace cola::update {

// Repositorio de GitHub donde se publican las versiones.
inline constexpr wchar_t kRepoOwner[] = L"ChrisRC7";
inline constexpr wchar_t kRepoName[] = L"ColaDeImpresion";
// Nombre fijo del manifiesto dentro de cada publicación.
inline constexpr wchar_t kManifestName[] = L"actualizacion.json";
// Tamaño máximo aceptado: del manifiesto y del paquete (este último deja
// lugar a un paquete que algún día incluya los renderizadores, ~60 MB).
inline constexpr std::uint64_t kMaxManifestBytes = 64 * 1024;
inline constexpr std::uint64_t kMaxPackageBytes = 300ull * 1024 * 1024;

// Versión "mayor.menor.revisión". Se comparan en ese orden.
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    auto operator<=>(const Version&) const = default;
    std::wstring ToString() const;
};

// Interpreta "1.2.3", "v1.2.3", "1.2" (= 1.2.0) o "1.2.3.0" (el cuarto
// número, el de compilación del .exe, se ignora). false si no es una versión:
// texto vacío, letras, números negativos o de más de 6 cifras.
bool ParseVersion(const std::wstring& text, Version& out);

// Versión de esta compilación (COLA_VERSION_STRING, de CMakeLists.txt).
Version CurrentVersion();

// Contenido de actualizacion.json ya validado.
struct Manifest {
    Version version;         // versión publicada
    std::wstring file;       // nombre del paquete dentro de la publicación
    std::uint64_t size = 0;  // tamaño exacto del paquete en bytes
    std::wstring sha256;     // huella del paquete: 64 hexadecimales en minúsculas
    std::wstring notes;      // novedades (opcional, recortado a 500 caracteres)
};

// Valida y lee el manifiesto. false (con el motivo en `error`) si el JSON es
// inválido o algún campo no cumple: "version" debe ser una versión; "file", un
// nombre de archivo simple terminado en .zip (solo letras, números, punto,
// guion y guion bajo: nunca una ruta ni una dirección); "size", entre 1 y
// kMaxPackageBytes; "sha256", 64 hexadecimales. Así, un manifiesto alterado no
// puede hacer que la app descargue de otro sitio ni reserve memoria sin límite.
bool ParseManifest(const std::string& json, Manifest& out, std::wstring* error = nullptr);

// https://github.com/<dueño>/<repo>/releases/latest/download/actualizacion.json
std::wstring ManifestUrl();
// https://github.com/<dueño>/<repo>/releases/download/v<versión>/<archivo>:
// el paquete se pide por la etiqueta de la versión del manifiesto (no por
// "latest") para que no cambie si se publica otra versión entre las dos
// descargas.
std::wstring PackageUrl(const Manifest& manifest);

}  // namespace cola::update
