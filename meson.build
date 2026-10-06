project('shady', 'c', version: '0.1.0',
  default_options: ['buildtype=debugoptimized', 'c_std=c11', 'warning_level=2'])

wlroots = dependency('wlroots-0.20', version: ['>=0.20.2', '<0.21'])
wlr_protocols = dependency('wlr-protocols')
wayland_scanner = find_program('wayland-scanner')
wlr_protocols_dir = wlr_protocols.get_variable(pkgconfig: 'pkgdatadir')
wayland_protocols_server = dependency('wayland-protocols')
wayland_protocols_server_dir = wayland_protocols_server.get_variable(pkgconfig: 'pkgdatadir')
subdir('compositor/protocols/wayland-protocols')
layer_shell_xml = join_paths(wlr_protocols_dir, 'unstable/wlr-layer-shell-unstable-v1.xml')
layer_shell_protocol_header = custom_target(
  'wlr-layer-shell-unstable-v1-protocol.h',
  input: layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)
layer_shell_protocol_code = custom_target(
  'wlr-layer-shell-unstable-v1-protocol.c',
  input: layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

shady_shell_protocol_xml = files('compositor/protocols/shady-shell-v1.xml')
shady_shell_server_header = custom_target(
  'shady-shell-v1-server-protocol.h',
  input: shady_shell_protocol_xml,
  output: 'shady-shell-v1-server-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)
shady_shell_client_header = custom_target(
  'shady-shell-v1-client-protocol.h',
  input: shady_shell_protocol_xml,
  output: 'shady-shell-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)
shady_shell_protocol_code = custom_target(
  'shady-shell-v1-protocol.c',
  input: shady_shell_protocol_xml,
  output: 'shady-shell-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

cc = meson.get_compiler('c')
spatial_enabled = get_option('spatial').enabled()
lua_enabled = get_option('lua').enabled()

egl = dependency('egl', required: spatial_enabled)
glesv2 = dependency('glesv2', required: spatial_enabled)
lua = cc.find_library('lua', required: true)
dl = cc.find_library('dl', required: false)
# Shaders, shell data and plugin assets live under one root that mirrors the
# source tree, so plugins resolve them through SHADY_ROOT either way.
shady_data_install_dir = join_paths(get_option('datadir'), 'shady')
plugin_install_dir = join_paths(get_option('libdir'), 'shady', 'plugins')
if get_option('installed_data')
  shady_data_root = join_paths(get_option('prefix'), shady_data_install_dir)
else
  shady_data_root = meson.project_source_root()
endif
shader_dir = join_paths(shady_data_root, 'compositor', 'shaders')

shady_sources = [
  layer_shell_protocol_header,
  'compositor/src/main.c',
  'compositor/src/module/module.c',
  'compositor/src/event/event.c',
  'compositor/src/module/builtin.c',
  'compositor/src/plugin/plugin.c',
  'compositor/src/plugin/manager.c',
  'compositor/src/plugin/manager_lua.c',
  'compositor/src/plugin/host_api.c',
  'compositor/src/plugin/motion.c',
  'compositor/src/plugin/representation.c',
  'compositor/src/config.c',
  'compositor/src/config_lua.c',
  'compositor/src/titlebar.c',
  'compositor/src/input.c',
  'compositor/src/xdg.c',
  'compositor/src/modules/window_motion/window_motion.c',
  'compositor/src/layer.c',
  'compositor/src/output.c',
  'compositor/src/output_management.c',
  'compositor/src/session_lock.c',
  'compositor/src/shell_protocol.c',
  'compositor/src/standard_protocols.c',
  ext_workspace_enum_header,
  'compositor/src/ipc/ipc.c',
  'compositor/src/ipc/json.c',
  'compositor/src/ipc/values.c',
  shady_shell_server_header,
  shady_shell_protocol_code,
  'compositor/src/modules/desktop/module.c',
  'compositor/src/modules/desktop/ime.c',
  'compositor/src/modules/workspace/workspace.c',
  'compositor/src/modules/workspace/module.c',
]

if spatial_enabled
  shady_sources += [
    'compositor/src/render/render.c',
    'compositor/src/render/gl_pipeline.c',
    'compositor/src/render/math3d.c',
    'compositor/src/render/pick3d.c',
    'compositor/src/experimental/runtime.c',
    'compositor/src/modules/environment/environment.c',
    'compositor/src/modules/environment/loaders.c',
    'compositor/src/modules/environment/scene.c',
    'compositor/src/modules/environment/draw.c',
    'compositor/src/modules/environment/sky.c',
    'compositor/src/modules/spatial/module.c',
    'compositor/src/plugin/environment.c',
  ]
else
  shady_sources += 'compositor/src/render/desktop.c'
endif

if lua_enabled
  shady_sources += [
    'compositor/src/modules/lua/lua.c',
    'compositor/src/modules/lua/module.c',
  ]
endif

physics_enabled = spatial_enabled and get_option('physics').enabled()
fps_enabled = spatial_enabled and get_option('fps').enabled()
window_motion_enabled = spatial_enabled and get_option('window_motion').enabled()
obj_loader_enabled = spatial_enabled and get_option('obj_loader').enabled()
close_animation_enabled = spatial_enabled and get_option('close_animation').enabled()
scene_effects_enabled = spatial_enabled and get_option('scene_effects').enabled()

if physics_enabled
  shady_sources += [
    'compositor/src/modules/physics/physics.c',
    'compositor/src/modules/physics/collision.c',
    'compositor/src/modules/physics/module.c',
  ]
endif
if fps_enabled
  shady_sources += [
    'compositor/src/modules/fps/fps.c',
    'compositor/src/modules/fps/module.c',
  ]
endif
if close_animation_enabled
  shady_sources += [
    'compositor/src/modules/close_animation/close_animation.c',
    'compositor/src/modules/close_animation/module.c',
  ]
endif
if scene_effects_enabled
  shady_sources += [
    'compositor/src/modules/scene_effects/scene_effects.c',
    'compositor/src/modules/scene_effects/module.c',
  ]
endif

core_cairo = dependency('cairo')
core_pangocairo = dependency('pangocairo')
core_pangoft2 = dependency('pangoft2')
core_fontconfig = dependency('fontconfig')

shady_deps = [
  wlroots,
  dependency('wayland-server'),
  dependency('xkbcommon'),
  dependency('libinput'),
  cc.find_library('m', required: true),
  lua,
  dl,
  core_cairo,
  core_pangocairo,
  core_pangoft2,
  core_fontconfig,
]
if spatial_enabled
  shady_deps += [egl, glesv2]
endif

shell_option = get_option('shell')
wayland_client = dependency('wayland-client', required: shell_option)
cairo = dependency('cairo', required: shell_option)
pangocairo = dependency('pangocairo', required: shell_option)
shell_xkbcommon = dependency('xkbcommon', required: shell_option)
# GL composition for the shell; without these it renders with wl_shm only.
shell_egl = dependency('egl', required: false)
shell_glesv2 = dependency('glesv2', required: false)
shell_wayland_egl = dependency('wayland-egl', required: false)
shell_gl_deps_found = shell_egl.found() and shell_glesv2.found() and shell_wayland_egl.found()
shell_gl_deps = shell_gl_deps_found ? [shell_egl, shell_glesv2, shell_wayland_egl] : []
wayland_protocols = dependency('wayland-protocols', required: shell_option)
shell_enabled = wayland_client.found() and cairo.found() and pangocairo.found() and shell_xkbcommon.found() and wayland_protocols.found()

if shell_enabled
  wayland_protocols_dir = wayland_protocols.get_variable(pkgconfig: 'pkgdatadir')
  xdg_shell_xml = join_paths(wayland_protocols_dir, 'stable/xdg-shell/xdg-shell.xml')
  xdg_shell_client_header = custom_target(
    'xdg-shell-client-protocol.h',
    input: xdg_shell_xml,
    output: 'xdg-shell-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  xdg_shell_protocol_code = custom_target(
    'xdg-shell-protocol.c',
    input: xdg_shell_xml,
    output: 'xdg-shell-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
  ext_session_lock_xml = join_paths(
    wayland_protocols_dir,
    'staging/ext-session-lock/ext-session-lock-v1.xml')
  ext_session_lock_client_header = custom_target(
    'ext-session-lock-v1-client-protocol.h',
    input: ext_session_lock_xml,
    output: 'ext-session-lock-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  ext_session_lock_protocol_code = custom_target(
    'ext-session-lock-v1-protocol.c',
    input: ext_session_lock_xml,
    output: 'ext-session-lock-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
  output_management_xml = join_paths(
    wlr_protocols_dir,
    'unstable/wlr-output-management-unstable-v1.xml')
  output_management_client_header = custom_target(
    'wlr-output-management-unstable-v1-client-protocol.h',
    input: output_management_xml,
    output: 'wlr-output-management-unstable-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  output_management_protocol_code = custom_target(
    'wlr-output-management-unstable-v1-protocol.c',
    input: output_management_xml,
    output: 'wlr-output-management-unstable-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
  executable('shady-shell',
    [
      layer_shell_protocol_header,
      layer_shell_protocol_code,
      xdg_shell_protocol_code,
      shady_shell_client_header,
      shady_shell_protocol_code,
      'shell/src/main.c',
      'shell/src/core.c',
      'shell/src/render_gl.c',
      'shell/src/render_shm.c',
      'shell/src/theme.c',
      'shell/src/apps.c',
      'shell/src/ui.c',
      'shell/src/script.c',
      'shell/src/plugins.c',
      'shell/src/compositor.c',
      'compositor/src/ipc/json.c',
    ],
    include_directories: include_directories('.', 'include', 'compositor/src'),
    c_args: [
      '-DSHELL_HAS_GL=@0@'.format(shell_gl_deps_found ? 1 : 0),
      '-DSHADY_SHELL_DATA_DIR="@0@"'.format(join_paths(shady_data_root, 'shell')),
      '-DSHADY_SHELL_PLUGIN_DIR="@0@"'.format(meson.current_build_dir()),
      '-DSHADY_SHELL_PLUGIN_INSTALL_DIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libdir'), 'shady', 'shell-plugins')),
    ],
    dependencies: [wayland_client, cairo, pangocairo, shell_xkbcommon, lua, dl, cc.find_library('m')] + shell_gl_deps,
    install: true)

  # Native shell plugins (include/shady/shell_plugin.h).
  shared_module('shady-shell-plugin-sysinfo',
    'shell/plugins/sysinfo.c',
    include_directories: include_directories('include'),
    dependencies: [cairo],
    install: true,
    install_dir: join_paths(get_option('libdir'), 'shady', 'shell-plugins'))

  # Logs people in through greetd for the Shady greeter (greeter/shell.lua).
  shared_module('shady-shell-plugin-greetd',
    ['greeter/src/greetd.c', 'compositor/src/ipc/json.c'],
    include_directories: include_directories('compositor/src', 'include'),
    dependencies: [cairo],
    install: true,
    install_dir: join_paths(get_option('libdir'), 'shady', 'shell-plugins'))

  shared_module('shady-shell-plugin-probe',
    'tests/shell-plugin-probe.c',
    include_directories: include_directories('include'),
    dependencies: [cairo],
    install: false)

  # Client bindings for the standard window and workspace protocols, used
  # by protocol-probe (tests/headless-standard-protocols.sh).
  standard_protocol_xmls = {
    'wlr-foreign-toplevel-management-unstable-v1':
      join_paths(wlr_protocols_dir, 'unstable/wlr-foreign-toplevel-management-unstable-v1.xml'),
    'ext-foreign-toplevel-list-v1':
      join_paths(wayland_protocols_dir, 'staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml'),
    'ext-workspace-v1':
      join_paths(wayland_protocols_dir, 'staging/ext-workspace/ext-workspace-v1.xml'),
  }
  standard_protocol_client_sources = []
  foreach name, xml : standard_protocol_xmls
    standard_protocol_client_sources += custom_target(name + '-client-protocol.h',
      input: xml, output: name + '-client-protocol.h',
      command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
    standard_protocol_client_sources += custom_target(name + '-client-protocol.c',
      input: xml, output: name + '-client-protocol.c',
      command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
  endforeach
  executable('protocol-probe',
    ['tests/protocol-probe.c'] + standard_protocol_client_sources,
    dependencies: [wayland_client],
    install: false)

  executable('headless-automation-probe',
    [
      xdg_shell_client_header,
      xdg_shell_protocol_code,
      'tests/headless-automation-probe.c',
    ],
    include_directories: include_directories('.'),
    dependencies: [wayland_client],
    build_by_default: false,
    install: false)

  executable('headless-session-lock-probe',
    [
      ext_session_lock_client_header,
      ext_session_lock_protocol_code,
      output_management_client_header,
      output_management_protocol_code,
      'tests/headless-session-lock-probe.c',
    ],
    include_directories: include_directories('.'),
    dependencies: [wayland_client],
    build_by_default: false,
    install: false)
endif

motion_plugin = []
if window_motion_enabled
  motion_plugin = shared_module('shady-plugin-window-motion',
    'plugins/window_motion.c',
    include_directories: include_directories('include'),
    dependencies: [cc.find_library('m', required: true)],
    install: true,
    install_dir: join_paths(get_option('libdir'), 'shady', 'plugins'))
endif

# Environment asset loaders live outside the core; see docs/ENVIRONMENT_API.md.
obj_loader_plugin = []
if obj_loader_enabled
  obj_loader_plugin = shared_module('shady-plugin-obj-loader',
    'plugins/loaders/obj/obj_loader.c',
    include_directories: include_directories('include'),
    dependencies: [cc.find_library('m', required: true)],
    install: true,
    install_dir: join_paths(get_option('libdir'), 'shady', 'plugins'))
endif

executable('shady',
  shady_sources,
  link_depends: [motion_plugin, obj_loader_plugin],
  c_args: [
    '-DWLR_USE_UNSTABLE',
    '-D_POSIX_C_SOURCE=200809L',
    '-DSHADY_PLUGIN_DIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libdir'), 'shady', 'plugins')),
    '-DSHADY_SHADER_DIR="@0@"'.format(shader_dir),
    '-DSHADY_HAS_SPATIAL=@0@'.format(spatial_enabled ? 1 : 0),
    '-DSHADY_HAS_LUA=@0@'.format(lua_enabled ? 1 : 0),
    '-DSHADY_HAS_PHYSICS=@0@'.format(physics_enabled ? 1 : 0),
    '-DSHADY_HAS_FPS=@0@'.format(fps_enabled ? 1 : 0),
    '-DSHADY_HAS_WINDOW_MOTION=@0@'.format(window_motion_enabled ? 1 : 0),
    '-DSHADY_HAS_OBJ_LOADER=@0@'.format(obj_loader_enabled ? 1 : 0),
    '-DSHADY_HAS_CLOSE_ANIMATION=@0@'.format(close_animation_enabled ? 1 : 0),
    '-DSHADY_HAS_SCENE_EFFECTS=@0@'.format(scene_effects_enabled ? 1 : 0),
  ],
  include_directories: include_directories('compositor/src', 'include', 'compositor/protocols'),
  dependencies: shady_deps,
  install: true)

executable('shadyctl',
  ['compositor/ctl/shadyctl.c', 'compositor/src/ipc/json.c'],
  c_args: ['-D_POSIX_C_SOURCE=200809L'],
  install: true)

ipc_json_test = executable('ipc-json-test',
  ['tests/ipc-json.c', 'compositor/src/ipc/json.c'],
  install: false)
test('ipc-json', ipc_json_test)

install_headers(
  'include/shady/module.h',
  'include/shady/plugin.h',
  'include/shady/motion.h',
  'include/shady/types.h',
  'include/shady/representation.h',
  'include/shady/event.h',
  'include/shady/environment.h',
  'include/shady/shell_plugin.h',
  subdir: 'shady')

shared_module('shady-plugin-hello',
  'examples/plugins/hello.c',
  include_directories: include_directories('include'),
  build_by_default: false,
  install: false)

shared_module('shady-plugin-publish-probe',
  'tests/publish-plugin.c',
  include_directories: include_directories('include'),
  install: false)

shared_module('shady-plugin-counter',
  'examples/plugins/counter.c',
  include_directories: include_directories('include'),
  build_by_default: false,
  install: false)

shared_module('shady-plugin-counter-bad',
  'tests/plugin-counter-bad.c',
  include_directories: include_directories('include'),
  build_by_default: false,
  install: false)

shared_module('shady-plugin-orbit-layout',
  'examples/plugins/orbit_layout.c',
  include_directories: include_directories('include'),
  build_by_default: false,
  install: false)

shared_module('shady-plugin-fps-cube',
  'plugins/fps_cube.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-fps-squash',
  'plugins/fps_squash.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-fps-jelly',
  'plugins/fps_jelly.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-fps-folded-paper',
  'plugins/fps_folded_paper.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-fps-origami',
  'plugins/fps_origami.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-magnetic-windows',
  'plugins/magnetic_windows.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon')],
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-astral-loom',
  'plugins/astral_loom.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon'), cc.find_library('m')],
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-afterglow',
  'plugins/afterglow.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon'), cc.find_library('m')],
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-window-constellation',
  'plugins/window_constellation.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon')],
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-window-portal',
  'plugins/window_portal.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon')],
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-focus-depth',
  'plugins/focus_depth.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-spatial-overview',
  'plugins/spatial_overview.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon')],
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-water-windows',
  'plugins/water_windows.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon')],
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-border-accent',
  'plugins/border_accent.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon')],
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-close-slide-fade',
  'plugins/close_slide_fade.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-close-burn',
  'plugins/close_burn.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-black-hole',
  'plugins/black_hole.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon'), cc.find_library('m')],
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-frozen-window',
  'plugins/frozen_window.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon')],
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

