# Manual de uso — Cola de Impresión (LAIMI)

## 1. Primer inicio

1. Ejecutar `ColaDeImpresion.exe`. Aparece la lista de trabajos de **este equipo**.
2. Para controlar el servidor del laboratorio: `Ctrl+O` (o menú `⋯` → *Conectar a servidor…*), escribir el nombre, p. ej. `\\SERVIDOR-LAIMI`, y `Enter`. El servidor queda en la lista de **servidores recientes** del menú.
3. El botón de la izquierda (o `Ctrl+E`) elige la impresora; **Todas las impresoras** muestra la cola completa del servidor con el nombre de la impresora en cada trabajo.
4. Bajo la cabecera está la **barra de acceso rápido**: los botones de las impresoras que se hayan fijado, con la cantidad de trabajos de cada una (ver §3.1).

La barra inferior resume la cola (trabajos, imprimiendo, en pausa, con error),
el servidor, problemas de impresoras y si la actualización es **en vivo** o por
**sondeo**.

## 2. Trabajos

Cada fila muestra: estado (ícono y etiqueta de color), documento, usuario,
equipo, hora, impresora, páginas y tamaño.

| Acción | Teclado | Mouse |
|---|---|---|
| Seleccionar | `↑` `↓` `Inicio` `Fin` | clic |
| Selección múltiple | `Shift+↑/↓`, `Ctrl+A` | `Ctrl`+clic, `Shift`+clic, o **arrastrar** con el botón izquierdo (rectángulo, como en el Explorador) |
| Quitar la selección | `Esc` o `Ctrl+Shift+A` | clic en una zona vacía de la lista |
| Pausar / reanudar | `Ctrl+P` (alterna) o `Ctrl+R` | clic derecho |
| Pausar / reanudar **el trabajo y su impresora** | `Ctrl+Alt+P` o **`F8`** | clic derecho |
| Reiniciar | `Ctrl+I` | clic derecho |
| Cancelar | `Supr` (pide confirmación) | clic derecho |
| Cancelar todos los de un usuario | — | clic derecho → *Cancelar todos los trabajos de…* |
| Subir / bajar en la cola | `Ctrl+↑` / `Ctrl+↓` | clic derecho |
| Reenviar a otra impresora | `Ctrl+T` | clic derecho |

**Reenviar**: se pausa el original, se envía a la impresora elegida y, si todo
sale bien, se elimina el original. Si ambas impresoras usan el mismo driver se
copian los datos exactos; si no, se convierten (EMF directamente; PCL/PS/PDF con
Ghostscript). Si no hay forma de convertir, la app pregunta si enviar los datos
tal cual (solo funciona si ambas impresoras entienden el mismo lenguaje).

## 2.1 Retención automática (trabajos nuevos en pausa)

Está **activada por defecto**. Cada trabajo que llega a la cola se pausa en el
mismo instante en que el spooler avisa y queda con la etiqueta violeta
**Retenido** hasta que un operador lo libere con **Reanudar** (`Ctrl+R`,
clic derecho, o `Ctrl+A` + `Ctrl+R` para liberar todos). Un trabajo liberado no
se vuelve a retener. Llega una notificación por cada trabajo retenido, salvo que
las notificaciones estén en *ninguna*.

- Activar/desactivar: menú `⋯` → *Retener (pausar) todos los trabajos nuevos* o `Ctrl+Shift+H`. La barra inferior muestra "Retención activa".
- Los trabajos que ya estaban en la cola al abrir la app no se tocan; solo los que llegan después.
- **La app tiene que estar abierta** (puede estar en la bandeja) para retener: conviene activar *Iniciar con Windows* en el equipo del operador.
- Para retener trabajos de otros usuarios hace falta el permiso "Administrar documentos"; si falla, la barra muestra "No se pudieron retener N trabajos".
- Es muy rápido (se salta la espera de notificaciones agrupadas), pero un trabajo diminuto podría empezar a imprimirse antes: en ese caso igual se pausa. Para que eso no pase nunca, configure las impresoras del servidor con *Iniciar la impresión después de que la última página se haya enviado a la cola*.

## 3. Impresoras

**`Ctrl+Shift+P` o `F9`** alterna la pausa de la impresora: si hay alguna en
pausa las reanuda, y si ninguna lo está las pausa. Actúa sobre la impresora
elegida o, en la vista **Todas las impresoras**, sobre todas las del servidor.
En el menú `⋯` están también *Reanudar impresora* (`Ctrl+Shift+R`) y *Vaciar
cola de la impresora…*.

**`Ctrl+Alt+P` o `F8`** hace las dos cosas a la vez: pausa —o reanuda— **los
trabajos seleccionados y la impresora de esos trabajos**. Si todo está en pausa
los libera a ambos, que es lo habitual después de la pausa de inicio. Sin
ningún trabajo seleccionado actúa solo sobre la impresora.

