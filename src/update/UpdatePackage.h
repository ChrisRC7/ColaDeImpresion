#pragma once
// Actualización automática (D58): lectura e instalación del paquete .zip.
//
// El paquete es un ZIP con los archivos de la app en rutas relativas a la
// carpeta del .exe ("ColaDeImpresion.exe", "herramientas/cola_testjob.exe"...).
//
// Cómo se instala sin cerrar la app (InstallPackage):
//   1. Se extrae todo a una carpeta temporal DENTRO de la carpeta de la app
//      (mismo disco: después solo hay que renombrar), comprobando el CRC-32 de
//      cada archivo. Los que ya son idénticos a los instalados se saltan.
//   2. Cada archivo instalado que cambia se renombra a "<nombre>.cola-old"
//      (Windows no deja sobrescribir un .exe en ejecución, pero sí renombrarlo)
//      y el nuevo ocupa su lugar.
//   3. Si algo falla a mitad, se deshace todo en orden inverso: la carpeta
//      queda como estaba.
// La app sigue corriendo con el código viejo que ya tiene en memoria; la
// versión nueva se usa al volver a abrirla. Los ".cola-old" se borran en el
// siguiente arranque (CleanupOldFiles).
//
// Nunca se tocan los archivos propios de cada copia o de cada usuario
// (IsProtectedFile): servidor-laimi.txt, portable.txt, config.json,
// historial.json y app.log, aunque el paquete los trajera.
//
// ReadPackage, IsSafeRelativePath, IsProtectedFile y ExtractEntry son lógica
// pura sobre un búfer; InstallPackage y CleanupOldFiles tocan el disco. Todo
// tiene pruebas en tests/ (las de disco, en una carpeta temporal).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cola::update {

// Sufijo de los archivos reemplazados que se borran en el siguiente arranque.
inline constexpr wchar_t kOldSuffix[] = L".cola-old";
// Carpeta temporal de extracción y archivo de bloqueo, dentro de la carpeta de la app.
inline constexpr wchar_t kStagingDir[] = L".actualizacion-tmp";
inline constexpr wchar_t kLockFile[] = L".actualizacion.lock";

// Una entrada (archivo) del paquete.
struct PackageEntry {
    std::wstring path;             // ruta relativa con "\" (ya validada)
    std::uint16_t method = 0;      // 0 = sin comprimir, 8 = DEFLATE
    std::uint32_t crc = 0;         // CRC-32 del contenido sin comprimir
    std::uint32_t compressedSize = 0;
    std::uint32_t size = 0;        // tamaño sin comprimir
    size_t dataOffset = 0;         // posición de los datos dentro del ZIP
};

// true si `path` (con "/" o "\") es una ruta relativa que no puede salir de la
// carpeta de destino: sin unidad ni barra inicial, sin componentes vacíos, "."
// ni "..", sin caracteres reservados de Windows ni nombres de dispositivo
// (CON, NUL, COM1...), y de menos de 200 caracteres.
bool IsSafeRelativePath(const std::wstring& path);

// true si el archivo es propio de cada copia o usuario y la actualización no
// debe tocarlo (se compara el nombre, sin importar mayúsculas ni la carpeta).
bool IsProtectedFile(const std::wstring& path);

// Lee el directorio central del ZIP. Las carpetas se omiten. false (con el
// motivo en `error`) si no es un ZIP válido o tiene algo que no se acepta:
// rutas inseguras o repetidas, entradas cifradas, ZIP64 o un método de
// compresión distinto de 0 y 8, o datos fuera del archivo.
bool ReadPackage(std::string_view zip, std::vector<PackageEntry>& entries, std::wstring* error = nullptr);

// Extrae una entrada a memoria y comprueba tamaño y CRC-32.
bool ExtractEntry(std::string_view zip, const PackageEntry& entry, std::string& out);

// Resultado de InstallPackage.
struct InstallResult {
    bool ok = false;
    // No se pudo escribir en la carpeta de la app (sin permiso): la app avisa
    // en vez de reintentar.
    bool accessDenied = false;
    // Otra copia de la app está instalando en esa carpeta en este momento.
    bool busy = false;
    int replaced = 0;     // archivos nuevos o cambiados
    int unchanged = 0;    // archivos que ya eran idénticos
    std::wstring error;   // motivo legible si !ok
};

// Instala el paquete en `targetDir` (la carpeta del .exe) como se explica
// arriba. El paquete debe traer "ColaDeImpresion.exe" en la raíz (comprueba
// que es un paquete de esta app). Bloquea mientras trabaja: llamar desde un
// hilo de fondo.
InstallResult InstallPackage(std::string_view zip, const std::wstring& targetDir);

// true si se puede crear un archivo en la carpeta (se prueba creando y
// borrando el archivo de bloqueo). Sirve para no descargar un paquete que
// después no se podría instalar.
bool CanWriteTo(const std::wstring& targetDir);

// Borra los "*.cola-old" que dejó una actualización anterior (los que sigan en
// uso por otra sesión se dejan para la próxima vez) y la carpeta temporal si
// quedó de una instalación interrumpida. Recorre hasta 4 niveles de carpetas.
void CleanupOldFiles(const std::wstring& targetDir);

}  // namespace cola::update
