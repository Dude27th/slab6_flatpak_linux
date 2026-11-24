# SLAB6 para Linux: paquete Flatpak

Port a Linux (SDL2) del editor de vóxeles SLAB6 de Ken Silverman. Esta carpeta tiene todo lo que hace falta para
compilarlo como Flatpak; no se descarga nada salvo el runtime.

## Compilar e instalar

```sh
flatpak install flathub org.freedesktop.Platform//26.08 org.freedesktop.Sdk//26.08
flatpak-builder --user --install --force-clean build-dir org.example.Slab6.yml
flatpak run org.example.Slab6                          # o: flatpak run org.example.Slab6 /1024x768 modelo.kv6
```

Necesitas `flatpak` y `flatpak-builder` (en Debian/Ubuntu: `sudo apt install flatpak flatpak-builder`).
El runtime 25.08 también sirve (cambia `runtime-version` en el manifiesto y en el comando de `flatpak install`).

## Archivos

| Archivo | Para qué sirve |
|---|---|
| `org.example.Slab6.yml` | manifiesto de Flatpak |
| `slab6.c`, `sdlmain.cpp`, `sysmain.h`, `s6win.h`, `s6long.h`, `s6port.h`, `s6menu.h` | el editor (SLAB6 original con los cambios del port) y su capa SDL2 |
| `s6filechooser.c` | selector de archivos del escritorio mediante el portal de Flatpak |
| `s6clipimage.c` | lee la imagen del portapapeles (Shift+Insert) |
| `slab6.sh` | lanzador (sin argumentos empieza en Documentos) |
| `org.example.Slab6.desktop`, `.metainfo.xml`, `.png` | entrada del menú de aplicaciones, metadatos e icono |

## Qué tener en cuenta

* **Id de la aplicación.** `org.example.Slab6` es provisional. Antes de compartir el paquete, cámbialo (y los tres archivos que
  llevan ese nombre) por uno de un espacio de nombres tuyo, por ejemplo `io.github.<tu usuario>.Slab6`.
* **Permisos.** Ve `~/Documents`; cualquier otro lugar se abre desde File > Open / Save As, que usan el selector de archivos del
  escritorio y dan acceso solo al archivo elegido. Esos archivos aparecen bajo `/run/user/UID/doc/...`; en un Save As allí,
  escribe tú la extensión (`.kv6`, `.kvx`, `.vox`).
* **Ventana.** Usa X11 a propósito (XWayland en Wayland): SLAB6 devuelve el ratón real al salir por el borde de la ventana, y
  Wayland no lo permite. El portapapeles de imágenes también va por X11.
* **SDL2.** El runtime 26.08 la ofrece mediante `sdl2-compat` (API de SDL2 sobre SDL3). Si falla la compilación al buscar
  `sdl2-config` o `SDL.h`, hay que añadir un módulo de SDL2 al manifiesto.
* **Licencia.** Rige la licencia original de SLAB6: libre para uso no comercial, conservando el nombre de Ken Silverman y
  marcando las versiones modificadas como tales (véase la cabecera de `slab6.c`). Por eso el paquete no sirve para Flathub tal cual.
