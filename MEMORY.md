# Memoria del Proyecto: Emancipation-Shell

## 📊 Estado Actual del Proyecto
- **Fase Actual:** Preparación y Diseño de Especificaciones (SDD).
- **Módulo Activo:** `001-surface-manager.md` (Pendiente de codificación).
- **Progreso General:** 15% (Estructura base, arquitectura y specs definidas).

## 🏛️ Decisiones Técnicas Clave (Decision Log)
- **Base Tecnológica:** Shell nativo en Wayland / OpenGL ES (sin dependencias de Qt/GTK).
- **Gestión de Servicios:** Preferencia por OpenRC / runit a nivel general del sistema, arquitectura modular desacoplada del compositor.
- **Metodología:** Desarrollo Guiado por Especificaciones (SDD) con orquestación de subagentes.

## 🚀 Próximos Pasos Inmediatos
1. Inicializar la implementación del código base para el **Surface Manager (`001`)**.
2. Conectar las llamadas al protocolo `wlr-layer-shell-unstable-v1`.
3. Validar los primeros criterios de aceptación con el Subagente de QA.

## ✅ Historial de Módulos Completados
* [ ] `001-surface-manager.md` - Gestión de capas Wayland.
* [ ] `002-config-parser.md` - Motor TOML y hot-reload.
* [ ] `003-bar-and-widgets.md` - Barra multi-monitor y widgets.
* [ ] `004-launcher-and-dock.md` - Lanzador flotante y dock.
* [ ] `005-control-center-and-osd.md` - Centro de control y OSD.
* [ ] `006-notification-daemon.md` - Demonio de notificaciones D-Bus.
* [ ] `007-lock-screen-and-session.md` - Pantalla de bloqueo y sesión.