**Al iniciar**, la app pausa las impresoras y retiene los trabajos que ya
estaban en cola, de modo que nada se imprime hasta que un operador lo permita.
Se desactiva en el menú (*Al iniciar: pausar impresoras y retener trabajos en
cola*). Ojo: con la impresora en pausa, liberar un trabajo con `Ctrl+R` no
alcanza; también hay que reanudar la impresora (`F9`), o usar `F8`, que libera
el trabajo y la impresora de una vez.
Pausar una impresora requiere el permiso "Administrar esta impresora"; si no se
tiene, la barra de estado lo avisa y la retención de trabajos sigue funcionando.

Los problemas (sin papel, atasco, puerta abierta, sin tóner, fuera de línea…)
aparecen en el botón de impresora, en la barra de estado y como notificación.

## 3.1 Acceso rápido a impresoras

La barra bajo la cabecera muestra **Todas las impresoras** y las impresoras que
el operador haya fijado, cada una con su cantidad de trabajos y un punto de
color si tiene algún problema (pausada, sin papel, sin conexión…).

| Acción | Cómo |
|---|---|
| Elegir qué impresoras aparecen | Botón `+` al final de la barra (o Menú → *Acceso rápido a impresoras*) y marcar/desmarcar |
| Filtrar la cola por una impresora | Clic en su botón |
| Lo mismo con el teclado | `Ctrl+1` … `Ctrl+9` (en el orden de la barra) |
| Fijar/quitar la impresora actual | `Ctrl+Shift+F` |
| Pausar, reanudar o vaciar esa impresora | Clic derecho sobre su botón |
| Ocultar la barra | Menú → *Acceso rápido a impresoras* → *Barra de acceso rápido* |

Las impresoras fijadas se guardan en `config.json` (`favoritePrinters`) y son
por servidor: si una no existe en el servidor conectado, simplemente no aparece.

## 3.2 Orden de la cola

De forma predeterminada la lista sigue el **orden de la cola** (impresora y
posición de impresión). Se puede ordenar por otros criterios:

| Criterio | Para qué sirve |
|---|---|
| Orden de la cola | El orden real en que se va a imprimir (predeterminado) |
| **Equipo** | Agrupa los trabajos por la PC desde la que se imprimió (LAB-PC02, LAB-PC07…) |
| **Hora de envío** | Del más viejo al más nuevo: útil para atender por llegada |
| Usuario, Documento, Estado, Tamaño | Buscar trabajos de una persona, repetidos, retenidos o muy pesados |

- `F6` cambia al criterio siguiente, `Shift+F6` invierte el orden.
- Menú → *Ordenar la cola por* permite elegir directamente y marcar *Orden inverso*.
- Cuando el orden no es el de la cola, la barra inferior lo indica (*Orden: Equipo*).
- Dentro de un mismo valor (p. ej. dos trabajos del mismo equipo) se conserva el orden de impresión.

El criterio se guarda entre sesiones (`sortBy` / `sortDescending` en `config.json`).

## 4. Vista previa

- Panel derecho: datos del trabajo y la página. `Ctrl+B` lo muestra/oculta; se puede cambiar su ancho arrastrando el separador.
- `Espacio` o `Enter`: **vista rápida** a pantalla completa (`Esc` para volver).
- `AvPág`/`→` y `RePág`/`←`: cambiar de página. Rueda del mouse: páginas.
- `Ctrl+rueda`, `Ctrl++`, `Ctrl+-`: zoom; `Ctrl+0`: ajustar. Con zoom, arrastrar para mover; doble clic alterna 100 %/200 %.

De dónde sale la vista previa:

| Datos del trabajo | Qué se necesita |
|---|---|
| EMF (drivers GDI, impresión en el servidor) | Nada extra |
| PostScript / PDF | Ghostscript |
| PCL 5 / PCL XL (PCL 6) | GhostPCL |
| XPS (drivers v4) | GhostXPS (o, sin él, miniatura de la primera página si Windows tiene visor XPS) |

Siempre hace falta poder **leer la carpeta de spool del servidor**. Esto no
depende de la PC del estudiante ni de la aplicación que imprimió: el archivo lo
guarda el servidor mientras el trabajo está en la cola, y la app solo lo lee.

- Si la app corre **en el propio servidor**, se usa la ruta local
  (`C:\Windows\System32\spool\PRINTERS`) y basta con ser administrador de ese
  equipo (Menú → *Reiniciar como administrador*).
- Desde **otra PC** se usa `\\SERVIDOR\C$\…` (o `ADMIN$`), que exige ser
  administrador del servidor; la alternativa es compartir la carpeta y ponerla
  en `spoolDirOverrides`.
- Si una cola tiene su **propia carpeta de spool**, también se busca ahí.
- Cuando no se encuentra el archivo, el panel muestra **en qué carpetas se buscó
  y el motivo de cada fallo**; lo mismo queda en `app.log`.

Las vistas previas se guardan en caché mientras la app está abierta.

## 5. Buscar y filtrar

