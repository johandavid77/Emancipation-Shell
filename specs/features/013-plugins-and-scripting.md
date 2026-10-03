# Especificación de Funcionalidad: 013 - Plugin System & Script Widgets

## 1. Resumen y Objetivo
- **Descripción corta:** Sistema de plugins modular y widgets script-backed para extensibilidad.
- **Alcance:**
  - Carga dinámica (.so) o scripts (shell/python) con API estable.
  - Widget script (interval, click, signals) con salida formateada.
  - Sandbox básico, permisos declarativos.

## 2. Criterios de Aceptación (Definition of Done)
- [ ] Plugin cargado/descargado sin reiniciar shell.
- [ ] Script widget actualiza con intervalo y responde a clicks.
- [ ] Errores de plugin no afectan núcleo (aislado).
