# Especificación de Funcionalidad: 002 - Config Parser & Hot Reload

## 1. Resumen y Objetivo
- **Descripción corta:** Motor central de configuración basado en archivos **TOML** con capacidad de lectura, validación de esquemas y recarga en tiempo de ejecución (*hot-reload*) sin reiniciar la sesión.
- **Alcance:** 
  - Parser de archivos TOML (ej. lectura de `config.toml`).
  - Sistema de vigilancia de archivos del sistema de archivos (`inotify` o equivalente).
  - Estructuras de datos internas tipadas para cada módulo del shell (barras, temas, fuentes, atajos).
  - Manejo de errores tolerante a fallos (si el usuario comete un error de sintaxis en el TOML, el shell mantiene la última configuración válida y emite una notificación de advertencia).

## 2. Requisitos Técnicos
- **Dependencias:** Librería de análisis TOML del lenguaje seleccionado (ej. `toml` en Rust o C++).
- **APIs del Sistema:** Monitoreo de cambios de archivos del kernel (`inotify`).

## 3. Criterios de Aceptación (Definition of Done)
- [ ] El shell carga correctamente la configuración por defecto si no existe un archivo de usuario.
- [ ] Modificar una propiedad en el archivo TOML (ej. tamaño de fuente o color de fondo) aplica el cambio en la interfaz gráfica en menos de 100ms sin reiniciar el proceso.
- [ ] Un error de sintaxis en el archivo de configuración no crashea el shell; muestra un mensaje de error por consola/notificación y preserva el estado anterior.