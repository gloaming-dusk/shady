# Desktop integration

What Shady offers ordinary Wayland clients, bars and shells, and how its own
shell is connected. The shell's Lua API is in
[SHELL_LUA_API.md](SHELL_LUA_API.md), the IPC socket in
[IPC_API.md](IPC_API.md).

## Two render paths

Spatial mode, the default, draws every window into a perspective 3D world
through Shady's GLES2 renderer. Safe mode (`--safe`, or the **Shady
(safe)** session) renders the wlroots scene graph directly with ordinary
scene-graph hit testing: no physics, first-person mode or effects. It is
the compatibility baseline. Both are demand-driven: an idle desktop does
not redraw.

The focused window is marked in both: a thin cyan ring in safe mode, a
cyan and brightness lift in spatial mode (rices restyle both).

## Protocols for clients

Besides xdg-shell and layer-shell, Shady implements:

- `text-input-v3` and `input-method-v2`, bridging focused text clients to
  one input method per seat, with preedit, commit, delete and keyboard
  grabs, so Hangul, Japanese and other input methods work;
- `xdg-activation-v1`, so a launched application can take focus with an
  activation token;
- `wp_fractional_scale_v1` with `wp_viewporter`, with preferred scales from
  output visibility;
- `wp_cursor_shape_v1`, for server-side cursor shapes;
- `ext-session-lock-v1`, screencopy and xdg-output (`grim` works), and the
  virtual keyboard (`wtype` works);
- output management (`wlr-randr`), data control (clipboard managers), idle
  notification, pointer constraints and relative pointer (games), and xdg
  decorations.

## Other bars and shells

Waybar, Quickshell, eww and the like see Shady's state through standard
protocols:

- `wlr-foreign-toplevel-management-unstable-v1` for task lists: titles,
  app ids, activated, maximized and fullscreen state, and activate, close,
  maximize and fullscreen requests;
- `ext-foreign-toplevel-list-v1` for the read-only window list;
- `ext-workspace-v1` for workspaces and switching.

Shady has no minimized state, so minimize requests are ignored. Workspaces
are created by Shady itself, so clients can activate them but not create,
assign or remove them. For anything else, the IPC socket and `shadyctl`
expose windows, workspaces, published values, shortcuts and events as JSON
lines.

## shady-shell

`shady-shell` is a separate Wayland client, not compositor UI. Everything it
shows comes from a Lua config: the default bar, Quick Settings, task menu,
launcher, notifications and shortcut overlay are `shell/default.lua`, and
each rice brings its own. Bars follow outputs as they come and go and draw
at the output's scale.

It talks to the compositor two ways:

- the `shady-shell-v1` Wayland protocol (`compositor/protocols/shady-shell-v1.xml`)
  for workspaces, windows and actions. Version 1 carries the workspace
  list, the active workspace and the focused window; version 2 adds stable
  window ids, window snapshots and activate/close; version 3 window state
  plus maximize, fullscreen and move-to-workspace; version 4 cycling
  windows and ending the session. `shady.toggle_launcher()` reaches the
  shell through it.
- the IPC socket for published values (a rice's palette, the sky's hour)
  and the shortcut overlay.

Launcher state, notifications, network and audio stay in the shell; the
compositor keeps no shell policy.

The default palette is Neon Transit's cyan. A rice sets its own through
`#RRGGBB` environment variables read at startup: `SHADY_SHELL_ACCENT`,
`SHADY_SHELL_ACCENT_2` (the badge gradient's end), `SHADY_SHELL_ACCENT_DEEP`
(selected fills), `SHADY_SHELL_SURFACE`, `SHADY_SHELL_TEXT`,
`SHADY_SHELL_TEXT_DIM` and `SHADY_SHELL_DANGER`; see
`rices/afterglow/theme.sh`. In spatial mode `shady.layer_effect` turns any
layer surface, Shady's or another client's, into frosted glass, and
`shady.layer_transform` places it in 3D ([LUA_API.md](LUA_API.md)).
