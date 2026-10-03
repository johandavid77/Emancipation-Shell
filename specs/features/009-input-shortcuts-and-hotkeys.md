# Especificación de Funcionalidad: 009 - Input, Shortcuts & Hotkeys

## 1. Resumen y Objetivo
- **Descripción corta:** Gestor de atajos globales y manejo de input (teclado/raton) para activar módulos (launcher, CC, OSD, lock, session).
- **Alcance:**
  - Mapeo configurable de hotkeys (TOML).
  - Activación de launcher (Super/Alt+Space), control center, clipboard, wallpaper, session, lock.
  - Soporte OSD por teclas multimedia (volumen/brillo) con auto-hide 2s.
  - Integración con compositor para shortcuts seguros.

## 2. Requisitos Técnicos
- **Input:** wl_seat, keyboard/pointer listeners.
- **Eventos:** Dispatch por event loop Wayland.

## 3. Criterios de Aceptación (Definition of Done)
- [ ] Hotkeys config definidos en TOML aplican sin reiniciar.
- [ ] Atajos activan módulos correctamente con foco inmediato (launcher).
- [ ] Multimedia activa OSD con animación suave y auto-hide ~2s.