shared_module('shady-plugin-shader-overlay',
  'plugins/shader_overlay.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: true,
  install_dir: plugin_install_dir)

module_resolver_test = executable('module-resolver-test',
  [
    'tests/module-resolver.c',
    'compositor/src/module/module.c',
    'compositor/src/event/event.c',
  ],
  c_args: [
    '-DWLR_USE_UNSTABLE',
    '-D_POSIX_C_SOURCE=200809L',
    '-DSHADY_HAS_SPATIAL=0',
    '-DSHADY_HAS_LUA=0',
    '-DSHADY_HAS_PHYSICS=0',
    '-DSHADY_HAS_FPS=0',
    '-DSHADY_HAS_WINDOW_MOTION=0',
    '-DSHADY_HAS_CLOSE_ANIMATION=0',
    '-DSHADY_HAS_SCENE_EFFECTS=0',
  ],
  include_directories: include_directories('compositor/src', 'include'),
  dependencies: [wlroots, dependency('wayland-server'), dependency('xkbcommon'), dl],
  install: false)

test('module-resolver', module_resolver_test)

math3d_test = executable('math3d-test',
  [
    'tests/math3d.c',
    'compositor/src/render/math3d.c',
  ],
  c_args: [
    '-D_POSIX_C_SOURCE=200809L',
  ],
  include_directories: include_directories('compositor/src'),
  dependencies: [cc.find_library('m', required: true)],
  install: false)

