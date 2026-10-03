# Emancipation-Shell

Módulo 001 - Surface Manager (wlr-layer-shell-unstable-v1)

## Requisitos

- meson, ninja-build
- wayland, wayland-protocols, wayland-scanner
- pkg-config

## Compilación

```bash
cd /home/johan/Desarrollos/Emancipation-Shell
meson setup build
meson compile -C build
```

## Ejecución

```bash
WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0} ./build/emancipation-shell
```

## Prueba headless (opcional)

```bash
weston --backend=headless --socket=wayland-test &
WAYLAND_DISPLAY=wayland-test ./build/emancipation-shell
pkill weston
```
