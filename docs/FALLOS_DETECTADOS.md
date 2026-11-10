# Fallos detectados y mejoras propuestas en la revisión de código (0.5.17)

Resultado de una revisión de lectura del código de la versión 0.5.17
(2026-11-09). Para cada fallo se indica **qué pasa**, **cómo corregirlo** y
**cómo comprobar que la corrección funciona**. Al final están las
[mejoras propuestas](#mejoras-propuestas) (X1–X6), con cómo implementarlas y
cómo saber si funcionaron.

- Ninguno está corregido todavía: este documento es el plan de trabajo.
- La revisión fue por lectura: no se compiló ni se ejecutó la app. Los fallos
  están razonados sobre el código, no reproducidos. El primer paso de cada
  corrección es reproducirlo (o escribir la prueba que falla).
- No se leyeron completos los controles de dibujo de `ui/` (lista, panel de
  vista previa, ayuda, diálogos), `JobSheet`, `XpsPackage` ni la primera mitad
  de `PdlSettings`.
- Al corregir, seguir [GUIA_IA.md](GUIA_IA.md): prueba unitaria para toda
  lógica pura, `cola_tests.exe` en verde, sin warnings, versión nueva y paquete.

## Resumen

| # | Fallo | Gravedad | Archivo principal |
|---|---|---|---|
| F1 | Memoria liberada en "Cancelar todos los de \<usuario\>" | Alta | `app/MainWindow.cpp` |
| F2 | El servidor del LAIMI se sobrescribe sin querer | Alta | `app/MainWindow.cpp` |
| F3 | Historial dice "Cancelado" en trabajos que se imprimieron | Media | `app/MainWindow.cpp`, `core/JobTracker` |
| F4 | Un reenvío parcial borra el trabajo original | Alta | `app/JobActions.cpp` |
| F5 | El nombre del documento se abre como ruta en la PC del operador | Alta (seguridad) | `preview/PreviewBuilder.cpp` |
| F6 | Un fallo de la consulta directa genera eventos falsos y re-pausa | Media | `core/QueueMonitor.cpp` |
| F7 | Reconectar al mismo servidor vuelve a retener todo | Media | `app/MainWindow.cpp` |
| M1 | Se pierde la selección en el historial | Baja | `app/RowBuilder.cpp` |
| M2 | `config.json` ilegible un instante = preferencias perdidas | Baja | `config/Settings.cpp` |
| M3 | Tras cancelar un apagado, la X cierra la app | Baja | `app/MainWindow.cpp` |
| M4 | Un envío RAW fallido deja un trabajo a medias en el destino | Baja | `preview/Reprint.cpp` |
| M5 | Vista previa colgada en "Cargando…" tras una excepción | Baja | `preview/PreviewService.cpp` |
| M6 | La retención no reintenta una pausa que falló | Media | `core/QueueMonitor.cpp` |

Orden sugerido: F1, F2 y F7 (cambios pequeños y seguros), luego F5, F3, F4 y
F6 (requieren pruebas nuevas), y al final los menores.

## Cómo preparar el entorno de prueba

Todas las comprobaciones manuales parten de lo mismo (detalle en
[GUIA_IA.md](GUIA_IA.md) §7):

1. Compilar Debug: `.\scripts\build.ps1 -Config Debug`.
2. Crear `build\portable.txt` y un `build\config.json` propio. Si la prueba no
   necesita la retención, poner `"autoHoldNewJobs": false` y
   `"pauseAllOnStart": false` para no pausar impresoras reales.
3. Abrir con
   `build\ColaDeImpresion.exe --instancia-adicional --config build\config.json`.
4. Crear trabajos que queden en cola sin imprimirse:
   ```powershell
   .\build\cola_testjob.exe "Microsoft Print to PDF" --jobs 3 --pages 5 --seconds 300 --pause `
       --output "$env:TEMP\salida.pdf" --fake-spool "$env:TEMP\spool"
   ```
5. Revisar `build\app.log` después de cada prueba y, al terminar, comprobar con
   `Get-PrintJob -PrinterName "Microsoft Print to PDF"` que no quedaron trabajos.

---

## F1. Memoria liberada en "Cancelar todos los de \<usuario\>"

**Dónde:** `MainWindow::ShowJobContextMenu`,
[MainWindow.cpp:2035-2043](../src/app/MainWindow.cpp#L2035-L2043).

**Qué pasa:** `focused` es un puntero a un trabajo de la instantánea actual.
`TrackPopupMenu` tiene su propio bucle de mensajes y, mientras el menú está
abierto, procesa los `WM_APP_SNAPSHOT` que llegan (uno por sondeo, cada 3 s).
`OnSnapshot` reemplaza `snapshot_` y la anterior se libera. Al elegir
*Cancelar todos los de \<usuario\>* se lee `focused->user` de memoria ya
liberada: puede cancelar los trabajos equivocados, no cancelar nada o cerrar
la app.

**Cómo corregirlo:** copiar lo que se necesita antes de abrir el menú y no usar
el puntero después.

```cpp
const core::JobInfo* focused = FocusedJob();
const std::wstring focusedUser = focused ? focused->user : std::wstring();
// ... armar el menú con focusedUser ...
UINT id = TrackPopupMenu(...);
// desde aquí `focused` no se usa más
case ID_CTX_CANCEL_USER:
    if (!focusedUser.empty() && snapshot_) { /* filtrar por focusedUser */ }
```

Revisar de paso los otros menús de `MainWindow.cpp` que llaman a
`TrackPopupMenu`: `StartRedirect` ya copia el trabajo en `redirectJob_`.

**Cómo comprobarlo:**

- *Reproducir antes de corregir:* compilar Debug con AddressSanitizer si el
  compilador lo permite (con MSVC, `/fsanitize=address`); con MinGW no hay
  ASan en Windows, así que se usa la prueba manual. Crear trabajos de dos
  usuarios, abrir el menú contextual, **esperar más de 5 s** con el menú
  abierto y elegir la opción.
- *Después de corregir:* la misma prueba debe cancelar exactamente los
  trabajos de ese usuario (comparar con `Get-PrintJob`), también si durante la
  espera se agregó o quitó un trabajo de la cola.
- *Revisión estática:* `Grep` de `TrackPopupMenu` en `MainWindow.cpp` y
  confirmar que ningún puntero obtenido de `snapshot_` antes del menú se usa
  después.

## F2. El servidor del LAIMI se sobrescribe sin querer

**Dónde:** `editingLaimi_` en
[MainWindow.cpp:2120-2123](../src/app/MainWindow.cpp#L2120-L2123),
[1371](../src/app/MainWindow.cpp#L1371) y
[2349-2356](../src/app/MainWindow.cpp#L2349-L2356).

**Qué pasa:** *Cambiar servidor del LAIMI…* pone `editingLaimi_ = true`, y solo
`Esc` o `Enter` dentro del cuadro la apagan. El cuadro no sale del modo
servidor al perder el foco. Secuencia que falla: elegir *Cambiar servidor del
LAIMI…*, hacer clic en la lista, pulsar `Ctrl+O`, escribir otro servidor y
`Enter`. La marca sigue activa y ese nombre se guarda con `SetLaimiServer` en
`servidor-laimi.txt`, que comparten todos los usuarios de la copia portable.

**Cómo corregirlo:** que la marca valga solo para la edición que la activó.

- En `Execute`, `Command::Connect` y `Command::Find` ponen
  `editingLaimi_ = false` antes de tocar el cuadro.
- Mejor aún: quitar el booleano de `MainWindow` y que `HeaderBar` recuerde el
  propósito del modo servidor (`BeginServerInput(texto, placeholder, propósito)`
  y un `ServerInputPurpose()`), de modo que no puedan desincronizarse.

**Cómo comprobarlo:**

- Copiar `servidor-laimi.txt` antes de empezar (o usar una carpeta de prueba).
- Repetir la secuencia de arriba. Antes de corregir, el archivo cambia; después
  no cambia y la app solo se conecta al servidor escrito.
- Comprobar que el camino normal sigue funcionando: *Cambiar servidor del
  LAIMI…*, escribir, `Enter` → el archivo cambia y la barra lo avisa.
- Variantes: salir con `Ctrl+F` en vez de con un clic; `Esc` y volver a entrar.
- Si se mueve el estado a `HeaderBar`, agregar una prueba unitaria del cambio
  de propósito en `tests/test_core_input_ui.cpp` (si la clase se puede crear
  sin ventana; si no, dejarlo en prueba manual y anotarlo).

## F3. Historial dice "Cancelado" en trabajos que se imprimieron

**Dónde:** las cuatro llamadas a `tracker_.MarkCancelledByUser` en
[MainWindow.cpp:1523](../src/app/MainWindow.cpp#L1523),
[1549](../src/app/MainWindow.cpp#L1549),
[1638](../src/app/MainWindow.cpp#L1638) y
[2110](../src/app/MainWindow.cpp#L2110); la prioridad está en
[JobTracker.cpp:18](../src/core/JobTracker.cpp#L18).

**Qué pasa:** el trabajo se marca como "cancelado por el operador" **antes** de
saber si la acción funcionó, y `JobTracker` no tiene forma de quitar la marca.
Si la cancelación falla (acceso denegado), el vaciado de la cola falla, el
reenvío falla o el operador responde "No" a la pregunta de RAW, la marca queda.
Cuando ese trabajo se imprime después, `GuessOutcome` devuelve `Cancelled`
porque esa marca se evalúa primero. El Excel cuenta menos páginas impresas de
las reales.

**Cómo corregirlo:**

1. Agregar `JobTracker::ForgetCancelledByUser(key)` (igual que
   `ForgetReleasedByUser`).
2. `ActionResult` lleva las claves de los trabajos en los que la acción falló
   (`failedKeys`); `JobActionRunner::Run` las llena en el caso de error.
3. `MainWindow::OnActionResult` llama a `ForgetCancelledByUser` para esas
   claves cuando la acción era `Cancel`.
4. Reenvío: marcar recién cuando el resultado llega con éxito, o quitar la
   marca si falló o si el operador rechazó el RAW.
5. Vaciar cola: si `PurgePrinter` falla, quitar la marca de todos los trabajos
   de esa impresora.

**Cómo comprobarlo:**

- *Prueba unitaria* en `tests/test_core_input_ui.cpp`: marcar un trabajo,
  llamar a `ForgetCancelledByUser`, hacerlo desaparecer en la instantánea
  siguiente estando primero en una cola operativa y comprobar que el evento
  trae `Printed`. Otra prueba confirma que **sin** olvidar sigue dando
  `Cancelled` (el comportamiento correcto cuando la cancelación sí funcionó).
- *Prueba manual:* con un usuario sin "Administrar documentos", intentar
  cancelar un trabajo ajeno (aparece el aviso de acceso denegado), dejar que se
  imprima y abrir el historial: debe decir "Impreso".
- *Reenvío:* provocar el fallo eligiendo como destino una impresora sin
  conexión o respondiendo "No" al aviso de RAW; liberar el original y comprobar
  el historial.

## F4. Un reenvío parcial borra el trabajo original

**Dónde:** rama de rasterizado por lotes de `JobActionRunner::Redirect`,
[JobActions.cpp:261-283](../src/app/JobActions.cpp#L261-L283).

**Qué pasa:** las páginas se piden a Ghostscript de 6 en 6 y "un lote con menos
de 6 páginas" se interpreta como fin del documento. Un fallo o un tiempo
agotado a mitad del documento también devuelve un lote corto (o vacío). Como
ya se imprimió alguna página, `PrintPagesGdi` termina bien, el reenvío se da
por bueno y **se cancela el original**: el estudiante pierde el resto.

**Cómo corregirlo:**

- Distinguir "no hay más páginas" de "falló": `ExternalRenderResult` ya trae
  `error`; hay que hacer que un error con páginas parciales también se pueda
  detectar (hoy solo se llena si no salió ninguna). Guardar el código de salida
  y si hubo tiempo agotado.
- Si un lote falla, el proveedor de páginas marca el fallo, `PrintPagesGdi`
  aborta el documento (`AbortDoc`) y el original se restaura sin cancelarlo.
- Segunda defensa: si se conocen las páginas del trabajo
  (`ComputePageAccounting(...).dataPages` fiable) y se imprimieron menos, no
  cancelar el original; dejarlo en pausa y avisar en la barra de estado.

**Cómo comprobarlo:**

- *Prueba unitaria:* extraer la decisión a una función pura, por ejemplo
  `bool RedirectComplete(páginasImpresas, páginasEsperadas, huboError)`, y
  probar: completo, corto sin error con páginas desconocidas, corto con error,
  y menos páginas de las esperadas.
- *Prueba manual del fallo:* con un trabajo PDF/PostScript de más de 12
  páginas, sustituir temporalmente el renderizador en `config.json` por un
  script que genere las primeras 6 páginas y luego termine con error (o bajar
  el tiempo máximo a un valor que no alcance). Resultado esperado: mensaje de
  error, el original sigue en la cola en su estado anterior y en el destino no
  queda un trabajo a medias.
- *Prueba manual del caso bueno:* reenvíos de 5, 6 y 7 páginas (por debajo,
  justo en el límite y por encima del lote) deben llegar completos y cancelar
  el original.

## F5. El nombre del documento se abre como ruta en la PC del operador

**Dónde:** [PreviewBuilder.cpp:294-298](../src/preview/PreviewBuilder.cpp#L294-L298)
y `LooksLikePath` en la línea 59.

**Qué pasa:** cuando no hay páginas de vista previa (siempre en los trabajos
ESC/Page de las Epson) y el nombre del trabajo parece una ruta, la app llama a
`GetFileAttributesW` y luego la abre con WIC o con el proveedor de miniaturas
del Shell. El nombre lo pone quien imprime:

- Con `\\equipo\recurso\x.pdf`, la PC del operador se conecta por SMB a ese
  equipo y se autentica con las credenciales del operador (que administra las
  colas). Además el archivo remoto pasa por los manejadores de miniaturas.
- Con `C:\...`, se muestra un archivo de la PC del operador que por casualidad
  tiene esa ruta, no el documento del estudiante.

**Cómo corregirlo:** el respaldo solo tiene sentido si el trabajo se envió
desde este mismo equipo.

- Función pura en `preview`: `bool CanUseOriginalFile(documento, equipoDelTrabajo, esteEquipo)`
  que devuelve `true` solo si la ruta es de unidad local (`X:\`), **no** es UNC
  y el equipo de origen (`JOB_INFO_2.pMachineName` sin las barras) es este.
- `BuildPreview` la usa antes de tocar el sistema de archivos.

**Cómo comprobarlo:**

- *Prueba unitaria* en `tests/test_preview.cpp` con: ruta UNC (falso), ruta
  local de otro equipo (falso), ruta local de este equipo (verdadero), nombre
  normal (falso), mayúsculas distintas en el nombre del equipo (verdadero).
- *Prueba manual con Process Monitor* (Sysinternals): filtrar por el proceso
  `ColaDeImpresion.exe` y por rutas que empiecen con `\\`. Crear un trabajo con
  nombre `\\127.0.0.1\prueba\a.png` (`cola_testjob` permite elegir el nombre
  del documento; si no, agregarle la opción), abrir el panel de detalles sobre
  él y confirmar que **no aparece ningún acceso** a esa ruta. Antes de corregir
  sí aparece.
- Confirmar que el caso legítimo sigue funcionando: imprimir desde esta PC una
  imagen local con un visor que use la ruta como nombre.

## F6. Un fallo de la consulta directa genera eventos falsos y re-pausa

**Dónde:** [QueueMonitor.cpp:221-228](../src/core/QueueMonitor.cpp#L221-L228).

**Qué pasa:** si `EnumJobs` por el nombre DNS completo falla en un refresco, la
lista de ese refresco es la de la caché del cliente, que puede no tener todos
los trabajos (es justo lo que corrigió D28). Consecuencias en cadena:

1. `JobTracker` ve desaparecer trabajos que siguen en la cola y los registra en
   el historial como "Retirado" o "Impreso".
2. Cinco segundos después la consulta directa vuelve y esos trabajos se avisan
   como nuevos.
3. `AutoHoldTracker` los olvidó al no verlos, así que los trata como nuevos y
   **los vuelve a pausar**, aunque el operador ya los hubiera liberado.

**Cómo corregirlo:** en la rama de error de la consulta directa, no degradar a
la caché si ya se tenía una lista buena del servidor.

- `Watched` recuerda si la última lista vino del servidor (`haveServerList`).
- Si la directa falla y `haveServerList` es verdadero, conservar `w.jobs` del
  refresco anterior durante el reintento y **no** ejecutar la retención en ese
  refresco.
- Si los fallos se repiten (por ejemplo 3 seguidos), marcar la impresora con
  `ProblemNotAvailable`: `JobTracker` y `PrintReleaseTracker` ya saben esperar
  en ese estado (D52).

**Cómo comprobarlo:**

- *Prueba unitaria:* separar la decisión en una función pura
  (`ChooseJobList(servidor, errorDirecto, caché, anterior, teníaListaDelServidor)`)
  y probar los cuatro casos. Añadir una prueba de `AutoHoldTracker` +
  `JobTracker` que simule "instantánea completa → instantánea degradada →
  instantánea completa" y compruebe que no hay eventos `Finished`/`Added` ni
  trabajos devueltos por `Process`.
- *Prueba en el LAIMI:* con un trabajo liberado y en cola (impresora en pausa),
  cortar la red unos segundos o reiniciar el servicio de cola del cliente.
  Esperado: el trabajo sigue "En cola", no aparece un registro nuevo en el
  historial y `app.log` muestra "EnumJobs(...) falló" sin "Retenido: ..."
  después.

## F7. Reconectar al mismo servidor vuelve a retener todo

**Dónde:** `MainWindow::ConnectTo`,
[MainWindow.cpp:496-543](../src/app/MainWindow.cpp#L496-L543), y
`hold_.Reset()` en [QueueMonitor.cpp:337](../src/core/QueueMonitor.cpp#L337).

**Qué pasa:** `ConnectTo` llama a `monitor_->Start` aunque el servidor sea el
mismo. El hilo del monitor arranca de cero, ve todas las impresoras "por
primera vez" y, con *pausar todo al iniciar* activado (predeterminado), pausa
los trabajos que no estén en pausa, incluidos los que el operador liberó y los
que se están imprimiendo. Se llega aquí con *Conectar al servidor del LAIMI*
estando ya conectado, eligiendo el servidor actual en la lista de recientes o
escribiéndolo otra vez con `Ctrl+O`.

**Cómo corregirlo:** en `ConnectTo`, si `!changed` y el monitor ya vigila ese
servidor, llamar a `monitor_->RequestRefresh()` y salir sin reiniciarlo. (La
reconexión tras un error ya la hace el propio monitor en cada sondeo.)

**Cómo comprobarlo:**

- Con la retención y la pausa de inicio activadas contra "Este equipo": crear 3
  trabajos, liberar uno (queda "En cola" con la impresora en pausa) y elegir en
  el menú el servidor que ya está activo.
- Antes de corregir: el trabajo vuelve a "Retenido" y `app.log` muestra
  "Monitor detenido" / "Monitor iniciado" y "Retenido: ...".
- Después: el trabajo sigue "En cola" y en `app.log` no aparece un reinicio del
  monitor.
- Comprobar que cambiar a **otro** servidor sigue reiniciando el monitor y
  aplicando la pausa de inicio.

---

## M1. Se pierde la selección en el historial

**Dónde:** `HistoryKey`, [RowBuilder.cpp:454-456](../src/app/RowBuilder.cpp#L454-L456).

**Qué pasa:** la clave de cada fila es `h<hora>_<índice>` y `History::Add`
inserta al principio, así que al terminar un trabajo cambian todas las claves y
la lista pierde la selección y el foco.

**Cómo corregirlo:** usar el índice contado desde el final
(`entries.size() - 1 - i`), que no cambia al insertar. Actualizar
`UpdateSelectionTotal` y `UpdatePreview`, que reconstruyen la clave.

**Cómo comprobarlo:** prueba unitaria: la clave de un registro es la misma
antes y después de agregar otro. Manual: seleccionar una fila del historial,
dejar terminar un trabajo y ver que la selección sigue en la misma fila.

## M2. `config.json` ilegible un instante = preferencias perdidas

**Dónde:** `Settings::Load`, [Settings.cpp:182-194](../src/config/Settings.cpp#L182-L194).

**Qué pasa:** si el archivo existe pero no se puede leer en ese momento
(antivirus, otra copia guardándolo), se cargan los valores por defecto y
`ConnectTo` los guarda encima al iniciar: se pierden atajos, columnas e
impresoras fijadas.

**Cómo corregirlo:** lo mismo que `History::Load` (D52): reintentar dos veces
con 200 ms y, si sigue ilegible, copiar a `config.json.danado` antes de
permitir que se guarde encima.

**Cómo comprobarlo:** prueba unitaria con un archivo dañado: existe la copia
`.danado`. Manual: abrir `config.json` con un programa que lo bloquee en
exclusiva durante el arranque y comprobar que las preferencias siguen ahí.

## M3. Tras cancelar un apagado, la X cierra la app

**Dónde:** `WM_QUERYENDSESSION`,
[MainWindow.cpp:2578-2583](../src/app/MainWindow.cpp#L2578-L2583).

**Qué pasa:** se pone `reallyQuit_ = true` al preguntar Windows si puede cerrar
la sesión. Si el cierre se cancela (otra app lo bloquea), la marca queda y la X
cierra la app en lugar de ocultarla en la bandeja: la retención deja de
funcionar sin que nadie lo note.

**Cómo corregirlo:** guardar en `WM_QUERYENDSESSION` sin tocar la marca, y
manejar `WM_ENDSESSION`: con `wParam == TRUE` se sale; con `FALSE` no se hace
nada.

**Cómo comprobarlo:** con un documento sin guardar en el Bloc de notas, iniciar
el cierre de sesión y cancelarlo en la pantalla de "aplicaciones que impiden
cerrar". Pulsar la X: la app debe quedar en la bandeja.

## M4. Un envío RAW fallido deja un trabajo a medias en el destino

**Dónde:** `SendRawData`, [Reprint.cpp:148-151](../src/preview/Reprint.cpp#L148-L151).

**Qué pasa:** si `WritePrinter` falla a mitad, se llama a `EndDocPrinter` y lo
ya escrito queda como un trabajo incompleto que la impresora intentará
imprimir.

**Cómo corregirlo:** si hubo error, `AbortPrinter` en lugar de `EndDocPrinter`.

**Cómo comprobarlo:** reenviar por RAW un trabajo grande a una impresora de
prueba y pausar/eliminar la cola destino durante el envío (o desconectar la
red). No debe quedar un trabajo en la cola destino y el original debe seguir
en la suya.

## M5. Vista previa colgada en "Cargando…" tras una excepción

**Dónde:** la tarea de `PreviewService::Request`,
[PreviewService.cpp:52-60](../src/preview/PreviewService.cpp#L52-L60).

**Qué pasa:** si `BuildPreview` lanza (por ejemplo `std::bad_alloc` con una
imagen enorme), `WorkQueue` atrapa la excepción pero la clave queda en
`pending_` para siempre y la interfaz nunca recibe el aviso.

**Cómo corregirlo:** `try/catch` dentro de la tarea: ante una excepción se
guarda un documento con el mensaje de error y se avisa igual.

**Cómo comprobarlo:** en Debug, forzar un `throw` temporal en `BuildPreview`
para un nombre de documento concreto y ver que el panel muestra el mensaje y
que al cambiar de trabajo y volver no queda en "Cargando…". Quitar el `throw`.

## M6. La retención no reintenta una pausa que falló

**Dónde:** `QueueMonitor::ApplyAutoHold`,
[QueueMonitor.cpp:293-299](../src/core/QueueMonitor.cpp#L293-L299).

**Qué pasa:** cada trabajo se evalúa una sola vez. Si `SetJob` falla por un
error pasajero (RPC ocupado, red), el trabajo queda sin retener y se imprime;
solo lo delata el contador de la barra de estado.

**Cómo corregirlo:** si el error **no** es `ERROR_ACCESS_DENIED`, permitir un
reintento en el refresco siguiente (por ejemplo `AutoHoldTracker::Forget(printer, id)`
para que vuelva a contarse como nuevo), con un máximo de 2 o 3 intentos por
trabajo. Con acceso denegado no se reintenta: no va a cambiar.

**Cómo comprobarlo:** prueba unitaria de `AutoHoldTracker`: tras `Forget`, el
mismo trabajo vuelve a salir en `Process`; tras agotar los intentos, no.
Manual: difícil de provocar; revisar en `app.log` del LAIMI si aparece
"No se pudo retener" con un error distinto de 5 seguido de "Retenido".

---

## Lista de control al terminar cada corrección

1. La prueba unitaria nueva **falla antes** del cambio y pasa después (si pasa
   antes, no prueba el fallo).
2. `.\scripts\build.ps1 -Config Debug` y Release: sin warnings y
   `cola_tests.exe` en verde.
3. Prueba manual descrita arriba, con `app.log` revisado.
4. Comprobado que el camino normal de esa función sigue funcionando.
5. `TODO.md` y `CONTEXTO.md` actualizados, versión nueva y
   `.\scripts\build.ps1 -Package`.
6. Marcar el fallo como corregido en la tabla del resumen de este documento,
   con la versión.

---

# Mejoras propuestas

No son fallos: el código hace lo que se diseñó, pero el diseño tiene un riesgo
o un costo que conviene quitar. Varias cambian un comportamiento visible, así
que **cada una necesita el visto bueno del usuario antes de implementarla**
(se indica qué hay que decidir). La lista de control de arriba vale igual.

| # | Mejora | Beneficio | Archivo principal |
|---|---|---|---|
| X1 | Historial compartido entre varias copias de la app | No se pierden ni se pisan registros | `config/History.cpp` |
| X2 | Historial que no descarta registros en silencio | Reportes mensuales completos | `config/History.cpp` |
| X3 | Confirmar antes de borrar el historial | Evita un borrado accidental | `app/MainWindow.cpp` |
| X4 | "Iniciar con Windows" sigue a la versión instalada | Tras actualizar arranca la versión nueva | `config/Autostart.cpp` |
| X5 | La selección no salta sola a otro trabajo | Evita liberar o cancelar el trabajo de otro | `ui/JobListView.cpp` |
| X6 | No rearmar la tabla si nada cambió | Menos trabajo y parpadeo cada 3 s | `app/MainWindow.cpp` |

Orden sugerido: X3 y X4 (pequeñas y sin riesgo), X5 (decisión de uso), X2 y X1
(tocan el formato y el guardado del historial; hacerlas juntas), X6 al final.

## X1. Historial compartido entre varias copias de la app

**Dónde:** `History::Save`, [History.cpp:127-161](../src/config/History.cpp#L127-L161),
y `SaveJsonFile`, [Json.cpp:438-459](../src/config/Json.cpp#L438-L459).

**Situación actual:** en modo portable `historial.json` está junto al `.exe`.
Cada copia de la app lo lee al iniciar y después escribe **su lista completa**
cada vez que cambia. Con dos copias abiertas a la vez:

- Gana la última que escribe: lo que la otra tenía y esta no (registros de
  antes de abrirse, carnets completados) se pierde.
- El mismo trabajo queda con el resultado de la copia que guardó última. La
  marca "lo liberó el operador" (D47) solo existe en la copia donde se pulsó
  `F11`, así que un trabajo puede alternar entre "Impreso" y "Retirado".
- Las dos usan el mismo temporal `historial.json.tmp`; si coinciden, una falla
  y no reintenta hasta el siguiente cambio.

**Cómo implementarla:**

1. **Identidad del registro.** Agregar `jobId` a `HistoryEntry` (hoy no se
   guarda). Clave de fusión: servidor + impresora + `jobId` + hora de envío.
   Para los registros viejos sin `jobId`: servidor + impresora + documento +
   usuario + hora de envío.
2. **Fusionar al guardar.** `Save` vuelve a leer el archivo y une las dos
   listas por esa clave antes de escribir. Si un trabajo está en las dos:
   se queda el resultado más informativo (Impreso, Cancelado o Falló ganan a
   Retirado) y los campos no vacíos (carnet, papel…). Orden final por hora de
   finalización, y luego se aplica el límite.
3. **Temporal propio.** `SaveJsonFile` usa `<archivo>.<pid>.tmp` para que dos
   procesos no choquen, y `Save` reintenta una vez si `MoveFileEx` falla.
4. **Borrar historial.** La fusión resucitaría lo borrado: ese guardado se hace
   sin fusionar. Las otras copias abiertas volverán a escribir lo que tengan en
   memoria; documentarlo en el manual ("cerrar las demás copias antes de
   borrar") o, si hace falta, guardar la hora del borrado en el archivo y
   descartar al fusionar lo anterior a ella.

**Qué hay que decidir:** si vale la pena el paso 4 completo o basta con la nota
en el manual.

**Cómo saber si funcionó:**

- *Pruebas unitarias* en `tests/test_util_config.cpp` con dos objetos `History`
  sobre el mismo archivo temporal:
  - A agrega el trabajo 1 y guarda; B (cargado antes) agrega el trabajo 2 y
    guarda → al recargar están los dos.
  - A y B registran el mismo trabajo, A como "Impreso" y B como "Retirado" →
    queda un solo registro, "Impreso", sin importar quién guarde último.
  - Un registro viejo sin `jobId` no se duplica al fusionar.
  - Tras `Clear` + `Save` el archivo queda vacío.
- *Prueba manual:* abrir dos copias (`--instancia-adicional`, la misma carpeta
  portable, cada una con su `--config`), liberar un trabajo con `F11` en una y
  dejar que la otra solo observe. Cerrar las dos en cualquier orden: el
  historial tiene ese trabajo **una vez** y como "Impreso".
- Comprobar que un `historial.json` de la 0.5.17 se abre sin perder registros.

## X2. Historial que no descarta registros en silencio

**Dónde:** `History::Add` y `Load`,
[History.cpp:119-122](../src/config/History.cpp#L119-L122) y
[163-169](../src/config/History.cpp#L163-L169); el valor por defecto de
`historyLimit` en `config/Settings.h`.

**Situación actual:** se guardan los últimos 2000 trabajos y los más viejos se
eliminan sin aviso. Si el laboratorio imprime unos cientos de trabajos por día,
eso son pocas semanas, y un Excel "de todo el mes" sale incompleto sin que
nadie lo note.

**Cómo implementarla:**

1. **Archivar en vez de descartar.** Cuando `Add` o `Load` recortan la lista,
   los registros que salen se agregan a `historial-AAAA-MM.json` (según su
   fecha de finalización) en la misma carpeta, con el mismo formato.
2. **Exportar con los archivos.** `ExportHistory` carga también los archivos
   mensuales cuyo mes cae en el período pedido; el diálogo de exportación lista
   sus días junto con los del historial activo.
3. **Avisar.** Si no se implementa el paso 2 todavía, al menos que el diálogo
   de exportación y la hoja "Resumen" digan "el historial conserva los últimos
   N trabajos; hay registros anteriores archivados".
4. **Subir el valor por defecto** a 10 000 solo después de medir: hoy cada
   cambio reescribe el archivo entero (unos 400 bytes por registro, es decir
   unos 4 MB cada vez con 10 000).

**Qué hay que decidir:** cuántos meses de historial hacen falta para los
reportes y si los archivos mensuales deben verse también en la vista de
historial de la app o solo en el Excel.

**Cómo saber si funcionó:**

- *Pruebas unitarias:* con límite 3, agregar 5 registros de dos meses distintos
  → el historial activo tiene 3 y los otros 2 están en el archivo del mes que
  corresponde; nada se pierde (la suma es 5). Cargar un archivo con más
  registros que el límite archiva el sobrante en vez de tirarlo.
- *Prueba de exportación:* un período que abarque registros archivados produce
  un Excel con todos; comparar el total de la hoja "Resumen" con la suma
  esperada.
- *Rendimiento:* generar un historial de 10 000 registros con un programa de
  prueba y medir el tiempo de `Save` y de abrir la vista de historial; anotar
  el resultado en la bitácora antes de subir el valor por defecto.

## X3. Confirmar antes de borrar el historial

**Dónde:** `ID_CLEAR_HISTORY`,
[MainWindow.cpp:2185-2190](../src/app/MainWindow.cpp#L2185-L2190).

**Situación actual:** *Borrar historial* está en el menú principal, junto a
*Exportar a Excel*, y borra y guarda al instante. Un clic equivocado elimina
todos los registros sin posibilidad de deshacer.

**Cómo implementarla:**

- Texto nuevo en `StringTable.inl`, por ejemplo `ConfirmClearHistory`:
  "¿Borrar los %u registros del historial? No se puede deshacer."
- `MessageBoxW` con `MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2` (botón por
  defecto: No), igual que al vaciar una cola.
- Si el historial está vacío, no preguntar.
- Opcional: antes de borrar, copiar el archivo a `historial.json.borrado`
  (una sola copia, se reemplaza) para poder recuperarlo a mano.

**Cómo saber si funcionó:**

- Manual: con registros en el historial, elegir *Borrar historial* y responder
  "No" → siguen todos y `historial.json` no cambió (comparar fecha y tamaño).
  Responder "Sí" → queda vacío. `Enter` sin mover nada equivale a "No".
- Con el historial vacío no aparece la pregunta.
- La prueba de marcadores de formato de las cadenas (es/en) sigue en verde.

## X4. "Iniciar con Windows" sigue a la versión instalada

**Dónde:** [Autostart.cpp:21-49](../src/config/Autostart.cpp#L21-L49) y la
lectura en `MainWindow::Create`,
[MainWindow.cpp:218](../src/app/MainWindow.cpp#L218).

**Situación actual:** al activar la opción se guarda en la clave `Run` la ruta
completa del `.exe`. `IsAutostartEnabled` solo mira si el valor existe. Cada
versión se entrega en su carpeta (`ColaDeImpresion <versión>\`), así que tras
actualizar la clave sigue apuntando a la versión anterior: arranca la vieja
(o nada, si se borró esa carpeta) y el menú igual muestra la opción marcada.

**Cómo implementarla:**

1. Función pura `AutostartCommand(rutaExe)` que arma el texto del valor
   (`"<ruta>" --tray`), usada al escribir y al comparar.
2. `AutostartState ReadAutostart()` devuelve `Off`, `On` (el valor coincide con
   esta copia, sin distinguir mayúsculas) o `Stale` (existe pero apunta a otra
   ruta).
3. En `MainWindow::Create`: si el estado es `Stale`, reescribir el valor con la
   ruta actual y dejarlo anotado en `app.log`.

**Qué hay que decidir:** con `Stale`, actualizar la ruta sin preguntar (lo
propuesto: la última versión que se abre pasa a ser la de inicio) o solo
mostrar la opción desmarcada.

**Cómo saber si funcionó:**

- *Prueba unitaria* de la comparación: misma ruta con otras mayúsculas = `On`;
  otra carpeta = `Stale`; valor vacío = `Off`.
- *Prueba manual:* activar la opción, copiar la carpeta de la app a otra ruta y
  abrir la copia nueva. Con `reg query HKCU\Software\Microsoft\Windows\CurrentVersion\Run /v ColaDeImpresion`
  el valor debe apuntar a la carpeta nueva y `app.log` debe decirlo.
- Desactivar la opción sigue borrando el valor.
- Cerrar sesión y volver a entrar: arranca la copia nueva, en la bandeja.

## X5. La selección no salta sola a otro trabajo

**Dónde:** `JobListView::SetRows`,
[JobListView.cpp:166-175](../src/ui/JobListView.cpp#L166-L175).

**Situación actual:** cuando el trabajo enfocado sale de la cola (se imprimió,
lo cancelaron, dejó de coincidir con la búsqueda), la lista enfoca **y
selecciona** el que quedó en su lugar, para poder seguir sin hacer clic. El
riesgo: `F11`, `Ctrl+P` y `Ctrl+R` no piden confirmación, así que una tecla
pulsada justo cuando la selección cambió actúa sobre el trabajo de otro
estudiante. Con *confirmar antes de cancelar* desactivado, `Supr` también.

**Cómo implementarla** (dos opciones):

- **A (recomendada): mover el foco sin seleccionar.** Al desaparecer la fila
  enfocada, el foco pasa a la vecina (se ve el contorno) pero la selección
  queda vacía. El operador confirma con `Espacio`, una flecha o un clic. Los
  comandos ya avisan "no hay trabajos seleccionados". Hay que revisar que
  `FocusedJob()` (vista previa, mover, reenviar) siga exigiendo selección.
- **B: conservar el comportamiento con un margen de seguridad.** `SetRows`
  anota el momento en que cambió la selección por sí sola; `MainWindow::Execute`
  ignora pausar, reanudar, cancelar, reiniciar y `F11` durante unos 700 ms
  después, con un aviso en la barra de estado.

En las dos, extraer la decisión a una función pura (qué fila queda enfocada y
qué selección resulta, dadas las filas anteriores y las nuevas) para poder
probarla sin ventana.

**Qué hay que decidir:** A o B. A cambia una comodidad que hoy existe (seguir
con el siguiente trabajo sin tocar nada); B la conserva pero deja un margen
fijo que puede no alcanzar.

**Cómo saber si funcionó:**

- *Prueba unitaria* de la función extraída: la fila enfocada desaparece → con
  A la selección queda vacía y el foco en la vecina; con selección múltiple, se
  quitan solo las claves que ya no existen; si la fila enfocada sigue, nada
  cambia.
- *Prueba manual:* tres trabajos en pausa, seleccionar el primero, liberarlo
  con `F11` y, apenas desaparezca, pulsar `F11` otra vez. Antes del cambio se
  libera el segundo; después no (aviso en la barra de estado) y el segundo
  sigue "Retenido".
- Repetir escribiendo en la búsqueda un texto que deje fuera el trabajo
  seleccionado.

## X6. No rearmar la tabla si nada cambió

**Dónde:** `QueueMonitor::Run` publica en cada vuelta
([QueueMonitor.cpp:364](../src/core/QueueMonitor.cpp#L364)) y
`MainWindow::OnSnapshot` rearma todo
([MainWindow.cpp:696-768](../src/app/MainWindow.cpp#L696-L768)).

**Situación actual:** cada sondeo (3 s) llega una instantánea aunque la cola
sea idéntica, y la ventana vuelve a filtrar, ordenar y construir todas las
filas, la barra de impresoras, la barra de estado y el panel de detalles.

**Cómo implementarla:**

- **No** suprimir la publicación en el monitor: las instantáneas hacen de reloj
  para `PrintReleaseTracker` (plazo de 15 s de D35), para los reintentos de
  `JobFactsService` (D48) y para el reintento de carnets (D56).
- En `OnSnapshot`, ejecutar siempre los pasos de seguimiento (`tracker_`,
  `RepauseReleasedPrinters`, `facts_->Request`, `carnets_->Request`) y saltarse
  la parte visual si la instantánea es igual a la anterior.
- "Igual" = función pura `SameVisibleContent(a, b)` en `core/PrintTypes` que
  compara error, indicadores de la barra de estado, y de cada impresora y
  trabajo los campos que se muestran (estado, posición, páginas, tamaño,
  `heldByApp`, textos…). Ante la duda, devolver `false`.
- `WM_APP_FACTS`, `WM_APP_CARNETS`, cambios de filtro, orden, idioma o
  impresora siguen rearmando siempre.

**Cómo saber si funcionó:**

- *Pruebas unitarias* de `SameVisibleContent`: dos instantáneas idénticas →
  verdadero; cambiar uno a uno estado, posición, `heldByApp`, problemas de una
  impresora, error global o cantidad de trabajos → falso.
- *Medición:* en Debug, contar las llamadas a `RebuildRows` (un contador en
  `app.log` cada minuto, que se quita después). Con la cola quieta durante un
  minuto: antes unas 20, después 0.
- *Que nada quede desactualizado:* con la cola quieta, pausar un trabajo desde
  la cola de Windows → la fila cambia en el siguiente sondeo. Un `F11` sobre un
  trabajo cuya reanudación falla debe volver a pausar la impresora a los 15 s
  (prueba que el reloj sigue corriendo).
- El panel de detalles no parpadea ni pierde la página o el zoom mientras la
  cola no cambia.
