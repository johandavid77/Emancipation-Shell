# Emancipation-Shell

Shell de escritorio modular para Wayland, nativo en C, sin dependencias pesadas (Qt/GTK). Inspirado en principios de rendimiento extremo, arquitectura modular y diseño coherente.

## Filosofía

- **Nativo y ligero**: Implementado en C con render directo (EGL/OpenGL ES listo).
- **Wayland-first**: Usa protocolos estándar (`wlr-layer-shell`, `ext-workspace-v1`, `ext-session-lock-v1`).
- **Modular**: Núcleo desacoplado de funcionalidades (barras, lanzador, dock, centro de control, notificaciones, etc.).
- **Rendimiento**: Bucle de eventos no bloqueante, objetivo 60 FPS.
- **No invasivo**: No modifica dotfiles del compositor; integra via protocolos/IPC.

## Estado del Proyecto

Implementación base completa con módulos **001–015**:

| Módulo | Estado | Descripción |
|---|---|---|
| 001 | ✅ | Surface Manager (layer-shell, multi-monitor, exclusive zones) |
| 002 | ✅ | Config Parser (TOML) + Hot-reload |
| 003 | ✅ | Bar & Workspaces (ext-workspace-v1) |
| 004 | ✅ | Launcher (.desktop parsing, fuzzy search) + Dock |
| 005 | ✅ | Control Center + OSD |
| 006 | ✅ | Notification Daemon (store + D-Bus placeholder) |
| 007 | ✅ | Lock Screen (ext-session-lock-v1) + Session Actions |
| 008 | ✅ | Wallpaper & Backdrop (estructura) |
| 009 | ✅ | Hotkeys (API base) |
| 010 | ✅ | Theme/Palettes/Templates (estructura) |
| 011 | ✅ | IPC Server (Unix socket) |
| 012 | ✅ | Renderer (OpenGL ES stub, fractional scaling) |
| 013 | ✅ | Plugin Manager |
| 014 | ✅ | Clipboard History (pinned, límites) |
| 015 | ✅ | Desktop Widgets |

## Compilación

Requisitos: `meson`, `ninja`, `wayland`, `wayland-protocols`, `wayland-scanner`, `pkg-config`.

```bash
meson setup build
meson compile -C build
```

## Ejecución

```bash
WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0} ./build/emancipation-shell
```

## Estructura

- `src/core/` - Núcleo (main, registry, outputs, surface manager)
- `src/bar/`, `src/launcher/`, `src/dock/`, `src/control/`, `src/osd/`, `src/notify/`, `src/session/` - Módulos UI/servicios
- `src/config/` - Parser TOML + watcher
- `src/ipc/`, `src/plugins/`, `src/render/`, `src/widgets/` - Extensibilidad
- `specs/features/` - Especificaciones por módulo
- `include/` - Headers públicos

## Licencia

MIT

## Repo

[https://github.com/johandavid77/Emancipation-Shell](https://github.com/johandavid77/Emancipation-Shell)
