# Especificación de Funcionalidad: 001 - Surface Manager

## 1. Resumen y Objetivo
- **Descripción corta:** Implementar el administrador de superficies base utilizando el protocolo `layer-shell` de Wayland para anclar elementos de la interfaz (barras, docks, paneles) en los bordes de la pantalla.
- **Alcance:** 
  - Conexión al servidor de pantalla de Wayland.
  - Creación y gestión de superficies de capa (`layer-shell`).
  - Configuración de anclajes (arriba, abajo, izquierda, derecha) y zonas de exclusión de espacio (*exclusive zones*).
  - Soporte multi-monitor detectando salidas dinámicas (`wl-output`).
- **Exclusiones:** No incluye la lógica visual interna de los widgets (eso va en especificaciones posteriores); este módulo solo gestiona los contenedores y superficies vacías.

## 2. Requisitos Técnicos y Protocolos Wayland
- **Protocolos necesarios:** 
  - `wlr-layer-shell-unstable-v1` (o protocolo de capa estándar adoptado).
  - `wl-output` (para gestión de pantallas y resoluciones).
- **Dependencias del Sistema:** 
  - Librerías cliente de Wayland (`wayland-client`, `wayland-protocols`).
  - Compilación de encabezados de protocolos Wayland mediante generadores de código estático (ej. `wayland-scanner`).

## 3. Criterios de Aceptación (Definition of Done)
- [ ] El proceso se conecta exitosamente al socket de Wayland del compositor (ej. Niri / Hyprland).
- [ ] Se crea una superficie de capa en el borde superior con una altura fija y un ancho que abarque todo el monitor.
- [ ] La zona exclusiva (*exclusive zone*) funciona correctamente, obligando al compositor a reservar espacio para que las ventanas maximizadas no se metan debajo de la barra.
- [ ] Si se conecta o desconecta un monitor, el gestor adapta, destruye o crea las superficies correspondientes sin fugas de memoria ni crashes.
- [ ] El consumo de recursos en reposo de este módulo es prácticamente nulo.

## 4. Comportamiento y Ciclo de Vida (UX / Runtime)
- **Inicialización:** Al arrancar el shell, se enumeran los monitores activos y se despliegan las capas configuradas.
- **Eventos de redimensionamiento:** Si el usuario cambia la resolución o la escala del monitor, el gestor recalcula el ancho/alto de la superficie de forma fluida.
- **Cierre seguro:** Ante una señal de interrupción, las superficies se destruyen ordenadamente enviando las solicitudes de liberación de protocolo correspondientes antes de cerrar el proceso.