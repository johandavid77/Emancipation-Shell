# Especificación de Funcionalidad: 012 - Rendering, FPS & Fractional Scaling

## 1. Resumen y Objetivo
- **Descripción corta:** Render OpenGL ES estable, FPS objetivo 60+, soporte a fractional scaling y gestión de contexto GPU.
- **Alcance:**
  - Render de superficies layer-shell con EGL/GL.
  - Animaciones frame-driven (vsync).
  - Fractional scaling (cuando compositor lo soporta).
  - shared_gl_context configurable.

## 2. Criterios de Aceptación (Definition of Done)
- [ ] Idle CPU<1%, FPS estable 60 en reposo/animaciones suaves.
- [ ] Fractional scaling aplicado correctamente a barras/paneles.
- [ ] No leaks de GL/EGL en hot-reload/reconfigure.
