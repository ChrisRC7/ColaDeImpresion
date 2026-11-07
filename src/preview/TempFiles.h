#pragma once
// Archivos temporales de vista previa (contenido potencialmente confidencial).
//
// Algunos pasos necesitan un archivo en disco (Ghostscript/GhostPCL/GhostXPS,
// la miniatura del Shell). Para no dejar copias de documentos de estudiantes:
//   - se crean en una carpeta privada por proceso
//     (%TEMP%\ColaDeImpresion\s<pid>-<tick>) con una DACL protegida que solo
//     da acceso al usuario actual (sin herencia: ni otros usuarios del equipo
//     ni el grupo Administradores por herencia);
//   - cada archivo se borra en cuanto se usa;
//   - al salir se borra la carpeta, y al iniciar se borran las carpetas que
//     hayan quedado de ejecuciones anteriores (cierre forzado, corte de luz) y
//     los archivos sueltos que dejaban versiones anteriores de la app.
// Ver D27.
//
// Hilos: PrivateTempDir / NewTempPath se llaman desde cualquier hilo de
// fondo (la creación de la carpeta va con candado y el contador es atómico).
// CleanupStaleTempFiles y RemovePrivateTempDir los llama Application al
// iniciar y al salir, en el hilo principal.

#include <string>

namespace cola::preview {

// Carpeta privada del proceso (se crea la primera vez). "" si no se pudo.
// Si falla no se reintenta en este proceso (se registra en el log una vez):
// sin carpeta privada no se crean temporales en ningún otro lado.
std::wstring PrivateTempDir();

// Ruta nueva y única dentro de la carpeta privada ("" si no hay carpeta).
// extension: con el punto (".xps") o nullptr/"" para ninguna. Solo arma el
// nombre ("t<n><ext>"); el archivo lo crea el llamador.
std::wstring NewTempPath(const wchar_t* extension);

// Borra la carpeta privada del proceso (llamar al salir).
// Después de esto, PrivateTempDir la volvería a crear si se la pide.
void RemovePrivateTempDir();

// Borra restos de ejecuciones anteriores (llamar al iniciar).
void CleanupStaleTempFiles();

}  // namespace cola::preview
