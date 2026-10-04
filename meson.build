project('shady', 'c', version: '0.1.0',
  default_options: ['buildtype=debugoptimized', 'c_std=c11', 'warning_level=2'])

wlroots = dependency('wlroots-0.20', version: ['>=0.20.2', '<0.21'])
wlr_protocols = dependency('wlr-protocols')
wayland_scanner = find_program('wayland-scanner')
wlr_protocols_dir = wlr_protocols.get_variable(pkgconfig: 'pkgdatadir')
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

shady_shell_protocol_xml = files('protocols/shady-shell-v1.xml')
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
shader_dir = join_paths(meson.project_source_root(), 'shaders')

shady_sources = [
  layer_shell_protocol_header,
  'src/main.c',
  'src/module/module.c',
  'src/event/event.c',
  'src/module/builtin.c',
  'src/plugin/plugin.c',
  'src/plugin/manager.c',
  'src/plugin/manager_lua.c',
  'src/plugin/host_api.c',
  'src/plugin/motion.c',
  'src/plugin/representation.c',
  'src/config.c',
  'src/config_lua.c',
  'src/titlebar.c',
  'src/input.c',
  'src/xdg.c',
  'src/modules/window_motion/window_motion.c',
  'src/layer.c',
  'src/output.c',
  'src/output_management.c',
  'src/session_lock.c',
  'src/shell_protocol.c',
  shady_shell_server_header,
  shady_shell_protocol_code,
  'src/modules/desktop/module.c',
  'src/modules/desktop/ime.c',
  'src/modules/workspace/workspace.c',
  'src/modules/workspace/module.c',
]

if spatial_enabled
  shady_sources += [
    'src/render/render.c',
    'src/render/gl_pipeline.c',
    'src/render/math3d.c',
    'src/render/pick3d.c',
    'src/experimental/runtime.c',
    'src/modules/environment/environment.c',
    'src/modules/environment/loaders.c',
    'src/modules/environment/scene.c',
    'src/modules/environment/draw.c',
    'src/modules/environment/sky.c',
    'src/modules/spatial/module.c',
    'src/plugin/environment.c',
  ]
else
  shady_sources += 'src/render/desktop.c'
endif

if lua_enabled
  shady_sources += [
    'src/modules/lua/lua.c',
    'src/modules/lua/module.c',
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
    'src/modules/physics/physics.c',
    'src/modules/physics/collision.c',
    'src/modules/physics/module.c',
  ]
endif
if fps_enabled
  shady_sources += [
    'src/modules/fps/fps.c',
    'src/modules/fps/module.c',
  ]
endif
if close_animation_enabled
  shady_sources += [
    'src/modules/close_animation/close_animation.c',
    'src/modules/close_animation/module.c',
  ]
endif
if scene_effects_enabled
  shady_sources += [
    'src/modules/scene_effects/scene_effects.c',
    'src/modules/scene_effects/module.c',
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
      'src/shell/main.c',
    ],
    include_directories: include_directories('.'),
    dependencies: [wayland_client, cairo, pangocairo, shell_xkbcommon],
    install: true)

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
    'examples/plugins/window_motion.c',
    include_directories: include_directories('include'),
    dependencies: [cc.find_library('m', required: true)],
    install: true,
    install_dir: join_paths(get_option('libdir'), 'shady', 'plugins'))
endif

# Environment asset loaders live outside the core; see docs/ENVIRONMENT_API.md.
obj_loader_plugin = []
if obj_loader_enabled
  obj_loader_plugin = shared_module('shady-plugin-obj-loader',
    'loaders/obj/obj_loader.c',
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
  include_directories: include_directories('src', 'include'),
  dependencies: shady_deps,
  install: true)

install_headers(
  'include/shady/module.h',
  'include/shady/plugin.h',
  'include/shady/motion.h',
  'include/shady/types.h',
  'include/shady/representation.h',
  'include/shady/event.h',
  'include/shady/environment.h',
  subdir: 'shady')

