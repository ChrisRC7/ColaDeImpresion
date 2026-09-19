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
- **Retención automática**: cada trabajo nuevo queda en pausa ("Retenido") hasta que un operador lo libera.
- **Trabajos**: documento, usuario, equipo, páginas, tamaño, estado, posición, hora. Pausar, reanudar, reiniciar, cancelar (uno, varios o todos los de un usuario), subir/bajar en la cola, **reenviar a otra impresora**.
- **Impresoras**: pausar/reanudar/vaciar cola; avisos de sin papel, atasco, sin tóner, fuera de línea...
- **Vista previa** del archivo de spool: EMF nativo; PostScript, PDF, PCL y XPS con Ghostscript/GhostPCL/GhostXPS (opcionales, autodetectados). Zoom, arrastre y paso de páginas. Caché de vistas previas.
- **Atajos de teclado configurables** (F1 muestra y permite reasignarlos).
- **Modo claro / oscuro** (sigue al sistema o manual), contraste verificado WCAG AA.
- **Historial** con estadísticas de páginas por día/semana, **notificaciones**, **bandeja del sistema**, **inicio con Windows**, **español / inglés**.

## Uso rápido

| Tecla | Acción |
|---|---|
| `↑` / `↓` | Navegar trabajos (`Shift` para seleccionar varios) |
| `Ctrl+P` / `Ctrl+R` | Pausar / reanudar |
| `Supr` | Cancelar |
| `Ctrl+↑` / `Ctrl+↓` | Subir / bajar en la cola |
| `Espacio` o `Enter` | Vista previa rápida |
| `AvPág` / `RePág` | Página siguiente / anterior |
| `Ctrl+T` | Reenviar a otra impresora |
| `Ctrl+E` | Elegir impresora (`Ctrl+Tab` para alternar) |
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
**"Administrar documentos" / "Administrar esta impresora"** en el servidor. Para
la **vista previa** hay que poder leer la carpeta de spool del servidor
(`\\SERVIDOR\C$\Windows\System32\spool\PRINTERS`), es decir, ser administrador
del servidor, o configurar una carpeta alternativa en `config.json`
(`spoolDirOverrides`). El menú ofrece **"Reiniciar como administrador"**.

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
tests/       pruebas unitarias      tools/  generador de trabajos de prueba
docs/        documentación          installer/  Inno Setup
```

## Documentación para continuar el desarrollo

- [docs/GUIA_IA.md](docs/GUIA_IA.md) — modelo de trabajo (para personas y asistentes de IA).
- [docs/CONTEXTO.md](docs/CONTEXTO.md) — contexto vivo: decisiones, arquitectura, estado, bitácora.
- [docs/PRUEBAS.md](docs/PRUEBAS.md) — plan de pruebas manuales en el laboratorio.
- [TODO.md](TODO.md) — tareas hechas y pendientes.