test('math3d', math3d_test)

physics_collision_test = executable('physics-collision-test',
  [
    'tests/physics-collision.c',
    'compositor/src/modules/physics/collision.c',
  ],
  c_args: [
    '-D_POSIX_C_SOURCE=200809L',
  ],
  include_directories: include_directories('compositor/src'),
  dependencies: [cc.find_library('m', required: true)],
  install: false)

test('physics-collision', physics_collision_test)

config_opacity_test = executable('config-opacity-test',
  [
    'tests/config-opacity.c',
    'compositor/src/config.c',
  ],
  c_args: [
    '-DWLR_USE_UNSTABLE',
    '-D_POSIX_C_SOURCE=200809L',
  ],
  include_directories: include_directories('compositor/src', 'include'),
  dependencies: [wlroots, dependency('wayland-server'), dependency('xkbcommon')],
  install: false)

test('config-opacity', config_opacity_test)

config_border_test = executable('config-border-test',
  [
    'tests/config-border.c',
    'compositor/src/config.c',
  ],
  c_args: [
    '-DWLR_USE_UNSTABLE',
    '-D_POSIX_C_SOURCE=200809L',
  ],
  include_directories: include_directories('compositor/src', 'include'),
  dependencies: [wlroots, dependency('wayland-server'), dependency('xkbcommon')],
  install: false)