`Ctrl+F` y escribir: filtra por documento, usuario, equipo, impresora o estado.
Varias palabras = todas deben coincidir (`jperez pdf`). `Esc` limpia el filtro.
En la barra de estado se ve "Filtro: … (n de m)".

## 6. Historial y estadísticas

`Ctrl+H` (o el botón del reloj) muestra los trabajos que salieron de la cola con
su resultado: **Impreso**, **Cancelado**, **Falló** o **Retirado** (se fue sin
que se viera imprimir). La barra de estado muestra páginas impresas hoy y en los
últimos 7 días. Menú → *Borrar historial* lo vacía.

## 7. Apariencia e idioma

- `Ctrl+D` o el botón sol/luna: alterna claro/oscuro. En el menú: *Tema: seguir al sistema / claro / oscuro*.
- Menú → *Language: English / Idioma: Español*.

## 8. Atajos de teclado

`F1` (o `?`) abre la lista con **todos** los comandos, agrupados: primero los de
trabajos (pausar, reanudar, pausar trabajo e impresora, cancelar, reordenar,
reenviar), después los de impresora y retención, luego selección, vista previa,
búsqueda y servidor. La barra de la derecha indica que hay más abajo: se puede
arrastrar, usar la rueda del mouse o `AvPág` / `RePág`.

Para cambiar un atajo: elegirlo con `↑`/`↓` o con un **clic**; luego `Enter`,
**otro clic**, **doble clic** o clic sobre la tecla de la derecha, y presionar la
nueva combinación. Si el comando tenía dos teclas (por ejemplo `Ctrl+Shift+P` y
`F9`), la nueva combinación reemplaza a ambas. `Supr` lo deja sin asignar, `R` restaura todos
los predeterminados, `Esc` cierra. Si la combinación ya era de otro comando, se
le quita a ese y se avisa.

## 9. Bandeja del sistema y notificaciones

- Cerrar la ventana la **oculta en la bandeja** (desactivable en el menú: *Cerrar a la bandeja del sistema*). Clic en el ícono para volver; clic derecho → *Salir*.
- *Iniciar con Windows* arranca la app minimizada al iniciar sesión.
- *Notificaciones*: ninguna / solo errores (predeterminado) / todas (incluye trabajos nuevos e impresos).

## 10. Configuración avanzada (`config.json`)

Menú → *Abrir carpeta de configuración*. Con la app cerrada se puede editar:

```json
{
  "spoolDirOverrides": { "\\\\SERVIDOR-LAIMI": "\\\\SERVIDOR-LAIMI\\spool$" },
  "renderers": { "ghostscript": "C:\\Program Files\\gs\\gs10.05.1\\bin\\gswin64c.exe",
                 "ghostpcl": "", "ghostxps": "" },
  "pollIntervalMs": 3000,
  "previewDpi": 110,
  "previewMaxPages": 30,
  "historyLimit": 2000,
  "confirmCancel": true
}
```

- `spoolDirOverrides`: carpeta desde donde leer el spool de cada servidor (clave `""` = este equipo). Útil si se comparte la carpeta `PRINTERS` del servidor con permisos de lectura para los operadores.
- `pollIntervalMs`: intervalo del sondeo de respaldo (500–60000 ms).
- Línea de comandos: `--server NOMBRE` (conectar al iniciar), `--tray` (iniciar oculta).

Registro de diagnóstico: `app.log` en la misma carpeta.

## 11. Problemas frecuentes

| Síntoma | Causa / solución |
|---|---|
| "Sin permisos de administración…" en la barra | El usuario no tiene "Administrar documentos" en esas impresoras. Pedir el permiso o *Reiniciar como administrador*. |
| "No se pudo leer el archivo de spool: acceso denegado" | Hace falta permiso sobre la carpeta de spool: ser administrador del servidor (o administrador local si la app corre **en** el servidor) o configurar `spoolDirOverrides`. El panel lista las carpetas que se probaron. |
| "No se encontró el archivo de spool del trabajo N" | El panel indica en qué carpetas se buscó y por qué falló cada una. Causas: la cola usa otra carpeta de spool, el servidor ya borró el archivo, o el trabajo se imprime directo sin spool. |
| Un trabajo aparece en la cola de Windows y no en la app | Revisar la barra inferior: *"N trabajos que el spooler cuenta no se pudieron leer"* (problema de permisos o de enumeración, queda en `app.log`) o *"N en otras impresoras"* (está en una impresora distinta a la elegida: cambiar a *Todas las impresoras*). También puede estar oculto por el filtro de búsqueda. |
| "Los datos están en PCL / PostScript…" | Instalar Ghostscript/GhostPCL (ver README). |
| "Actualización por sondeo" | El servidor no envía notificaciones (firewall/RPC). Funciona igual, con unos segundos de retraso. |
| "No se pudo conectar con el servidor" | Nombre incorrecto, servidor apagado o firewall bloqueando "Compartir archivos e impresoras". Se reintenta automáticamente. |
