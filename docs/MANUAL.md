# Manual de uso — Cola de Impresión (LAIMI)

## 1. Primer inicio

1. Ejecutar `ColaDeImpresion.exe`. Aparece la lista de trabajos de **este equipo**.
2. Para controlar el servidor del laboratorio: `Ctrl+O` (o menú `⋯` → *Conectar a servidor…*), escribir el nombre, p. ej. `\\SERVIDOR-LAIMI`, y `Enter`. El servidor queda en la lista de **servidores recientes** del menú.
3. El botón de la izquierda (o `Ctrl+E`) elige la impresora; **Todas las impresoras** muestra la cola completa del servidor con el nombre de la impresora en cada trabajo.
4. Bajo la cabecera está la **barra de acceso rápido**: los botones de las impresoras que se hayan fijado, con la cantidad de trabajos de cada una (ver §3.1).
5. A la derecha de la búsqueda hay dos botones: **Pausar / Reanudar trabajos** (los seleccionados, igual que `Ctrl+P`) y **Pausar / Reanudar impresora** (igual que `F9`; en la vista "Todas" actúa sobre todas). Su texto dice lo que harán: si todo lo elegido está en pausa dicen *Reanudar* (con ▶ resaltado). Sin trabajos seleccionados el primero queda gris. En ventanas angostas muestran solo íconos (documento o impresora + ⏸/▶); al pasar el mouse se ve la descripción con el atajo.

La barra inferior empieza con el **total de la selección**: al elegir uno o
varios trabajos muestra cuántos son y la suma de las páginas que se van a
imprimir (la columna *Páginas*, es decir páginas × copias), por ejemplo
*Selección: 3 trabajo(s) · 11 páginas a imprimir*. Si alguno tiene "—" (no se
pudo determinar) se indica aparte: *(1 sin determinar)*. En el historial suma las
páginas de los registros elegidos. Después resume la cola (trabajos, imprimiendo,
en pausa, con error), el servidor, problemas de impresoras y si la actualización
es **en vivo** o por **sondeo**.

## 2. Trabajos

La cola es una **tabla** como la de Windows. Columnas: `#` (posición en la
cola), **Documento**, **Estado**, **Usuario**, **Equipo** (la PC desde la que
se envió), **Impresora** (en la vista "Todas"), **Páginas**, **Papel**,
**Orientación**, **Doble cara**, **Origen del papel**, **Tamaño** y **Enviado**
(fecha y hora de envío). Desde Estado en adelante los valores van centrados
bajo su título.

- **Ordenar:** un clic en el encabezado de una columna ordena por ella; otro
  clic invierte el orden (la flecha del encabezado lo indica). Un clic en `#`
  vuelve al orden real de impresión. También `F6` / `Shift+F6`. El orden es
  solo una vista: no cambia el orden real del spooler.
- **Páginas** es lo que se va a imprimir en total: **páginas × copias**
  (3 páginas con 3 copias = 9). Para los trabajos enviados desde otras PCs las
  copias se leen de los datos del trabajo (en las Epson del LAIMI, del
  encabezado EJL), porque Windows no las informa. `—` = no se sabe. Si un
  valor es estimado (por ejemplo, todavía no se leyeron los datos), la pestaña
  Configuración lo indica con la insignia *Estimado* y el motivo.
- Papel, orientación, doble cara y origen siguen el mismo criterio que la
  pestaña Configuración (sección 4.1). El papel se muestra por nombre
  (Carta, A3, 12 × 18) y el origen con el nombre de la bandeja: en las Epson
  del LAIMI, *Auto selección*, *Alimentador de papel*, *Bandeja 1* o
  *Bandeja 2* (nunca un número de código).
- **Ancho de las columnas**, como en la cola de Windows: arrastrar el borde
  derecho del título de una columna (el cursor cambia a ↔). **Doble clic** en
  el borde ajusta la columna a su contenido. Si las columnas no caben aparece
  una barra de desplazamiento horizontal (también `Shift` + rueda). Los anchos
  se guardan; clic derecho en los títulos (o menú `⋯`) → *Restablecer el ancho
  de las columnas* vuelve a los predeterminados. Documento ocupa el espacio
  libre hasta que se le da un ancho propio.
- **Orden de las columnas**, como en la cola de Windows: arrastrar el título
  de una columna hacia la izquierda o la derecha; una línea azul marca dónde
  va a quedar y se suelta para ubicarla ahí (cerca de los bordes la tabla se
  desplaza sola). Un clic sin arrastrar sigue ordenando por esa columna. El
  orden se guarda por operador (en su `config.json`) y se conserva al volver a
  abrir la app; la cola y el historial tienen cada uno su orden. Clic derecho
  en los títulos (o menú `⋯`) → *Restablecer el orden de las columnas* vuelve
  al orden original.
