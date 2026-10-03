# Especificación de Funcionalidad: 004 - Launcher & Dock

## 1. Resumen y Objetivo
- **Descripción corta:** Lanzador rápido de aplicaciones flotante (tipo *spotlight* / rofi-like) indexando archivos `.desktop` del sistema y un dock inferior para aplicaciones ancladas.
- **Alcance:**
  - Análisis (*parsing*) de rutas de aplicaciones del sistema (`/usr/share/applications`, `~/.local/share/applications`).
  - Algoritmo de búsqueda rápida / coincidencia difusa (*fuzzy search*).
  - Panel flotante centrado para el lanzador y barra de dock inferior interactiva para favoritos.

## 2. Criterios de Aceptación (Definition of Done)
- [ ] Al activar el atajo del lanzador, aparece la interfaz flotante con foco de teclado inmediato.
- [ ] La búsqueda filtra aplicaciones instantáneamente a medida que el usuario escribe.
- [ ] Al presionar Enter sobre un resultado, se ejecuta el comando de la aplicación y el lanzador se oculta limpiamente.
- [ ] El dock muestra las aplicaciones fijadas y las activas en ejecución.