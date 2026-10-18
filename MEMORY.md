# M10 — Extras (Emancipation-Shell)

Estado: implementación mínima (scaffold) para cumplir con la etapa M10.

## Componentes
- Hotkeys: stub existente (`src/hotkeys/hotkeys.c`, `include/hotkeys/hotkeys.h`).
  CC ya provee atajos (`@lock`, `@launcher`, `@screen_off`, `@network`, `@bluetooth`).
- Plugins: registro mínimo sin dlopen (`src/plugins/plugin.c`, `include/plugins/plugin.h`).
- Memory: este archivo.

## Plugin API
```c
struct plugin {
  const char *name;
  int (*init)(void *ctx);
  void (*fini)(void);
};
```

Funciones: `plugin_registry_init`, `plugin_registry_fini`, `plugin_register`,
`plugin_init_all`, `plugin_fini_all`.

## Notas
- Sin dependencias nuevas. Puede ampliarse a .so más adelante.
- Hotkeys pueden mapearse a IPC cuando se requiera.
