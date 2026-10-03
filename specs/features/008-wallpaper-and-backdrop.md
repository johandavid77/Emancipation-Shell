# Especificación de Funcionalidad: 008 - Wallpaper & Backdrop

## 1. Resumen y Objetivo
- **Descripción corta:** Gestión de fondos de pantalla por monitor con modos de relleno, transiciones suaves y efecto backdrop para paneles.
- **Alcance:**
  - Cargador de fondos desde directorios (recursivo, día/noche opcional).
  - Modos de relleno: center, crop, fit, stretch, repeat, span.
  - Transiciones animadas al cambiar wallpaper.
  - Backdrop blur/tint para paneles flotantes (control center, launcher, notificaciones).
  - Automatización de cambio periódico con orden aleatorio/alfabético.

## 2. Requisitos Técnicos
- **Renderizado:** Integración con render OpenGL ES para wallpapers/transiciones.
- **Configuración:** TOML [wallpaper], [backdrop] con hot-reload.

## 3. Criterios de Aceptación (Definition of Done)
- [ ] Cambiar wallpaper aplica correctamente por monitor con modo de relleno.
- [ ] Transiciones animadas ejecutadas sin drops de FPS (<16ms).
- [ ] Backdrop blur/tint aplicado a paneles flotantes activables por config.
- [ ] Automatización de wallpaper funciona con intervalo configurable y respeta orden.
