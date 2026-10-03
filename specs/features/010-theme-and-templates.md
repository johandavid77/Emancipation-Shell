# Especificación de Funcionalidad: 010 - Theme, Palettes & Templates

## 1. Resumen y Objetivo
- **Descripción corta:** Sistema de temas completo (dark/light/auto), paletas builtin/community/wallpaper (Material 3), templates para apps externas.
- **Alcance:**
  - Modos dark/light/auto.
  - Paletas: Ayu, Catppuccin, Dracula, Eldritch, Gruvbox, Kanagawa, Noctalia, Nord, Rosé Pine, Tokyo-Night.
  - Generación desde wallpaper (Material 3 schemes).
  - Templates CSS/TOML para exportar temas a apps (post-hooks).
  - Hot-reload de tema.

## 2. Criterios de Aceptación (Definition of Done)
- [ ] Cambio de tema aplica globalmente <100ms sin reiniciar.
- [ ] Auto mode respeta ciclo día/noche.
- [ ] Templates generan archivos y ejecutan post_hook correctamente.
