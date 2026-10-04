# Shell design

Status: phases 1 (IPC) and 2 (shell core and render) implemented. This
document describes where `shady-shell` is going and why. API references for
finished pieces live in their own documents ([IPC API](IPC_API.md)).

## Problem

The compositor is easy to rice: Lua configures and scripts it, native C
plugins extend it, and GLSL shaders restyle windows and the scene. The shell
is not. `src/shell/main.c` is one ~2000-line C file with a fixed bar
(workspaces, tasks, clock), a fixed launcher, a context menu and a short
Quick Settings panel. Its only customisation is seven `SHADY_SHELL_*`
colour variables. It ignores `wl_output`, so it has no multi-monitor or
HiDPI support, and its compositor state comes from the private
`shady-shell-v1` protocol, which no other program can use.

Rices are limited by the shell, not by the compositor.

## Goal

Give the shell the same extension model as the compositor:

| | Compositor | Shell |
|---|---|---|
| Configuration and scripting | Lua | Lua |
| Native extensions | C plugin ABI (`SHADY_API_HAS`) | C plugin ABI, same conventions |
| Visual effects | GLSL, host-managed programs | GLSL, host-managed programs, same uniform names |
| Hot reload | `shady.plugins.reload` | config and plugin reload without restarting the session |

A rice author learns one model and uses it for both halves of the desktop.

## Architecture

The shell stays a separate process. A shell bug, a slow Lua loop or a
crashing shell plugin must never take the compositor down, and the shell
must be restartable on its own.

```
compositor ──IPC socket──▶ shady-shell
  │                         ├─ core     Wayland, one layer surface per output, EGL, poll loop
  │                         ├─ render   per-node cairo textures, GL composite, shader effects
  │                         ├─ ui       widget tree, flex layout, input, animation, signals
  │                         ├─ lua      config, widget definitions, hot reload
  │                         ├─ plugin   C ABI: services, custom widgets, shaders
  │                         └─ ipc      client of the compositor socket
  └─ layer-surface effects  backdrop shaders and 3D placement for any layer client
```

### Content in cairo, composition in GL

Text and icons are drawn with cairo and pango into one texture per widget;
only widgets whose content changed are redrawn. Composition, animation and
effects run in GLES2 through EGL. A widget shader receives its own content
as `u_tex`, exactly like a compositor window shader, so effects can bend,
tint or animate any widget.

### No raw GL for plugins

As in the compositor, shell plugins and Lua never see GL handles. They
create programs, set uniforms, provide textures and request draws; the host
owns GL state and frees everything on reload. Uniform names follow
[SHADER_API.md](SHADER_API.md): `u_tex`, `u_time`, `u_resolution`,
`u_params`, plus shell-specific `u_widget_size` and `u_hover`.

### Backdrop effects belong to the compositor

A client cannot see what is behind its surface, so glass refraction,
backdrop blur and sky reflections cannot be done in the shell. The
compositor will expose the same per-window shader machinery for layer
surfaces (render hook + `u_scene` sampling + 3D placement). This works for
any layer-shell client, so a bar made with another toolkit also becomes
part of the 3D scene.

Shared look without backdrop access comes from IPC: plugins publish values
(for example afterglow's hour and sky palette) and shell shaders receive
them as uniforms.

## Lua sketch

```lua
local shell = require("shell")

shell.bar {
  output = "*", edge = "top", height = 36,
  effect = shell.shader("shaders/aurora.frag", { strength = 0.6 }),
  shell.row { gap = 8, padding = { 0, 10 },
    shell.workspaces {},
    shell.tasks { max_width = 220 },
    shell.spacer(),
    shell.text { text = shell.poll("date +%H:%M", 1000) },
    shell.text { text = shell.ipc.focused:map(function(w) return w and w.title or "" end) },
  },
}
```

- Widgets: `box`, `row`, `column`, `text`, `image`, `button`, `slider`,
  `spacer`, plus built-ins (`workspaces`, `tasks`, `clock`).
- Signals: values that redraw their bound widgets when they change.
  `shell.ipc.*` signals mirror the compositor; `shell.poll(cmd, ms)` and
  `shell.listen(cmd)` turn command output into signals (eww's
  `defpoll`/`deflisten`), which covers most widgets before any native
  service exists.
- Popups (launcher, menus, Quick Settings) are separate layer surfaces
  declared the same way.

## Phases

1. **Compositor IPC** — done. Unix socket with JSON lines: queries,
   commands and event subscriptions, plus the `shadyctl` CLI. Useful on its
   own to scripts and other bars. See [IPC_API.md](IPC_API.md).
2. **Shell core and render** — done. One bar per output that follows
   hotplug and scale changes, integer HiDPI scaling, a poll loop with
   timers and fd watches, repaints coalesced on frame callbacks, and two
   renderers: GLES2 composition through EGL (default) and a `wl_shm`
   fallback. The UI was split out of the old single `main.c` without
   changing its behaviour; see "Code layout" below.
3. **UI and Lua** — widget tree, layout, signals, hot reload. Rebuild the
   current bar, launcher, context menu and Quick Settings in Lua; the
   existing `headless-shell-*` tests must keep passing.
4. **Shader effects** in the shell.
5. **Shell C plugin ABI** for services that Lua cannot reach well (audio,
   tray, notifications).
6. **Layer-surface effects** in the compositor.
7. **Standard protocols** (`wlr-foreign-toplevel-management`,
   `ext-workspace-v1`) so third-party bars work too.

## Open questions

- Where IPC values published by plugins live: a new append-only plugin API
  entry (`ipc_publish(key, json)`) is the current plan.
- Whether the shell embeds the same Lua version and sandbox as the
  compositor, or a separate `shell.lua` loaded from
  `~/.config/shady/shell.lua`.
- Font and icon theme discovery (fontconfig is already a dependency; icon
  themes are not).

## Code layout

```
src/shell/
  core.c/.h        Wayland globals, outputs, seat input routing, layer
                   surfaces, timers, fd watches, the poll loop
  render.h         renderer interface: begin() gives a cairo image, end() commits
  render_gl.c      EGL + GLES2: upload the image, draw it with a shader, swap
  render_shm.c     double-buffered wl_shm fallback
  theme.c/.h       palette (SHADY_SHELL_* colours) and drawing primitives
  shell.h          the UI's model and the functions below
  main.c           shady-shell-v1 model, one bar per output, the clock timer
  bar.c launcher.c menu.c quick.c
                   one surface each: draw(), pointer and key handlers
  apps.c           .desktop index for the launcher
```

A surface is a `shell_surface` with a handler table. `shell_surface_redraw()`
only marks it dirty; the loop paints dirty surfaces once per iteration and
not faster than the compositor's frame callbacks, so a burst of protocol
events costs one repaint. `draw()` receives a cairo context already scaled
to the surface's buffer scale, so all UI code works in logical pixels.

The compositor side gained one fix here: layer surfaces are re-configured
whenever the output layout changes, so any layer-shell bar (not only this
one) follows mode and scale changes.
