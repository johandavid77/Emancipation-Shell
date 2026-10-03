# Especificación de Arquitectura

## 1. Topología del Sistema
El shell se divide en tres capas principales:
+-------------------------------------------------------------+
|                     Capa de Presentación                    |
|      (Barras, Widgets, OSD, Lanzador, Centro de Control)    |
+-------------------------------------------------------------+
│ (Render / Eventos)
+-------------------------------------------------------------+
|                      Core / Shell Engine                    |
|     (Gestor de Superficies, State Machine, Protocolos Wayland)|
+-------------------------------------------------------------+
│ (IPC / Signals)
+-------------------------------------------------------------+
|                     Servicios de Fondo                      |
|         (Config Watcher, Plugin Loader, IPC Server)         |
+-------------------------------------------------------------+

## 2. Subsistemas Clave
- **Surface Manager:** Administra las capas de Wayland (`layer-shell`) para cada monitor activo, gestionando la visibilidad según el estado de enfoque y pantallas de bloqueo.
- **Config & State Watcher:** Monitorea los cambios en el archivo de configuración (ej. TOML) mediante inotify/equivalentes y emite eventos globales de recarga.
- **IPC Server:** Servidor de comunicación por sockets locales para permitir control mediante scripts externos o atajos de teclado.
- **Plugin System:** Interfaz de carga dinámica para extender capacidades sin compilar el núcleo.