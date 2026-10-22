# Changelog

Shady is experimental: the Lua, IPC and plugin interfaces may still change
between releases. Breaking changes are called out here.

## Unreleased

- `rewind`, a new plugin loaded by Afterglow and Horizon: every layout the
  desktop settles into (positions, depth, workspaces, maximized and
  fullscreen) is kept as a keyframe. `Super+BackSpace` opens a timeline and
  slides the windows back through them; windows that did not exist yet
  drift up out of view and the world fades toward an old photograph. Return
  restores the moment, Escape returns to now, and a restore can itself be
  rewound. `rewind.*` published values let a shell show where you are.
- The NixOS VM tests are gone; CI builds the package with its unit tests.

## 0.1.0 (2026-10-22)

The first release, five weeks after the first commit. Everything below is
new.

### Compositor

- A wlroots 0.20.2 compositor with two render paths: a custom GLES2
  renderer that places windows in a perspective 3D world, and the plain
  wlroots scene graph (`--safe`). Both redraw only when something changes.
- Window physics: gravity, wobble and tilt, landing on the floor, collision
  with authored OBJ environments through triangle SAT tests, and automatic
  respawn of windows that leave the world.
- First-person mode: walk, look and jump through the world, pick windows up,
  carry them and throw them.
- A camera you orbit, pan and zoom, 3D picking of whole window shells, a
  sky, a floor grid, shadows, rounded frames, title bars and close
  animations.
- Workspaces, window rules, maximize and fullscreen, layer shell, input
  methods (`text-input-v3`, `input-method-v2`), fractional scaling, xdg
  activation, cursor shapes, session lock, screencopy, output management and
  the foreign-toplevel and `ext-workspace-v1` protocols for other bars.
- **Shortcut overlay:** `Super+Shift+/` lists every shortcut of the session,
  the compositor's, the rice's and each plugin's, drawn by the shell.

### Extending it

- Lua 5.4 in two phases: `config.lua` before startup (settings, modules,
  plugins, key names) and `init.lua` at runtime (bindings, rules, events,
  live window objects, headless UI automation).
- A public C plugin API with versioned ABIs, hot reload with state
  migration (ABI v2), plugin-defined window bodies, per-window shaders,
  render hooks, scene post-processing, environment loaders and published
  values.
- An IPC socket speaking JSON lines, with the `shadyctl` CLI: query windows,
  workspaces, plugins, values and shortcuts, send commands, press keys and
  follow events.
- Plugins: spatial overview, water windows, frozen window, black hole,
  magnetic windows, window constellation, window portal, border accent,
  focus depth, the Afterglow sky, five first-person window bodies and two
  close animations.

### shady-shell

- A separate layer-shell shell whose every surface is a hot-reloaded Lua
  config: bars, popups, a launcher, Quick Settings, a task menu, a
  notification server and the shortcut overlay.
- Widgets with flexbox-like layout, spring animation, GLSL effects, frosted
  glass and 3D placement through the compositor, input-method text entry
  and HiDPI rendering; native shell plugins for what Lua cannot reach.

### Rices, session and packaging

- Four rices: **Horizon** (the default), **Objects**, **Afterglow** and
  **Neon Transit**.
- A greetd greeter built from the shell, a `shady-session` launcher and
  wayland-sessions entries.
- A Nix flake with the package, `nix run` to try it, a NixOS module
  (`programs.shady`) and VM tests that log in and check the session and the
  greeter.
- ASan/UBSan regression suites for the 3D and 2D paths and unit tests.
