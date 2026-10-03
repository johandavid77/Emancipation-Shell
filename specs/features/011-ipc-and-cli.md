# Especificación de Funcionalidad: 011 - IPC Server & CLI (`noctalia msg`)

## 1. Resumen y Objetivo
- **Descripción corta:** Servidor IPC local (Unix socket) + cliente CLI para controlar shell por scripts.
- **Alcance:**
  - Unix domain socket con protocolo JSON/simple.
  - Comandos: toggle-launcher, show/hide CC, toggle-dock, notification-clear-*, reload-config, theme-set, wallpaper-set, etc.
  - Cliente `noctalia msg <cmd> [args]`.

## 2. Requisitos Técnicos
- **IPC:** Unix socket integrado al event loop Wayland.

## 3. Criterios de Aceptación (Definition of Done)
- [ ] Socket creado en $XDG_RUNTIME_DIR/noctalia/ipc.sock.
- [ ] Comandos responden con éxito/error JSON.
- [ ] `noctalia msg` CLI funcional standalone.