test('config-border', config_border_test)

config_titlebar_test = executable('config-titlebar-test',
  [
    'tests/config-titlebar.c',
    'compositor/src/config.c',
  ],
  c_args: [
    '-DWLR_USE_UNSTABLE',
    '-D_POSIX_C_SOURCE=200809L',
  ],
  include_directories: include_directories('compositor/src', 'include'),
  dependencies: [wlroots, dependency('wayland-server'), dependency('xkbcommon')],
  install: false)

test('config-titlebar', config_titlebar_test)

if window_motion_enabled
  motion_plugin_test = executable('motion-plugin-test',
    'tests/motion-plugin.c',
    include_directories: include_directories('include'),
    dependencies: [dl],
    install: false)
  test('motion-plugin', motion_plugin_test,
    args: [motion_plugin.full_path()], depends: [motion_plugin])
endif

shared_module('shady-plugin-motion-contract',
  'tests/motion-contract.c',
  include_directories: include_directories('include'),
  build_by_default: false,
  install: false)

if obj_loader_enabled
  obj_loader_test = executable('obj-loader-test',
    'tests/obj-loader.c',
    include_directories: include_directories('include'),
    dependencies: [dl, cc.find_library('m', required: true)],
    install: false)
  test('obj-loader', obj_loader_test,
    args: [obj_loader_plugin.full_path(),
      join_paths(meson.project_source_root(), 'assets', 'test-room.obj'),
      meson.current_build_dir()],
    depends: [obj_loader_plugin])
