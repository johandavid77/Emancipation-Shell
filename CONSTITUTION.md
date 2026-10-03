# Constitución de Proyecto: [Nombre de tu Shell]

## Artículo I: Filosofía y Alcance (Core Philosophy)
1. **Naturaleza de Shell, no de DE:** El proyecto es exclusivamente una capa de shell y servicios visuales para compositores Wayland. No gestionará tiling, pantallas físicas ni drivers de hardware (eso corresponde al compositor o servicios del sistema).
2. **Independencia de Frameworks Pesados:** Se priorizará el desarrollo nativo o mediante bibliotecas de bajo nivel / renderizado directo (ej. OpenGL ES, Vulkan, o wrappers ligeros). Queda prohibido el acoplamiento a frameworks pesados de entornos de escritorio monolíticos (como Qt o GTK completos), salvo componentes de integración estrictamente necesarios.
3. **No Invasivo:** El shell jamás modificará archivos de configuración del compositor (*dotfiles*) por defecto. Toda interacción se basará en protocolos estándar o IPC expuesto por el compositor.

## Artículo II: Rendimiento y Eficiencia
1. **Presupuesto de Recursos:** El consumo de memoria base en reposo no debe superar los umbrales definidos para entornos minimalistas. Las animaciones y renderizados de superficies deben mantener un objetivo estricto de 60 FPS mínimos.
2. **Recarga en Caliente (Hot Reload):** Cualquier cambio en el archivo de configuración principal debe aplicarse en tiempo de ejecución sin reiniciar la sesión del usuario.

## Artículo III: Adherencia a Estándares y Protocolos
1. **Wayland First:** El posicionamiento de superficies se regirá estrictamente bajo protocolos estándar de Wayland (`layer-shell`, `ext-workspace-v1`, etc.). Evitar hacks propietarios específicos de un solo compositor a menos que estén aislados en plugins modulares opcionales.

## Artículo IV: Calidad de Código y Arquitectura
1. **Diseño Modular:** Las características secundarias (bandejas, widgets avanzados, conectores de clima) deben aislarse mediante un sistema de plugins desacoplado del núcleo.
2. **Invariantes de Arquitectura:** Ningún componente de la interfaz gráfica debe bloquear el hilo principal de eventos o servicios IPC.