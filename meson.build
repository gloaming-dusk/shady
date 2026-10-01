project('shady', 'c', version: '0.1.0',
  default_options: ['buildtype=debugoptimized', 'c_std=c11', 'warning_level=2'])

wlroots = dependency('wlroots-0.20', version: ['>=0.20.2', '<0.21'])
wlr_protocols = dependency('wlr-protocols')
wayland_scanner = find_program('wayland-scanner')
wlr_protocols_dir = wlr_protocols.get_variable(pkgconfig: 'pkgdatadir')
layer_shell_protocol_header = custom_target(
  'wlr-layer-shell-unstable-v1-protocol.h',
  input: join_paths(wlr_protocols_dir, 'unstable/wlr-layer-shell-unstable-v1.xml'),
  output: 'wlr-layer-shell-unstable-v1-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
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
  'src/config.c',
  'src/config_lua.c',
  'src/input.c',
  'src/xdg.c',
  'src/layer.c',
  'src/output.c',
  'src/output_management.c',
  'src/session_lock.c',
  'src/modules/desktop/module.c',
]

if spatial_enabled
  shady_sources += [
    'src/render/render.c',
    'src/render/gl_pipeline.c',
    'src/render/math3d.c',
    'src/render/pick3d.c',
    'src/experimental/runtime.c',
    'src/modules/environment/environment.c',
    'src/modules/spatial/module.c',
    'src/assets/mesh.c',
    'src/assets/obj.c',
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
close_animation_enabled = spatial_enabled and get_option('close_animation').enabled()
scene_effects_enabled = spatial_enabled and get_option('scene_effects').enabled()

if physics_enabled
  shady_sources += [
    'src/modules/physics/physics.c',
    'src/modules/physics/module.c',
  ]
endif
if fps_enabled
  shady_sources += [
    'src/modules/fps/fps.c',
    'src/modules/fps/module.c',
  ]
endif
if window_motion_enabled
  shady_sources += [
    'src/modules/window_motion/window_motion.c',
    'src/modules/window_motion/module.c',
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

shady_deps = [
  wlroots,
  dependency('wayland-server'),
  dependency('xkbcommon'),
  cc.find_library('m', required: true),
  lua,
  dl,
]
if spatial_enabled
  shady_deps += [egl, glesv2]
endif

executable('shady',
  shady_sources,
  c_args: [
    '-DWLR_USE_UNSTABLE',
    '-D_POSIX_C_SOURCE=200809L',
    '-DSHADY_SHADER_DIR="@0@"'.format(shader_dir),
    '-DSHADY_HAS_SPATIAL=@0@'.format(spatial_enabled ? 1 : 0),
    '-DSHADY_HAS_LUA=@0@'.format(lua_enabled ? 1 : 0),
    '-DSHADY_HAS_PHYSICS=@0@'.format(physics_enabled ? 1 : 0),
    '-DSHADY_HAS_FPS=@0@'.format(fps_enabled ? 1 : 0),
    '-DSHADY_HAS_WINDOW_MOTION=@0@'.format(window_motion_enabled ? 1 : 0),
    '-DSHADY_HAS_CLOSE_ANIMATION=@0@'.format(close_animation_enabled ? 1 : 0),
    '-DSHADY_HAS_SCENE_EFFECTS=@0@'.format(scene_effects_enabled ? 1 : 0),
  ],
  include_directories: include_directories('src', 'include'),
  dependencies: shady_deps,
  install: true)

install_headers(
  'include/shady/module.h',
  'include/shady/plugin.h',
  'include/shady/event.h',
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

shared_module('shady-plugin-orbit-layout',
  'examples/plugins/orbit_layout.c',
  include_directories: include_directories('include'),
  build_by_default: false,
  install: false)
