# wlroots' wlr_ext_workspace_v1.h includes <wayland-protocols/ext-workspace-v1-enum.h>
# but does not install it; generate it here so that include path resolves
# through include_directories('protocols').
ext_workspace_enum_header = custom_target(
  'ext-workspace-v1-enum.h',
  input: join_paths(wayland_protocols_server_dir, 'staging/ext-workspace/ext-workspace-v1.xml'),
  output: 'ext-workspace-v1-enum.h',
  command: [wayland_scanner, 'enum-header', '@INPUT@', '@OUTPUT@'],
)
