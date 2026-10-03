# Especificación de Funcionalidad: 014 - Clipboard History & Management

## 1. Resumen y Objetivo
- **Descripción corta:** Historial de portapapeles con favoritos (pinned), auto-paste y acciones sobre imágenes.
- **Alcance:**
  - Seguimiento de selección/clipboard Wayland.
  - Max entries configurable, pinned persistente.
  - Auto-paste modes (off/auto/ctrl_v/ctrl_shift_v/shift_insert).
  - Clipboard image action command (preview/edit).

## 2. Criterios de Aceptación (Definition of Done)
- [ ] Items copiados aparecen en historial <100ms.
- [ ] Pinned no se purgan por límite.
- [ ] Auto-paste funciona según modo configurado.
