# Pixmo Games — notas de desarrollo

App homebrew (`.wuhb`) para Wii U con Aroma que muestra los juegos instalados.
Está escrita en C con SDL2 (wut + devkitPPC) y stb. Instrucciones para la
consola: [LEEME.txt](../LEEME.txt) / [README.txt](../README.txt).

## Compilar

| Qué | Comando | Resultado |
|---|---|---|
| Wii U | `./build-wiiu.ps1` (PowerShell 7, Docker) | `build-wiiu/pixmogames.wuhb`, `build-wiiu/pixmogames-sd.zip` |
| Plugin de inicio | `./build-plugin.ps1` (PowerShell 7, Docker) | `plugin/pixmo_inicio.wps` |
| PC (pruebas, Linux/WSL con SDL2) | `bash build-pc.sh` | `build-pc/ludoteca` |
| Versión para publicar | `./tools/release.ps1` | `release/PixmoGames-v<versión>.zip` y el código fuente |

- Wii U: imagen fijada `devkitpro/devkitppc` (wut + SDL2 2.32 con teclado del
  sistema), sin red. No usa librerías externas aparte de SDL2.
- Recursos del `.wuhb` (icono y pantallas de carga): `tools/make_assets.py`
  (necesita Pillow) → `assets/` y `src/pixmo_icono.h`.
- Datos en la SD: `SD:/wiiu/apps/pixmogames/`. (Si no existe pero sí la carpeta
  antigua `apps/ludoteca/` con un `config.txt`, la app usa la antigua: era el
  nombre de las primeras versiones.)

## Idiomas

El código escribe los textos de la interfaz en español. `src/i18n.c` tiene la
tabla español → inglés y `tr()` devuelve el texto en el idioma elegido
(Ajustes > Idioma: automático según la consola, español o inglés). Las
funciones de texto (`text_draw`, `text_width`, `text_fit`, `text_wrap`) pasan
todo por `tr()`, así que una etiqueta fija solo necesita su entrada en la
tabla; los textos armados con `snprintf` traducen el formato
(`snprintf(buf, n, tr("%d juegos"), n)`). `py -3 tools/check_i18n.py` revisa que
no falte ningún texto y que los `%d`/`%s` coincidan.

## Probar sin consola

- **PC con capturas:** `py -3 tools/fake_titles.py pcdata` crea juegos
  ficticios. La versión de PC necesita la fuente del sistema de Wii U en
  `pcdata/CafeStd.ttf` (cópiala de tu consola o de Cemu; no se incluye).
  `tools/pc_run.sh "60:plus 90:shot=ajustes 92:quit"` ejecuta un guion de
  botones (`a b x y l r zl zr plus minus up down left right`, `text=hola`,
  `tap=x,y`, `shot=nombre`, `quit`) y guarda PNG en `build-pc/shots/`.
  `LUDO_NONET=1` desactiva la red, `LUDO_LANG=1` simula una consola en inglés y
  `LUDO_PCDATA=<carpeta>` usa otra carpeta de datos. El renderizador por
  software de PC es lento (5-10 FPS): no sirve para medir rendimiento.
- **Cemu:** el `.wuhb` corre en Cemu 2.x (ruta real de consola: MCP, ACP, API
  FS, GX2, red). Con `SD:/wiiu/apps/pixmogames/autotest.txt` la app sigue el
  mismo guion de botones. Cemu no carga plugins de Aroma: el plugin se prueba
  en la consola.

## Arte y fichas propias (opcional)

En `SD:/wiiu/apps/pixmogames/`:

- `covers/<ID de GameTDB>.jpg|png` o `covers/<TITLE ID>.jpg|png`: carátula
  (440x620 o menos va bien en la consola).
- `fondos/<TITLE ID>.jpg`: fondo 1280x720 de la vista Cine.
- `titulos/<TITLE ID>.png`: logo del título con transparencia.
- `fichas.txt`: una línea por juego, campos separados por tabuladores:
  `TITLE ID (16 cifras hex)`, `año`, `jugadores`, `valoración x10 (0-50)`,
  `géneros separados por coma`, `editor`, `desarrollador`, `sinopsis` (`\n`
  para saltos de línea) y, opcional, `plataforma` (por ejemplo
  `Nintendo 64`, para los juegos de Consola Virtual o inyectados).
  Las líneas con menos de 8 campos se ignoran.
