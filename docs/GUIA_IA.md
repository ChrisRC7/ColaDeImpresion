# Guía de trabajo para asistentes de IA

Este documento define **cómo se trabaja** en este proyecto. Cualquier IA (o
persona) que continúe el desarrollo debe seguirlo para mantener el mismo modelo
de trabajo. El **qué** (estado del proyecto, decisiones, arquitectura) está en
[CONTEXTO.md](CONTEXTO.md).

---

## 1. Antes de empezar (orden de lectura obligatorio)

1. **[docs/CONTEXTO.md](CONTEXTO.md)** — propósito, entorno (LAIMI, servidor de impresión), decisiones, arquitectura, estado y bitácora.
2. **[TODO.md](../TODO.md)** — tareas hechas `[x]`, pendientes `[ ]` y notas.
3. Este documento completo.
4. Los encabezados (`.h`) del módulo que vaya a tocar: cada uno empieza con un comentario que explica su responsabilidad.

## 2. Reglas del proyecto (no negociables)

| Regla | Detalle |
|---|---|
| **Commits sin coautor de IA** | Nunca agregar `Co-Authored-By:` ni ninguna firma de IA en commits o PRs. El dueño no quiere que aparezcan IAs como colaboradores en GitHub. |
| **Mantener `TODO.md` al día** | Al terminar cada tarea: marcarla `[x]`, agregar subtareas descubiertas como `[ ]`, y anotar en "Notas" lo que quede a medias. |
| **Mantener `docs/CONTEXTO.md` al día** | Cada decisión técnica nueva, módulo nuevo, limitación descubierta o cambio de estado se registra ahí, y se agrega una línea a la **Bitácora**. |
| **Idioma** | Documentación, comentarios del código, mensajes de commit y textos de UI en **español**. Identificadores del código en **inglés**. |
| **C++ con Win32 API** | Sin frameworks de UI (nada de Qt, MFC, WinUI, Electron). Dependencias externas solo si son imprescindibles y se justifican en CONTEXTO.md. |
| **Modularidad** | Respetar las capas (ver §4). Un archivo = una responsabilidad. Si un `.cpp` pasa de ~800 líneas, dividirlo. |
| **Pruebas** | Toda lógica pura nueva lleva prueba en `tests/`. `cola_tests.exe` debe pasar al 100 % antes de cada commit. |
| **Compilar sin warnings** | GCC con `-Wall -Wextra` y MSVC `/W4`. |

## 3. Flujo de trabajo por tarea

1. **Elegir** una tarea pendiente de `TODO.md` (o la que pida el usuario).
2. **Entender** el módulo afectado leyendo su `.h` y CONTEXTO.md.
3. **Implementar** en el módulo correcto (ver §4 y §5).
4. **Compilar y probar**:
   ```powershell
   .\scripts\build.ps1 -Config Debug     # compila + ejecuta pruebas
   ```
   o manualmente:
   ```bash
   export PATH="/d/Programacion/Herramientas/winlibs/mingw64/bin:$PATH"
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build && ./build/cola_tests.exe
   ```
5. **Verificar en la app real** cuando el cambio es visible (ver §7).
6. **Documentar**: actualizar `TODO.md`, `docs/CONTEXTO.md` (bitácora + lo que corresponda) y, si cambia el uso, `README.md` / `docs/MANUAL.md`.
7. **Commit** (ver §8). Un commit por unidad lógica de trabajo.

## 4. Arquitectura en capas

```
app/      ← orquestación: MainWindow, Application, acciones, RowBuilder
  ├── ui/       ← controles Win32 propios (solo dibujan y avisan por callbacks)
  ├── input/    ← comandos y atajos
  ├── preview/  ← spool, formatos, renderizado, caché, reimpresión
  ├── core/     ← spooler de Windows, monitor, estados, JobTracker
  ├── config/   ← JSON, ajustes, historial, rutas, autoarranque
  ├── i18n/     ← textos es/en
  └── util/     ← cadenas, formato, log, hilos de trabajo, ComPtr
```

Dependencias permitidas (de arriba hacia abajo, **nunca** al revés):

- `util` no depende de nada del proyecto.
- `i18n` → nada. `config` → `util`.
- `core` → `config`, `util` (no conoce UI ni preview).
- `preview` → `core` (solo tipos/spooler), `i18n`, `config`, `util`.
- `input` → `i18n`. `ui` → `core` (tipos), `preview` (tipos), `input`, `i18n`, `config`, `util`.
- `app` → todo.

**Hilos**: la UI corre en el hilo principal. Todo lo que llama al spooler o
renderiza documentos va en hilos de fondo (`QueueMonitor`, `PreviewService`,
`JobActionRunner`, todos con `util::WorkQueue` o hilo propio). Los resultados
vuelven a la UI con `PostMessage` (ver `PostOwned` en `MainWindow.cpp`). Nunca
llamar a `OpenPrinter/EnumJobs/SetJob` desde el hilo de la UI.

## 5. Recetas frecuentes

### Agregar un texto de interfaz
1. Añadir `X(MiTexto, L"español", L"inglés")` en `src/i18n/StringTable.inl`.
2. Usar `i18n::Tr(i18n::S::MiTexto)`. Con parámetros: `util::Format(Tr(S::MiTexto), ...)`.

