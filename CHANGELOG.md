# Changelog

Shady is experimental: the Lua, IPC and plugin interfaces may still change
between releases. Breaking changes are called out here.

## Unreleased

Fixes from testing on hardware:

- Clicks work after dragging or resizing a window by its own decorations
  (Brave's tab strip). The compositor swallowed the button release that
  ended the grab, so the seat kept the button pressed and dropped every
  later click on it. A client that started a grab now gets the release.
- Closing a window that had refused to close once (Ghostty asking for
  confirmation after `Super+Q`) no longer leaves a frozen picture of it on
  screen. The close-animation module advanced that picture through its own
  copy of the renderer's snapshot struct, which had fallen out of step
  when the viewport crop was added; both now share one definition.
- The window you raised gets the clicks where windows overlap. Picking
  chose the nearest hit, and a window leaning a fraction of a degree
  (wobble, a drag settling) could put its far half behind the window it
  covers. Windows in one plane are now ordered by stacking, as drawn.
- Window edges can be grabbed over a shell's desktop: in Objects the
  background layer took the press just outside every window.
- `shady.automation.button_down`/`button_up` for drags that wait for the
  client between steps.

Everyday use, measured on a 14" 2880x1800 laptop (Intel Lunar Lake):

- Windows can be resized. In spatial mode clicks were only picked inside a
  window's geometry, so the resize borders that GTK, Ghostty and Chromium
  keep in their shadow were out of reach, and windows with Shady's title
  bar had no edges at all. Every window now has a 10px grab border (the
  pointer turns into a resize arrow there, corners take both edges),
  `Super`+drag moves the window under the pointer and `Super`+right-drag
  resizes it from the nearest corner, as in niri and sway. A resize from
  the left or top keeps the opposite edge still instead of letting it
  jitter while the client catches up, and clients are told they are being
  resized.
- One title bar per window. Ghostty and other GTK applications do not
  speak xdg-decoration and got Shady's title bar on top of their own.
  Shady now offers KDE's server-decoration protocol, which they use to ask
  for Shady's bar (Ghostty drops its own), and a client that uses neither
  protocol is taken to draw its own decorations, as the spec says.
- The sky is drawn once and reused. Afterglow's sky and sea filled the
  whole panel with an expensive shader on every frame, also when only a
  terminal had changed, so a client drawing at 60 fps kept the GPU about
  60% busy. It is now kept in a texture per output and
  redrawn only when the camera, the palette, the output size or the sky
  plugin changes: a Horizon frame went from 10.0 to 4.3 ms of GPU time.
- The pointer is no longer tiny. On a system without a cursor theme
  wlroots drew a built-in 24px arrow at half size on a scale 2 panel.
  Shady now builds with Adwaita as a fallback theme, honours
  `XCURSOR_THEME`/`XCURSOR_SIZE` or the new `cursor_theme`/`cursor_size`
  settings, and exports them so clients match.
- Output scales are guessed the way niri and GNOME do (135 DPI on laptop
  panels, 110 on monitors), so a 14" 2880x1800 panel gets 1.75 instead of
  2 and has as much room as in niri.
- Touchpads tap to click (`touchpad_tap`, on by default) and can scroll
  naturally (`touchpad_natural_scroll`).
- xdg-shell v5: clients learn how much room there is (configure_bounds)
  and which window states Shady supports.

- `rewind`, a new plugin loaded by Afterglow and Horizon: every layout the
  desktop settles into (positions, depth, workspaces, maximized and
  fullscreen) is kept as a keyframe. `Super+BackSpace` opens a timeline and
  slides the windows back through them; windows that did not exist yet
  drift up out of view and the world fades toward an old photograph. Return
  restores the moment, Escape returns to now, and a restore can itself be
  rewound. `rewind.*` published values let a shell show where you are.
- The NixOS VM tests are gone; CI builds the package with its unit tests.
- The 3D renderer is idle again when nothing changes. wlroots sends a frame
  event after every commit, and Shady redrew on each one, so the desktop
  was redrawn at the refresh rate forever. It now draws only when asked: a
  client commits (any surface, popups and subsurfaces included), something
  moves, or wlroots needs a frame. Representation providers (the
  `fps-*` window bodies) no longer keep it awake outside first-person
  mode, where they are not drawn. An idle Horizon desktop went from 60
  frames a second to a handful.
- Afterglow's sky skips cloud noise where it would be multiplied by zero:
  the same picture for less GPU time on every frame.

Spatial mode is easier to work in with ordinary applications (a browser
was the test):

- The focused window shows exactly what its client drew. It used to be
  tinted blue, brightened, left translucent and run through the glass
  effect, which put colour fringes on text and the sky behind web pages.
  The rice's glass now shades only the windows behind it;
  `window_focused_exact = false` brings the old look back.
- Menus and other popups appear where the application put them, follow
  their window and take clicks. They were drawn offset by the window's
  position, and clicks fell through to the window underneath.
- Clients that draw their own decorations (Chromium, Brave, Firefox) ask
  for that through xdg-decoration and no longer get a second title bar
  from Shady. Windows are drawn to their xdg geometry, without the
  shadow margin such clients keep around themselves, and a buffer cropped
  with wp_viewporter (Chromium when maximized) is drawn cropped instead of
  squeezed.
- Windows are shown at their real size and square-on. Afterglow, Horizon
  and Neon Transit pulled the camera in and turned it, which enlarged every
  window about 1.4 times (too big for the screen) and skewed it.
- HiDPI: each output gets a scale from its panel's physical size (2 on a
  13.6" 2880x1800 laptop; `output_scale` or `SHADY_OUTPUT_SCALE` to choose).
  Shady used to leave every output at scale 1.
- New windows are told how much room there is (xdg configure_bounds), are
  asked to shrink when they open larger than the screen, and are placed
  with their title bar on screen. Placement, resizing and restoring used
  the surface instead of the window geometry, so client-decorated windows
  landed offset by their shadow and grew with each unmaximize.
- Maximized and fullscreen windows below a reserved area (Horizon's status
  line) were drawn mirrored to the other end of the screen, away from where
  they took clicks.
- Screenshots no longer need grim: `shady.screenshot()`, the IPC
  `screenshot` command and `shell.screenshot()` save the next frame as a
  PNG in `~/Pictures`. `Print` takes one, the control centre's Capture tile
  uses it, and a notification says where it went.
- The brightness slider works without brightnessctl: it reads the backlight
  from sysfs and sets it through systemd-logind.
- Horizon's status line reserves its strip at the top, so windows no longer
  open or maximize underneath it.
- `shell.notify()` and a `value` event for shady-shell configs.
- A dragged window leans a little and springs back exactly flat. The tilt
  used to stay wherever the drag left it, softening the text for good.
  Afterglow and Horizon no longer bend windows while they are dragged.

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
