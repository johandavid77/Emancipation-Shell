# Especificación de Funcionalidad: 015 - Desktop Widgets

## 1. Resumen y Objetivo
- **Descripción corta:** Widgets flotantes en escritorio (clock, calendar, notes, system stats) posicionables.
- **Alcance:**
  - Layer-shell independiente o overlay según diseño.
  - Draggable, persistencia de posición/tamaño.
  - Widgets builtin + plugin/script.

## 2. Criterios de Aceptación (Definition of Done)
- [ ] Widgets flotantes renderizados sin interferir con layer-shell de bar/dock.
- [ ] Posiciones guardadas entre reinicios.
- [ ] Soportan mostrar/ocultar globalmente.