### Agregar un comando con atajo de teclado
1. Añadir el valor al enum en `src/input/Commands.h`.
2. Añadir la fila en `kCommandTable` (`src/input/Commands.cpp`) **en el mismo orden**, con id estable (se guarda en config.json), texto y atajo por defecto.
3. Añadir el texto `CmdXxx` en `StringTable.inl`.
4. Implementar el `case` en `MainWindow::Execute`.
5. El panel de ayuda (F1) y la reasignación funcionan solos. La prueba `NoDuplicateDefaultShortcuts` detecta conflictos.

### Agregar una opción de configuración
1. Campo en `config::Settings` (`Settings.h`).
2. Leer en `FromJson` (con valor por defecto y `clamp` si es numérico) y escribir en `ToJson`.
3. Si es visible, agregar la entrada al menú en `MainWindow::ShowMainMenu` / `HandleMenuCommand`.
4. Ampliar la prueba `SettingsRoundTrip`.

### Agregar un formato de vista previa
1. Detección en `DetectFormat` (`preview/SpoolFile.cpp`) + prueba en `DetectPrinterLanguages`.
2. Generación en `BuildPreview` (`preview/PreviewBuilder.cpp`) produciendo `PreviewPage`s (`EmfPage` o `BitmapPage`).
3. Si también se puede reenviar a otra impresora, tratarlo en `JobActionRunner::Redirect`.

### Agregar un control de UI
Derivar de `ui::Window`, pintar con `gfx::BufferedPaint` en `WM_PAINT`, tomar
colores **solo** de `Style::Get().palette()` y medidas con `Style::Get().Px(dip)`,
exponer `OnStyleChanged()` y comunicar eventos por callbacks (`std::function`).

## 6. Trampas conocidas (leer antes de tocar código)

- **`%ls` siempre** para cadenas anchas en `util::Format`/logs (no `%s`): con MinGW `%s` no es portable.
- **Macros de `winspool.h`/`windows.h`**: nombres como `SetPrinter`, `GetJob`, `StartDoc` son macros. No usar esos nombres para métodos propios (por eso existe `HeaderBar::SetPrinterLabel`).
- **`WIN32_LEAN_AND_MEAN`** está definido: incluir explícitamente `<objbase.h>`, `<shellapi.h>`, `<commctrl.h>`, `<winspool.h>` según haga falta.
- **Nombres de spool**: Windows usa *spool file pooling* (`FPnnnnn.SPL`); el número no es el id del trabajo. Ver `FindPooledSpoolFile`.
- **Permisos**: leer la carpeta de spool y reordenar la cola requieren permisos de administración. En una PC sin esos permisos las pruebas de vista previa usan `spoolDirOverrides` + `cola_testjob --fake-spool`.
- **Client Side Rendering**: en impresoras compartidas el servidor suele recibir RAW (PCL/PS) ya renderizado; sin Ghostscript/GhostPCL no hay vista previa de esos trabajos (es esperado).
- **PowerShell y `$null`**: al pasar `$null` a un `string` en P/Invoke llega `""`; usar `[NullString]::Value`.
- **Pruebas de UI automatizadas**: no simular la tecla Alt sola (abre el menú de sistema y "se traga" las demás teclas). Si el usuario está usando el equipo, **no** inyectar teclado y **no** capturar la pantalla completa: capturar solo la ventana de la app (`PrintWindow`).
- **Macro `CHECK_EQ`**: copia sus argumentos; no cambiarla a referencias (en Release se leían temporales destruidos).

## 7. Cómo verificar en la app real

1. Compilar Debug y crear `build\portable.txt` para que la configuración quede en `build\` y no en `%APPDATA%`.
2. Crear trabajos de prueba que quedan en cola sin imprimir:
   ```powershell
   .\build\cola_testjob.exe "Microsoft Print to PDF" --jobs 3 --pages 5 --seconds 300 --pause `
       --output "$env:TEMP\salida.pdf" --fake-spool "$env:TEMP\spool"
   ```
   y en `build\config.json` poner `"spoolDirOverrides": { "": "C:\\...\\Temp\\spool" }`.
3. Abrir `build\ColaDeImpresion.exe`, probar y revisar `build\app.log`.
4. Al terminar, cerrar la herramienta de prueba (aborta los trabajos) y verificar con
   `Get-PrintJob -PrinterName "Microsoft Print to PDF"` que no quedaron trabajos.

## 8. Commits

- Mensaje en español: primera línea corta en imperativo o descriptiva (≤ 72 caracteres), línea en blanco, detalle en viñetas si hace falta.
- **Sin** `Co-Authored-By` ni firmas de IA.
- Antes de commitear: compila sin warnings, `cola_tests` en verde, `TODO.md` y `docs/CONTEXTO.md` actualizados.
- No hacer `push` salvo que el usuario lo pida.

## 9. Definición de "terminado"

Una tarea está terminada cuando: compila sin warnings en Debug y Release, las
pruebas pasan, se verificó en la app si es visible, `TODO.md` y `CONTEXTO.md`
reflejan el cambio y está commiteada.
