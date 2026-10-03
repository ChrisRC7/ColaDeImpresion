# Cola de Impresión — LAIMI

Gestor de colas de impresión para Windows, hecho en **C++ con Win32 API**, para
que el personal del **LAIMI (Instituto Tecnológico de Costa Rica)** controle las
colas del servidor de impresión: ver en tiempo real qué se está imprimiendo,
previsualizar los documentos, pausar, cancelar, reordenar o reenviar trabajos a
otra impresora, todo desde el teclado y con modo claro/oscuro.

![Plataforma](https://img.shields.io/badge/Windows-10%20%2F%2011-blue) ![Lenguaje](https://img.shields.io/badge/C%2B%2B-20-informational)

## Funciones

- **Servidores remotos** (`\\SERVIDOR`) o impresoras del propio equipo; vista de todas las impresoras o de una sola.
- **Tiempo real**: notificaciones del spooler + sondeo de respaldo.
- **Retención automática**: cada trabajo nuevo queda en pausa ("Retenido") hasta que un operador lo libera; al iniciar también se pausan las impresoras y los trabajos ya encolados.
- **Selección como en el Explorador**: clic, `Ctrl`/`Shift`+clic, arrastre con rectángulo, y clic en el vacío para no seleccionar nada.
- **Trabajos**: documento, usuario, equipo, páginas, tamaño, estado, posición, hora. Pausar, reanudar, reiniciar, cancelar (uno, varios o todos los de un usuario), subir/bajar en la cola, **reenviar a otra impresora**.
- **Impresoras**: pausar/reanudar/vaciar cola; avisos de sin papel, atasco, sin tóner, fuera de línea...
- **Acceso rápido a impresoras**: barra superior con las impresoras que el operador fija (botón `+`), cada una con su cantidad de trabajos y su estado; un clic o `Ctrl+1`…`Ctrl+9` filtran la cola por esa impresora.
- **Tabla como la cola de Windows**: un clic en el encabezado ordena por esa columna y otro clic invierte (documento, estado, usuario, equipo, impresora, páginas, papel, orientación, doble cara, origen del papel, tamaño, enviado; `#` = orden real de impresión). También `F6` / `Shift+F6`.
- **Columnas redimensionables** arrastrando el borde del título (doble clic = ajustar al contenido), con barra horizontal si no caben; los anchos se guardan.
- **Botones de pausa** junto a la búsqueda: pausar / reanudar los trabajos seleccionados y la impresora.
- **Páginas a imprimir** en la lista: páginas × copias (3 págs. con 3 copias = 9), con las copias leídas de los datos del trabajo también para los enviados desde otras PCs.
- **Panel de detalles** (vista previa y configuración) oculto al iniciar; se abre con el botón de la cabecera, `Ctrl+B` o `F7`.
- **Vista previa** de los datos del trabajo, leídos con la API del spooler (`OpenPrinter("Cola,Job N")` + `ReadPrinter`; basta con "Administrar documentos"): EMF nativo; PostScript, PDF, PCL y XPS con Ghostscript/GhostPCL/GhostXPS (opcionales, autodetectados). Zoom, arrastre y paso de páginas. Caché de vistas previas. Los datos ESC/Page de las Epson del LAIMI no tienen vista previa (se explica por qué).
- **Configuración de impresión por trabajo** (`F7`): papel, orientación, doble cara, copias, intercalado, color, páginas lógicas, caras y hojas previstas, cada valor con su origen y su fiabilidad (*Fiable / Estimado / No determinable*). Ver [docs/DATOS_DE_IMPRESION.md](docs/DATOS_DE_IMPRESION.md).
- **Atajos de teclado configurables** (F1 muestra y permite reasignarlos).
- **Modo claro / oscuro** (sigue al sistema o manual), contraste verificado WCAG AA.
- **Historial** con estadísticas de páginas por día/semana, **notificaciones**, **bandeja del sistema**, **inicio con Windows**, **español / inglés**.

## Uso rápido

| Tecla | Acción |
|---|---|
| `↑` / `↓` | Navegar trabajos (`Shift` para seleccionar varios) |
| `Ctrl+P` / `Ctrl+R` | Pausar (alterna) / reanudar el trabajo |
| `Ctrl+Shift+P` o `F9` | Pausar / reanudar la impresora |
| `Ctrl+Alt+P` o `F8` | Pausar / reanudar el trabajo **y** su impresora |
| `Ctrl+Enter` o `F11` | Imprimir los seleccionados y volver a pausar la impresora al terminar |
| `Supr` | Cancelar |
| `Ctrl+↑` / `Ctrl+↓` | Subir / bajar en la cola |
| `Espacio` o `Enter` | Vista previa rápida |
| `AvPág` / `RePág` | Página siguiente / anterior |
| `Ctrl+T` | Reenviar a otra impresora |
| `Ctrl+E` | Elegir impresora (`Ctrl+Tab` para alternar) |
| `Ctrl+1` … `Ctrl+9` | Impresoras del acceso rápido |
| `Ctrl+Shift+F` | Fijar / quitar la impresora del acceso rápido |
| `F6` / `Shift+F6` | Cambiar / invertir el orden de la cola |
| `F7` | Vista previa / configuración de impresión del trabajo |
| `Ctrl+O` | Conectar a un servidor |
| `Ctrl+F` | Buscar / filtrar |
| `Ctrl+H` | Historial |
| `Ctrl+Shift+H` | Activar / desactivar la retención de trabajos nuevos |
| `Ctrl+D` | Modo claro / oscuro |
| `F1` o `?` | Todos los atajos (y reasignarlos con teclado o clic) |

Manual completo: [docs/MANUAL.md](docs/MANUAL.md).

## Permisos

La app se ejecuta con los permisos del usuario. Para gestionar trabajos de
**otros** usuarios, reordenar la cola o pausar impresoras hace falta el permiso
**"Administrar documentos" / "Administrar esta impresora"** en el servidor. La
**vista previa** y la **configuración** de cada trabajo usan el mismo permiso
"Administrar documentos": los datos se leen con la API del spooler, sin ser
administrador del servidor. Solo si esa vía falla se intenta leer el archivo de
spool (por la ruta local en el propio servidor o por `\\SERVIDOR\C$`, que exige
ser administrador). El menú ofrece **"Reiniciar como administrador"**. Si no se
pueden leer los datos, el panel indica qué se intentó y por qué falló (lo mismo
queda en `app.log`).

## Compilar

Requisitos: CMake ≥ 3.20 y **MinGW-w64** (p. ej. [WinLibs](https://winlibs.com)) o **Visual Studio 2022**.

```powershell
.\scripts\build.ps1              # Release + pruebas  → build-release\ColaDeImpresion.exe
.\scripts\build.ps1 -Package     # además arma dist\
.\scripts\build.ps1 -Installer   # además genera el instalador (Inno Setup 6)
```

Manual con CMake:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/cola_tests.exe
```

El ejecutable es autónomo (sin DLLs adicionales). Para usarlo sin instalar,
basta copiar `ColaDeImpresion.exe`; si junto a él existe `portable.txt`, la
configuración se guarda en esa misma carpeta.

## Vista previa de PCL / PostScript / PDF / XPS (opcional)

`.\scripts\get-renderers.ps1` descarga Ghostscript, GhostPCL y GhostXPS
(Artifex, verifica la firma) en `third_party\renderers`, y luego
`.\scripts\build.ps1 -Package` los incluye en `dist\renderers\` junto con el
runtime de Visual C++ que necesitan. También se puede instalar
[Ghostscript](https://ghostscript.com/releases/) o copiar `gswin64c.exe`,
`gpcl6win64.exe`, `gxpswin64.exe` (con sus DLL) en `renderers\` junto al ejecutable. Se detectan automáticamente; también
se pueden indicar en `config.json` → `"renderers"`.

## Estructura

```
src/app      orquestación (ventana principal, acciones, arranque)
src/ui       controles Win32 propios, tema, DPI
src/core     spooler de Windows, monitor en tiempo real, estados
src/preview  lectura del spool, formatos, renderizado, reenvío
src/input    comandos y atajos
src/config   JSON, ajustes, historial
src/i18n     textos es/en
src/util     utilidades
tests/       pruebas unitarias      tools/  trabajos de prueba e inspección (cola_jobinspect)
docs/        documentación          installer/  Inno Setup
```

## Documentación para continuar el desarrollo

- [docs/GUIA_IA.md](docs/GUIA_IA.md) — modelo de trabajo (para personas y asistentes de IA).
- [docs/CONTEXTO.md](docs/CONTEXTO.md) — contexto vivo: decisiones, arquitectura, estado, bitácora.
- [docs/PRUEBAS.md](docs/PRUEBAS.md) — plan de pruebas manuales en el laboratorio.
- [TODO.md](TODO.md) — tareas hechas y pendientes.
