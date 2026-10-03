# Especificación de Funcionalidad: 006 - Notification Daemon

## 1. Resumen y Objetivo
- **Descripción corta:** Servidor de notificaciones compatible con el estándar de FreeDesktop para capturar, mostrar y almacenar alertas visuales del sistema.
- **Alcance:**
  - Interfaz D-Bus para recibir solicitudes de notificación de aplicaciones externas.
  - Renderizado de alertas flotantes temporales (*toast notifications*).
  - Centro de historial de notificaciones accesible desde la barra o el centro de control.

## 2. Criterios de Aceptación (Definition of Done)
- [ ] El shell actúa exitosamente como el demonio de notificaciones por defecto del sistema D-Bus (`org.freedesktop.Notifications`).
- [ ] Las notificaciones entran visualmente con una animación limpia en la esquina de la pantalla asignada.
- [ ] Las notificaciones no leídas se almacenan en un historial persistente durante la sesión.