- `miniaturas.bin`: la app lo arma sola (carátulas a 120 px de alto en RGB565,
  little-endian: `"PXM1"`, cantidad, 0, y por juego `{TID u64, ancho u16,
  alto u16, tamaño del archivo de la carátula u32, desplazamiento u32}`).

Los juegos de Wii y GameCube instalados para Wii U (tipo de app `0x8000002E`)
quedan marcados en `cache.txt` con líneas `p <TID> wii`. El homebrew de Aroma
(`.wuhb` en `SD:/wiiu/apps/` o una carpeta más adentro) se lista con un Title ID
inventado (`0005000F` + FNV-1a de la ruta relativa a la SD); `src/wuhb.c` lee
nombre, icono y pantallas de inicio del paquete, y se abre con
`RL_LaunchHomebrew` del módulo `homebrew_rpx_loader` de Aroma. En `cache.txt`
van como `p <TID> hb <ruta>`.

## Plugin de inicio (`plugin/`)

`./build-plugin.ps1` compila `plugin/pixmo_inicio.wps` (WUPS + librpxloader, con
las imágenes de `ghcr.io/wiiu-env` del `plugin/Dockerfile`). Va en
`SD:/wiiu/environments/aroma/plugins/`. Aroma carga los plugins una vez al
encender, así que su memoria sabe si el menú Wii U que arranca es el primero:

- primer menú Wii U desde que cargó Aroma y `set al_encender 1` (o sin
  config) → abre Pixmo Games;
- `volver.txt` en la carpeta de datos (lo deja Pixmo Games justo antes de
  iniciar un juego si `set volver 1`; dice `wii` para los juegos de Wii, que
  terminan con un reinicio) → lo borra y abre Pixmo Games. Un `volver.txt` de
  un juego de Wii U en el primer menú quedó de apagar la consola dentro del
  juego: entonces manda la regla de encendido.

Abre Pixmo Games como lo abre el menú al tocar su icono (homebrew_on_menu):
`RPXLoader_PrepareLaunchFromSD("wiiu/apps/pixmogames/pixmogames.wuhb")` y luego
`_SYSLaunchTitleByPathFromLauncher` con la ruta de la app Salud y seguridad. Lo
hace desde un reemplazo de `VPADRead` solo en el proceso del menú Wii U, 0,7 s
después de la primera lectura (el menú ya tiene el primer plano); con B
mantenido no hace nada. Cada decisión queda en `inicio_log.txt`.

## Carrusel sin saltos

El carrusel de la vista Cine dibuja miniaturas (`miniaturas.bin`, todas en
memoria desde el arranque) en vez de las carátulas grandes: el ancho de cada
caja sale de la miniatura y se redondea a píxeles enteros, igual que las
posiciones, así nada se mueve cuando llega o se descarta una carátula grande.
La caja 3D usa la miniatura mientras carga la carátula grande.

## Marca y música

- Marca de Pixmo Games en `assets/fuente/`: `tools/make_assets.py` hace el
  icono, las pantallas de carga con el fondo neón y `src/pixmo_icono.h`;
  `src/brand.c` dibuja la mascota y el nombre con la letra de píxeles 5x7.
- Música: `tools/musica_consola.py <canciones> musica` convierte con ffmpeg a
  WAV IMA ADPCM 32 kHz estéreo (~32 KB/s) para `SD:/.../pixmogames/musica/`.
  `src/adpcm.c` las decodifica (igual bit a bit que ffmpeg:
  `tools/test_adpcm.c`); `audio.c` las baraja, carga la siguiente en un hilo
  del núcleo 2 y las reescala a 48 kHz. Sin canciones suena una música
  ambiental sintetizada.

## Estructura (`src/`)