shared_module('shady-plugin-hello',
  'examples/plugins/hello.c',
  include_directories: include_directories('include'),
  build_by_default: false,
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
  'examples/plugins/fps_cube.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: false)

shared_module('shady-plugin-fps-squash',
  'examples/plugins/fps_squash.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: false)

shared_module('shady-plugin-fps-jelly',
  'examples/plugins/fps_jelly.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: false)

shared_module('shady-plugin-fps-folded-paper',
  'examples/plugins/fps_folded_paper.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: false)

shared_module('shady-plugin-fps-origami',
  'examples/plugins/fps_origami.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: false)

shared_module('shady-plugin-magnetic-windows',
  'examples/plugins/magnetic_windows.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon')],
  build_by_default: true,
  install: false)

shared_module('shady-plugin-astral-loom',
  'examples/plugins/astral_loom.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon'), cc.find_library('m')],
  build_by_default: true,
  install: false)

shared_module('shady-plugin-afterglow',
  'examples/plugins/afterglow.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon'), cc.find_library('m')],
  build_by_default: true,
  install: false)

shared_module('shady-plugin-window-constellation',
  'examples/plugins/window_constellation.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon')],
  build_by_default: true,
  install: false)

shared_module('shady-plugin-window-portal',
  'examples/plugins/window_portal.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon')],
  build_by_default: true,
  install: false)

shared_module('shady-plugin-focus-depth',
  'examples/plugins/focus_depth.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: false)

shared_module('shady-plugin-spatial-overview',
  'examples/plugins/spatial_overview.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon')],
  build_by_default: true,
  install: false)

shared_module('shady-plugin-water-windows',
  'examples/plugins/water_windows.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon')],
  build_by_default: true,
  install: false)

shared_module('shady-plugin-border-accent',
  'examples/plugins/border_accent.c',
  include_directories: include_directories('include'),
  dependencies: [dependency('xkbcommon')],
  build_by_default: true,
  install: false)

shared_module('shady-plugin-close-slide-fade',
  'examples/plugins/close_slide_fade.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: false)

shared_module('shady-plugin-close-burn',
  'examples/plugins/close_burn.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: false)

shared_module('shady-plugin-shader-overlay',
  'examples/plugins/shader_overlay.c',
  include_directories: include_directories('include'),
  build_by_default: true,
  install: false)

module_resolver_test = executable('module-resolver-test',
  [
    'tests/module-resolver.c',
    'src/module/module.c',
    'src/event/event.c',
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
  include_directories: include_directories('src', 'include'),
  dependencies: [wlroots, dependency('wayland-server'), dependency('xkbcommon'), dl],
  install: false)

test('module-resolver', module_resolver_test)

math3d_test = executable('math3d-test',
  [
    'tests/math3d.c',
    'src/render/math3d.c',
  ],
  c_args: [
    '-D_POSIX_C_SOURCE=200809L',
  ],
  include_directories: include_directories('src'),
  dependencies: [cc.find_library('m', required: true)],
  install: false)

test('math3d', math3d_test)

physics_collision_test = executable('physics-collision-test',
  [
    'tests/physics-collision.c',
    'src/modules/physics/collision.c',
  ],
  c_args: [
    '-D_POSIX_C_SOURCE=200809L',
  ],
  include_directories: include_directories('src'),
  dependencies: [cc.find_library('m', required: true)],
  install: false)

test('physics-collision', physics_collision_test)

config_opacity_test = executable('config-opacity-test',
  [
    'tests/config-opacity.c',
    'src/config.c',
  ],
  c_args: [
    '-DWLR_USE_UNSTABLE',
    '-D_POSIX_C_SOURCE=200809L',
  ],
  include_directories: include_directories('src', 'include'),
  dependencies: [wlroots, dependency('wayland-server'), dependency('xkbcommon')],
  install: false)

test('config-opacity', config_opacity_test)

config_border_test = executable('config-border-test',
  [
    'tests/config-border.c',
    'src/config.c',
  ],
  c_args: [
    '-DWLR_USE_UNSTABLE',
    '-D_POSIX_C_SOURCE=200809L',
  ],
  include_directories: include_directories('src', 'include'),
  dependencies: [wlroots, dependency('wayland-server'), dependency('xkbcommon')],
  install: false)

test('config-border', config_border_test)

config_titlebar_test = executable('config-titlebar-test',
  [
    'tests/config-titlebar.c',
    'src/config.c',
  ],
  c_args: [
    '-DWLR_USE_UNSTABLE',
    '-D_POSIX_C_SOURCE=200809L',
  ],
  include_directories: include_directories('src', 'include'),
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