endif

black_hole_plugin_test = executable('black-hole-plugin-test',
  'tests/black-hole-plugin.c',
  include_directories: include_directories('include'),
  dependencies: [dl, dependency('xkbcommon'), cc.find_library('m', required: true)],
  install: false)
test('black-hole-plugin', black_hole_plugin_test,
  args: [join_paths(meson.current_build_dir(), 'libshady-plugin-black-hole.so')])

frozen_window_plugin_test = executable('frozen-window-plugin-test',
  'tests/frozen-window-plugin.c',
  include_directories: include_directories('include'),
  dependencies: [dl, dependency('xkbcommon'), cc.find_library('m', required: true)],
  install: false)
test('frozen-window-plugin', frozen_window_plugin_test,
  args: [join_paths(meson.current_build_dir(), 'libshady-plugin-frozen-window.so')])

# ---- installed data and session ------------------------------------------

install_subdir('compositor/shaders', install_dir: join_paths(shady_data_install_dir, 'compositor'))
# The Lua UIs and their data; their C sources stay behind.
install_subdir('shell', install_dir: shady_data_install_dir,
  exclude_directories: ['src', 'plugins'])
install_subdir('greeter', install_dir: shady_data_install_dir,
  exclude_directories: ['src'])
install_subdir('plugins/shaders', install_dir: join_paths(shady_data_install_dir, 'plugins'))
foreach rice : ['afterglow', 'neon-transit']
  rice_files = []
  foreach file : ['config.lua', 'init.lua', 'shell.lua', 'theme.sh', 'README.md']
    if import('fs').is_file(join_paths('rices', rice, file))
      rice_files += join_paths('rices', rice, file)
    endif
  endforeach
  install_data(rice_files,
    install_dir: join_paths(shady_data_install_dir, 'rices', rice))
endforeach
# Rice configs load plugins from $SHADY_ROOT/build, as they do in a checkout.
install_symlink('build',
  pointing_to: join_paths('..', '..', get_option('libdir'), 'shady', 'plugins'),
  install_dir: shady_data_install_dir)

session_conf = configuration_data()
session_conf.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))
session_conf.set('datadir', join_paths(get_option('prefix'), shady_data_install_dir))
configure_file(input: 'data/shady-session.in', output: 'shady-session',
  configuration: session_conf,
  install: true, install_dir: get_option('bindir'), install_mode: 'rwxr-xr-x')
configure_file(input: 'data/shady-greeter.in', output: 'shady-greeter',
  configuration: session_conf,
  install: true, install_dir: get_option('bindir'), install_mode: 'rwxr-xr-x')
foreach session : ['shady', 'shady-safe']
  configure_file(input: 'data/@0@.desktop.in'.format(session),
    output: '@0@.desktop'.format(session),
    configuration: session_conf,
    install: true,
    install_dir: join_paths(get_option('datadir'), 'wayland-sessions'))
endforeach
