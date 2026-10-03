# Especificación de Funcionalidad: 005 - Control Center & OSD

## 1. Resumen y Objetivo
- **Descripción corta:** Panel lateral de ajustes rápidos (Control Center) y superposiciones visuales flotantes en pantalla (OSD) para cambios de volumen y brillo.
- **Alcance:**
  - Panel deslizante lateral con toggles rápidos para Wi-Fi, Bluetooth, Modo Oscuro, Silencio de Audio y Perfiles de Energía.
  - Indicadores OSD flotantes que aparecen temporalmente al modificar el hardware mediante teclas multimedia.

## 2. Criterios de Aceptación (Definition of Done)
- [ ] El panel de control se despliega fluidamente desde el borde de la pantalla al hacer click en el indicador correspondiente.
- [ ] Los cambios realizados en los toggles se comunican directamente con los servicios del sistema (Bluez, NetworkManager).
- [ ] Al presionar las teclas físicas de volumen o brillo, aparece el OSD con una animación suave y desaparece tras un tiempo de inactividad de 2 segundos.