| Archivo | Contenido |
|---|---|
| `main.c` | bucle, mandos (GamePad/Pro/Wiimote vía SDL), táctil, teclado del sistema, modo guion |
| `platform_wiiu.c` | MCP (lista de títulos), ACP (meta.xml), API FS propia para iconos/arranque/partidas, fuente del sistema, idioma, red, lanzamiento (Wii U con `ACPAssignTitlePatch`, por ruta para Wii, `RL_LaunchHomebrew` para `.wuhb`) |
| `wuhb.c` | paquetes `.wuhb` de la SD: búsqueda, lectura de archivos internos (gzip) y `meta.ini` |
| `platform_pc.c` | equivalentes de prueba sobre `pcdata/` |
| `library.c` | juegos, caché y fichas de la SD, estadísticas, categorías, miniaturas (`miniaturas.bin`), hilo de carga (carátulas, fondos, logos de títulos con su halo) y hilo de descarga |
| `i18n.c` | idioma de la interfaz y tabla español → inglés |
| `ui_home.c` | pestañas, selección, búsqueda, lanzamiento, entrada, vistas Vitrina/Cuadrícula |
| `ui_cine.c` | vista Cine: fondo con zoom lento, caja 3D proyectada (malla para la textura, lomo con texto), ficha, carrusel |
| `ui_panels.c` | ajustes, menú del juego, categorías (icono/color/logo), confirmaciones, ayuda |
| `ui_draw.c` | tema claro/oscuro, botones redondos, insignias, carátulas generadas, barra superior |
| `gfx.c` | primitivas suavizadas por geometría (rectángulos redondeados, sombras, brillos) |
| `font.c` | texto con la fuente del sistema (stb_truetype + atlas; transformación por vértice para el lomo) |
| `icons.c` | iconos vectoriales por campos de distancia |
| `audio.c` | efectos sintetizados; canciones de la SD (o música ambiental sintetizada si no hay) |
| `adpcm.c` | lectura de WAV IMA ADPCM |
| `brand.c` | icono y nombre de Pixmo Games (letra de píxeles) |
| `net.c` | HTTP/1.0 mínimo para GameTDB |

## Notas técnicas

- Acceso a archivos de otros títulos: `platform_wiiu.c` crea su propio
  `FSClient` y, si MochaPayload (parte de Aroma) responde a su consulta de
  versión, le envía el ioctl 0x28 que da acceso completo a ese cliente (lo
  mismo que hace `Mocha_UnlockFSClient` de libmocha). Sin eso, los títulos se
  listan y lanzan igual (nombres por ACP), solo faltan iconos/arte de consola.
- Todo búfer que el sistema llena por IPC (lecturas FS, stat, MCP, ACP) va
  alineado a 0x40 y redondeado a líneas de caché completas (`ipc_alloc`): en
  la consola real, un tamaño que no es múltiplo de 64 corrompe el heap.
- `stb_image` se compila con `STBI_NO_THREAD_LOCALS`: el formato RPX no
  admite relocaciones TLS.
- `gethostbyname` de wut fallaba bajo Cemu; se usa `getaddrinfo`.
- Los hilos de carga corren en el núcleo 2 con pausas cortas (Cafe OS no
  reparte tiempo entre hilos de igual prioridad). En la consola, cargar una
  carátula de 440x620 toma ~300 ms: la interfaz nunca la espera.
- Juegos de Wii U: como Launchiine, `MCP_GetTitleInfo` + `ACPAssignTitlePatch`
  (asigna la actualización del juego) + `_SYSLaunchTitleWithStdArgsInNoSplash`.
  Con `SYSLaunchTitle` solo, la consola usaba la actualización asignada al último
  título iniciado y muchos juegos se quedaban en la pantalla blanca de Wii U.
  Antes de lanzar se detienen los hilos de carga y descarga (`lib_stop_for_launch`).
- Juegos de Wii y GameCube: se lanzan con `_SYSLaunchTitleByPathFromLauncher`
  después de preparar `nn_cmpt` (pantalla y mando), como el menú Wii U;
  `SYSLaunchTitle` solo no sirve para ellos. La forma de jugar elegida se
  recuerda aparte para Wii (`set wii_mode`) y GameCube (`set gc_mode`).
- Vista Cine: el fondo se dibuja como un solo cuadro del tamaño de la pantalla
  con coordenadas de textura recortadas (no una malla que se salga de pantalla);
  la carátula de la caja va en una malla de 6x8 para que la textura no se doble
  (SDL interpola las texturas de forma afín).
- Al salir, el hilo de descarga se aborta (`net_abort`) para que la consola
  no quede esperando hilos vivos.