- **Nunca se oculta ninguna columna**: si la ventana es angosta o se abre el
  panel de detalles y las columnas no caben, aparece la barra horizontal
  (también `Shift` + rueda) para llegar a las de la derecha.

| Acción | Teclado | Mouse |
|---|---|---|
| Seleccionar | `↑` `↓` `Inicio` `Fin` | clic |
| Selección múltiple | `Shift+↑/↓`, `Ctrl+A` | `Ctrl`+clic, `Shift`+clic, o **arrastrar** con el botón izquierdo (rectángulo, como en el Explorador) |
| Quitar la selección | `Esc` o `Ctrl+Shift+A` | clic en una zona vacía de la lista |
| Pausar / reanudar | `Ctrl+P` (alterna) o `Ctrl+R` | clic derecho |
| Pausar / reanudar **el trabajo y su impresora** | `Ctrl+Alt+P` o **`F8`** | clic derecho |
| **Imprimir los seleccionados y volver a pausar** la impresora | `Ctrl+Enter` o **`F11`** | clic derecho |
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

**`Ctrl+Enter` o `F11`** imprime solo lo elegido sin dejar la impresora
abierta: reanuda los trabajos seleccionados (uno o varios) y, si su impresora
está en pausa, también la reanuda. Cuando **todos** esos trabajos ya se
enviaron a la impresora (salen de la cola o quedan como "enviado/impreso"),
la app **vuelve a pausar la impresora sola**. Mientras tanto la barra inferior
muestra "Imprimiendo N liberado(s): la impresora se pausará al terminar".
Detalles:

- Con trabajos de varias impresoras, cada impresora se vuelve a pausar en
  cuanto terminan *sus* trabajos.
- Si alguien vuelve a pausar o cancela uno de esos trabajos, deja de
  esperarse; si un trabajo no se pudo reanudar (p. ej. por permisos), se deja
  de esperar a los 15 s. Así la impresora nunca queda abierta indefinidamente.
- Mientras la impresora está reanudada también podría imprimirse otro trabajo
  de esa cola que **no** esté en pausa; con la retención automática y la pausa
  de inicio activadas (lo predeterminado) todos los demás están retenidos.
- La app tiene que seguir abierta (puede estar en la bandeja) hasta que
  terminen de enviarse.

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

- Panel derecho de **detalles** (vista previa y configuración): **arranca oculto** para que la tabla use todo el ancho. Se abre y se cierra con el botón de panel de la cabecera (el primero de los íconos de la derecha), con `Ctrl+B`, o con `F7` (abre directamente la pestaña Configuración). Su ancho se cambia arrastrando el separador.
- `Espacio` o `Enter`: **vista rápida** a pantalla completa (`Esc` para volver).
- `AvPág`/`→` y `RePág`/`←`: cambiar de página. Rueda del mouse: páginas.
- `Ctrl+rueda`, `Ctrl++`, `Ctrl+-`: zoom; `Ctrl+0`: ajustar. Con zoom, arrastrar para mover; doble clic alterna 100 %/200 %.

### 4.1 Pestaña «Configuración» (`F7`)

Arriba del panel hay dos pestañas: **Vista previa** y **Configuración**. `F7`
(o un clic) alterna entre ellas; en la ficha, `AvPág`/`RePág` y la rueda la
desplazan. El encabezado del panel muestra además un resumen de una línea:
papel · orientación · doble cara · copias · páginas a imprimir.

La ficha muestra, para el trabajo seleccionado:

- **Configuración solicitada**: tamaño de papel (nombre y medidas en mm),
  orientación, doble cara (una cara / borde largo / borde corto), copias,
  intercalado, color, origen del papel, calidad, escala, tipo de medio y
  páginas por cara.
- **Páginas, caras y hojas**: páginas en los datos, páginas lógicas (una copia),
  copias hechas por la aplicación (si se detectan), caras y hojas físicas
  previstas, y el progreso del spooler.
- **Datos y vista previa**: formato real de los datos, cómo se leyeron y si hay
  vista previa (o por qué no).
- **Trabajo**: los datos de `JOB_INFO_2` (id, usuario, equipo, estado, fecha,
  tamaño, tipo de datos, controlador, procesador).

Cada valor dice **de dónde sale** y lleva una insignia:

