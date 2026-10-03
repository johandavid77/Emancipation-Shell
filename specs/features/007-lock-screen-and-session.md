# Especificación de Funcionalidad: 007 - Lock Screen & Session Actions

## 1. Resumen y Objetivo
- **Descripción corta:** Pantalla de bloqueo segura utilizando los protocolos de bloqueo de sesión de Wayland y menú de gestión de energía (apagado, reinicio, suspensión).
- **Alcance:**
  - Protocolo `ext-session-lock-v1` para garantizar un bloqueo de pantalla robusto e inquebrantable.
  - Interfaz de ingreso de contraseña o desbloqueo.
  - Panel de acciones de sesión (Apagar, Reiniciar, Suspender, Cerrar sesión) conectado a `systemd-logind`.

## 2. Requisitos Técnicos y Protocolos Wayland
- **Protocolos:** `ext-session-lock-v1`.

## 3. Criterios de Aceptación (Definition of Done)
- [ ] La pantalla de bloqueo captura todas las entradas de teclado y no permite el acceso al escritorio hasta introducir credenciales válidas.
- [ ] El menú de sesión ejecuta correctamente las órdenes de apagado o suspensión a través de las interfaces del sistema operativo sin errores de permisos.