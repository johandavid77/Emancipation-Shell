project('emancipation-shell', 'c',
  version: '0.1.0',
  default_options: ['warning_level=2', 'c_std=c11']
)

wl_client = dependency('wayland-client', required: true)
sc = find_program('wayland-scanner', required: true)

layer_xml = files('protocols/wlr-layer-shell-unstable-v1.xml')
ws_xml = files('protocols/ext-workspace-v1.xml')
sl_xml = files('protocols/ext-session-lock-v1.xml')
xdg_xml = files('protocols/xdg-shell.xml')

layer_h = custom_target('layer-shell-client-protocol.h',
  input: layer_xml,
  output: 'zwlr-layer-shell-v1-client-protocol.h',
  command: [sc, 'client-header', '@INPUT@', '@OUTPUT@'],
  install: false
)
layer_c = custom_target('layer-shell-client-protocol.c',
  input: layer_xml,
  output: 'zwlr-layer-shell-v1-client-protocol.c',
  command: [sc, 'private-code', '@INPUT@', '@OUTPUT@'],
  install: false
)

ws_h = custom_target('ext-workspace-client-protocol.h',
  input: ws_xml,
  output: 'ext-workspace-v1-client-protocol.h',
  command: [sc, 'client-header', '@INPUT@', '@OUTPUT@'],
  install: false
)
ws_c = custom_target('ext-workspace-client-protocol.c',
  input: ws_xml,
  output: 'ext-workspace-v1-client-protocol.c',
  command: [sc, 'private-code', '@INPUT@', '@OUTPUT@'],
  install: false
)

sl_h = custom_target('ext-session-lock-client-protocol.h',
  input: sl_xml,
  output: 'ext-session-lock-v1-client-protocol.h',
  command: [sc, 'client-header', '@INPUT@', '@OUTPUT@'],
  install: false
)
sl_c = custom_target('ext-session-lock-client-protocol.c',
  input: sl_xml,
  output: 'ext-session-lock-v1-client-protocol.c',
  command: [sc, 'private-code', '@INPUT@', '@OUTPUT@'],
  install: false
)

xdg_h = custom_target('xdg-shell-client-protocol.h',
  input: xdg_xml,
  output: 'xdg-shell-client-protocol.h',
  command: [sc, 'client-header', '@INPUT@', '@OUTPUT@'],
  install: false
)
xdg_c = custom_target('xdg-shell-client-protocol.c',
  input: xdg_xml,
  output: 'xdg-shell-client-protocol.c',
  command: [sc, 'private-code', '@INPUT@', '@OUTPUT@'],
  install: false
)

extra_protos = []

foreach proto : [['cursor-shape-v1', 'protocols/cursor-shape-v1.xml'],
                 ['tablet-unstable-v2', 'protocols/tablet-unstable-v2.xml'],
                 ['ext-foreign-toplevel-list-v1', 'protocols/ext-foreign-toplevel-list-v1.xml']]
  extra_protos += custom_target(proto[0] + '-client-protocol.h',
    input: files(proto[1]), output: proto[0] + '-client-protocol.h',
    command: [sc, 'client-header', '@INPUT@', '@OUTPUT@'])
  extra_protos += custom_target(proto[0] + '-client-protocol.c',
    input: files(proto[1]), output: proto[0] + '-client-protocol.c',
    command: [sc, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

gen_inc = include_directories('.')

inc_dirs = include_directories('include')

srcs = [
  'src/core/main.c',
  'src/core/output.c',
  'src/core/registry.c',
  'src/core/surface_mgr.c',
  'src/util/log.c',
  'src/config/config.c',
  'src/config/parser.c',
  'src/config/watcher.c',
  'src/bar/bar.c',
  'src/bar/workspaces.c',
  'src/bar/sysinfo.c',
  'src/core/seat.c',
  'src/core/panel.c',
  'src/compositors/niri_ipc.c',
  'src/calendar/ics.c',
  'src/launcher/panel.c',
  'src/launcher/desktop.c',
  'src/launcher/icons.c',
  'src/launcher/launcher.c',
  'src/dock/dock.c',
  'src/control/control_center.c',
  'src/control/calendar_panel.c',
  'src/osd/osd.c',
  'src/notify/notification.c',
  'src/notify/notify_daemon.c',
  'src/session/lock_screen.c',
  'src/session/session_actions.c',
  'src/wallpaper/wallpaper.c',
  'src/hotkeys/hotkeys.c',
  'src/ipc/ipc.c',
  'src/clipboard/clipboard.c',
  'src/render/renderer.c',
  'src/plugins/plugin.c',
  'src/widgets/desktop_widget.c',
  'src/third_party/tomlc99/toml.c',
  layer_c, layer_h,
  xdg_c, xdg_h,
  extra_protos,
  ws_c, ws_h,
  sl_c, sl_h
]

executable('emancipation-shell',
  srcs,
  include_directories: [inc_dirs, gen_inc, include_directories('src')],
  dependencies: [
    wl_client,
    dependency('cairo', required: true),
    dependency('pangocairo', required: true),
    dependency('xkbcommon', required: true),
  ],
  install: false
)