| Insignia | Significado |
|---|---|
| **Fiable** | Confirmado por la fuente que realmente aplica el spooler o por los propios datos del trabajo. |
| **Estimado** | Supuesto o sin confirmar; debajo se explica el motivo (p. ej. "trabajo RAW: el controlador ya aplicó las opciones en el equipo del usuario"). |
| **No determinable** | Falta información; se explica cuál. |

Las caras y hojas **nunca** se calculan multiplicando a ciegas páginas × copias:
algunas aplicaciones repiten las páginas para hacer las copias y otras dejan que
lo haga el controlador. Detalle técnico y pruebas en
[DATOS_DE_IMPRESION.md](DATOS_DE_IMPRESION.md).

### 4.2 De dónde sale la vista previa

Los datos del trabajo se leen con la **API del spooler** (como lo hace el propio
Windows): basta con tener **"Administrar documentos"** en la cola (o ser el
dueño del trabajo). No hace falta ser administrador del servidor ni acceder a
`\\SERVIDOR\C$`. Si la API falla, se intenta el archivo de spool como en
versiones anteriores (eso sí exige ser administrador del servidor).

| Datos del trabajo | Vista previa |
|---|---|
| EMF (drivers GDI, impresión en el servidor) | Sí, sin nada extra |
| PostScript / PDF | Sí, con Ghostscript (incluido en la carpeta `renderers`) |
| PCL 5 / PCL XL (PCL 6) | Sí, con GhostPCL |
| XPS (drivers v4) | Sí, con GhostXPS (o miniatura de la primera página) |
| **ESC/Page de Epson (colas LAIMI 1/2 - INK)** | **No disponible**: es el lenguaje propio de la impresora, generado en la PC del estudiante, y no hay intérprete. La pestaña Configuración sí funciona. |

Si no se pueden leer los datos, el panel muestra **qué se intentó y por qué
falló** cada vía; lo mismo queda en `app.log`. Un trabajo que todavía se está
recibiendo se analiza al terminar de llegar.

Las copias temporales que necesitan Ghostscript y la miniatura de Windows se
crean en una carpeta privada (solo accesible para el usuario que ejecuta la app)
y se borran enseguida; al salir se borra la carpeta.

Las vistas previas se guardan en caché mientras la app está abierta.

## 5. Buscar y filtrar

`Ctrl+F` y escribir: filtra por documento, usuario, equipo, impresora o estado.
Varias palabras = todas deben coincidir (`jperez pdf`). `Esc` limpia el filtro.
En la barra de estado se ve "Filtro: … (n de m)".

## 6. Historial y estadísticas

`Ctrl+H` (o el botón del reloj) muestra los trabajos que salieron de la cola con
su resultado: **Impreso**, **Cancelado**, **Falló** o **Retirado** (se fue sin
que se viera imprimir). Cada trabajo conserva las mismas columnas que tenía en la
cola (`#`, páginas × copias, papel, orientación, doble cara, origen del papel,
tamaño, enviado) y además la hora en que terminó (*Finalizado*). Los registros
guardados con versiones anteriores a la 0.5.0 muestran "—" en las columnas que
no tenían. Las páginas son las mismas que se veían en la cola **también cuando el
trabajo se liberó y se imprimió** (antes de la 0.5.1 un trabajo de 2 páginas × 2
copias liberado pasaba al historial como 2). La barra de estado muestra páginas
impresas hoy y en los últimos 7 días (contadas como la columna *Páginas*).
Menú → *Borrar historial* lo vacía.

### Exportar a Excel

En el historial aparece el botón **Exportar a Excel** junto a los de pausa
(también `Ctrl+Shift+E` o menú `⋯` → *Exportar historial a Excel…*). Pide dónde
guardar el archivo (`Historial de impresión AAAA-MM-DD.xlsx`) y ofrece abrirlo.
El libro tiene dos hojas:

- **Historial**: todos los registros con las columnas de la tabla, en el orden en
  que el operador las tenga, más *Servidor*. Páginas, tamaño (KB) y las fechas son
  números y fechas de Excel (se pueden sumar, filtrar y ordenar); la primera fila
  queda fija y con filtros.
- **Resumen**: datos generales (registros, período, trabajos y páginas impresas,
  promedio por trabajo y por día, cancelados, con error, retirados, usuarios,
  equipos e impresoras distintos, día y hora con más páginas, usuario con más
  páginas, trabajo más grande, tamaño total) y tablas de **páginas impresas por
  tipo de papel, impresora, doble cara, orientación, origen del papel, usuario,
  equipo, día y hora**, con su porcentaje, además de los trabajos por resultado.
  Las tablas por categoría cuentan solo los trabajos impresos.

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
