# Especificación de Funcionalidad: 003 - Bar & Widgets System

## 1. Resumen y Objetivo
- **Descripción corta:** Implementación de barras superiores/inferiores multi-monitor altamente modulares y contenedor de widgets de estado.
- **Alcance:**
  - Renderizado de barras en los bordes definidos por el `Surface Manager`.
  - Módulos / Widgets iniciales:
    - Indicador de espacios de trabajo (Workspaces) usando `ext-workspace-v1`.
    - Reloj y fecha personalizables.
    - Bandeja de sistema (`System Tray` / Status Notifier Items).
    - Indicadores de hardware (Batería, Volumen de Audio mediante Pipewire, Red/Wi-Fi).

## 2. Requisitos Técnicos y Protocolos Wayland
- **Protocolos:** `ext-workspace-v1` para la gestión y estado de escritorios virtuales.
- **Servicios Externos:** Conexión a demonios de audio (Pipewire/PulseAudio) y redes (NetworkManager/iwd).

## 3. Criterios de Aceptación (Definition of Done)
- [ ] Las barras se despliegan de manera independiente en cada monitor detectado.
- [ ] El widget de espacios de trabajo refleja en tiempo real el cambio de foco de ventanas del compositor (ej. Niri o Hyprland).
- [ ] Los clicks y interacciones sobre los widgets (ej. cambiar volumen con la rueda del mouse) responden inmediatamente.