# archpaper

Gestor de fondos de pantalla para **Wayland** en Arch Linux y derivadas, con **interfaz gráfica en Qt6** y CLI integrada para autostart/scripts.

## Características

- **GUI en Qt6** con organización en tres paneles:
  - Carpetas favoritas a la izquierda.
  - Miniaturas de imágenes del directorio seleccionado en el centro.
  - Vista previa grande y controles a la derecha.
- **Filtro rápido** por nombre de archivo.
- **Doble clic** para aplicar un wallpaper directamente.
- Backends **swaybg** (universal Wayland) y **hyprpaper** (Hyprland).
- Detección automática de backend en Hyprland.
- Modo daemon para cambios automáticos por intervalo.
- Soporte opcional de `wallust` para generar esquemas de color desde el wallpaper.
- Configuración persistente en `~/.config/archpaper/config`.

## Dependencias

```text
swaybg
qt6-base
```

Opcional:

```text
hyprpaper
wallust
```

Para compilar:

```text
cmake
base-devel
```

## Compilar

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Instalar

```bash
sudo cmake --install build --prefix /usr
```

O desde PKGBUILD:

```bash
makepkg -si
```

También genera un archivo `.desktop` para aparecer en el menú de aplicaciones.

## Uso

### Interfaz gráfica

```bash
archpaper
```

En la ventana:
- Selecciona una carpeta de la lista (o añade una nueva).
- Haz clic en una miniatura para ver la vista previa.
- Doble clic o botón **Aplicar** para establecer el fondo.
- Usa el cuadro de búsqueda para filtrar por nombre.
- Activa el daemon para cambios automáticos.

### CLI

```bash
archpaper set <imagen> [--mode fill|fit|stretch|center|tile] [--backend swaybg|hyprpaper] [--wallust] [--wallust-hook <script>]
archpaper random <directorio> [--wallust] [--wallust-hook <script>]
archpaper daemon <directorio> --interval <segundos> [--wallust] [--wallust-hook <script>]
archpaper clear
archpaper status
archpaper backend
```

## Integración con wallust

Si tienes `wallust` instalado, puedes generar automáticamente un esquema de
color a partir del wallpaper:

```bash
archpaper set ~/Imágenes/fondo.jpg --wallust
```

También puedes activarlo desde la GUI marcando *Generar esquema con wallust*.
La opción se guarda en `~/.config/archpaper/config` bajo `wallust=true|false`.

`archpaper` ejecuta `wallust run <imagen>`; eso hace que **wallust use tu propia
configuración** de `~/.config/wallust/wallust.toml` para escribir sus templates y
sus hooks. Es decir, si en tu `wallust.toml` tienes un `[hooks.reload]` que
recarga waybar, kitty, etc., ya funciona **sin añadir nada más**.

### Hook adicional (opcional)

Si además quieres algo fuera de `wallust.toml`, puedes usar el campo *Hook* de
la GUI o `--wallust-hook <script>` en la CLI:

```bash
archpaper set ~/Imágenes/fondo.jpg --wallust --wallust-hook ~/.config/archpaper/otro_hook.sh
```

El script recibirá el wallpaper como `$1` y en la variable `$WALLPAPER`:

## Integración con compositores

### Hyprland

```ini
exec-once = archpaper set ~/Imágenes/fondo.jpg
```

### Sway

```sway
exec archpaper set ~/Imágenes/fondo.jpg
```

## Estructura del proyecto

```text
include/archpaper/  # API pública del núcleo en C
src/core/           # Núcleo: backend, config, daemon, utils
src/cli/            # Punto de entrada CLI
src/gui/            # Interfaz gráfica Qt6
```

## Licencia

MIT
