# Shell Lua API

`shady-shell` draws whatever its Lua config describes. The default UI (bar,
Quick Settings, task menu, launcher) is itself a config:
[`shell/default.lua`](../shell/default.lua). Read it alongside this page.

## Config file

The shell loads the first of:

1. `$SHADY_SHELL_CONFIG`
2. `$XDG_CONFIG_HOME/shady/shell.lua` (or `~/.config/shady/shell.lua`)
3. `shell/default.lua` from the data directory (`$SHADY_SHELL_DATA_DIR`
   overrides it)

If your config fails to load at startup, the shell logs the error and
falls back to the default. Edits are picked up live: saving any `.lua` file
in the config's directory reloads the config in a fresh Lua state. If the
new version fails, the error is logged and the running UI stays as it was.
`require` finds modules next to the config and in the data directory.

To keep the default UI and change a few things, start your config with:

```lua
dofile(shell.data_dir .. "/default.lua")
```

## Views

Every surface has a `view` function that returns a widget tree. The shell
calls it whenever the surface is repainted: when the compositor's state
changes, a polled value changes, the clock ticks, the pointer moves onto
something with an `id`, or you call `shell.redraw()`. Views should only
read state and build widgets; they may not open or close popups.

The first argument is a context table:

| Field | Meaning |
|---|---|
| `name` | the bar or popup name |
| `output` | output name (`"DP-1"`), if the surface is on a known output |
| `width`, `height` | surface size in logical pixels |
| `scale` | buffer scale |
| `hovered` | `id` of the widget under the pointer, if any |

Popups receive the table passed to `shell.open` as a second argument.

### `shell.bar{...}`

A bar on every output, or on the outputs listed.

```lua
shell.bar {
    name = "top",                 -- required, unique
    edge = "top",                 -- "top" or "bottom"
    size = 38,                    -- height in logical pixels
    exclusive = true,             -- reserve space so windows avoid it
    layer = "top",                -- background | bottom | top | overlay
    outputs = { "DP-1" },         -- default: all outputs
    view = function(ctx) return shell.row { ... } end,
}
```

### `shell.popup{...}`

A surface that is opened and closed on demand.

```lua
shell.popup {
    name = "quick",
    anchor = { "top", "right" },  -- edges; none centres it
    margin = { top = 42, right = 8 },
    width = 250,                  -- omit width/height to fit the content
    keyboard = "none",            -- none | exclusive | on_demand
    persistent = false,           -- keep it open, like a bar
    outputs = nil,                -- "all": persistent on every output, one each
    fill = false,                 -- cover the whole output (anchors and size ignored)
    exclusive = 0,                -- pixels to reserve along its anchored edge
    input = "all",                -- or "widgets": take the pointer only over interactive widgets
    text_input = false,           -- take text from input methods (Hangul, Japanese, ...)
    layer = "overlay",
    view = function(ctx, args) ... end,
    on_key = function(key, text, args) ... end,
}
```

With `text_input = true` (and a `keyboard` mode) the popup takes text from
the input method, through text-input-v3: what is still being composed is
`ctx.preedit` in the view, and committed text arrives in `on_key` with an
empty key name, like typed text. Mark the widget where text goes with
`caret = true`, so the input method places its candidate window next to it.

Without `width` or `height` the popup takes its content's natural size and
follows it as the content changes. `on_key` receives the xkb key name
(`"Escape"`, `"Return"`, `"Up"`, `"BackSpace"`, `"a"`) and the text it
types (`""` for none).

A `fill` popup covers its output: a full-screen launcher, or with
`layer = "background"` a wallpaper. Its view gets the output's size as
`ctx.width` and `ctx.height`. Background and bottom layer surfaces are drawn
behind the windows, also in spatial mode, and only take the pointer where
no window is.

With `input = "widgets"` the surface takes the pointer only over widgets
that react to it (`on_click`, `on_drag`, `on_scroll`, `id` or `hover`);
everywhere else clicks fall through to the windows or the desktop below.
This makes a full-screen `fill` layer usable as a desktop with a few
clickable things on it.

A `persistent` popup is opened by the shell itself as soon as an output is
ready, stays on one output, and moves to another when its output goes away
(a monitor unplugged, a VT switch). The greeter's login card is one
([GREETER.md](GREETER.md)).

With `outputs = "all"` a popup is persistent on every output instead: one
copy per output, following hotplug, each getting its own output as
`ctx.output`. Wallpapers, docks and desktop widgets use it. `shell.open`,
`shell.close` and `shell.is_open` are meant for popups that come and go, not
for these.

### Opening popups

| Function | |
|---|---|
| `shell.open(name[, args])` | Open (or reopen) a popup. `args.output` (an output name) and `args.margin` override its placement; the whole table is passed to its view. |
| `shell.close(name)` | Close it. A popup with `exit` styles plays them first; meanwhile it ignores input and no longer counts as open, so it can be reopened at once. |
| `shell.toggle(name[, args])` | Open or close it. |
| `shell.is_open(name)` | Whether it is open. |
| `shell.redraw()` | Repaint every surface, e.g. after changing your own state. |

## Widgets

Widgets are tables; these constructors fill in `type`:

| Constructor | |
|---|---|
| `shell.row{...}` / `shell.column{...}` / `shell.box{...}` | Container; its array part holds children. `box` is a row unless `direction = "column"`. |
| `shell.stack{...}` | Container whose children all lie over the same area (see Layout). |
| `shell.icon(name, {size=, stroke=, color=})` | A built-in line icon: `wifi`, `volume`, `mute`, `bluetooth`, `moon`, `sun`, `capture`, `play`, `pause`, `next`, `previous`, `battery`, `search`, `power`, `grid`, `plus`, `close`, `sliders`, `lock`, `music`, `check`, `bell`. `size` defaults to 16, `stroke` to 2 (in a 24-unit box). |
| `shell.text(text, {...})` | A line of text. |
| `shell.pip{radius=, lit=}` | The focus dot: a glowing dot when lit, a faint ring otherwise. |
| `shell.image{path=, width=, height=}` | A PNG. |
| `shell.spacer{...}` | An empty box that grows (or has a fixed `width`/`height`). |

Children may contain `false` (skipped, so `cond and widget or false`
works) and plain lists of widgets (spliced in, so loops can build them).

### Layout

Containers lay children out along one axis, like a simplified flexbox.

| Property | Default | |
|---|---|---|
| `direction` | `"row"` | `"row"` or `"column"` |
| `gap` | 0 | space between children |
| `padding` | 0 | number, `{vertical, horizontal}` or `{top, right, bottom, left}` |
| `align` | `"center"` | cross axis: `start`, `center`, `end`, `stretch` |
| `justify` | `"start"` | main axis: `start`, `center`, `end`, `space-between` |
| `width`, `height` | natural | fixed size |
| `min_width`, `max_width`, `min_height`, `max_height` | | limits on the natural size |
| `grow` | 0 | share of spare space along the parent's main axis |
| `shrink` | 1 for text, 0 otherwise | share of a deficit; text then ellipsizes |
| `clip` | false | hide children that do not fit (e.g. a task list) |

In a `stack` (`direction = "stack"`) every child gets the whole inner area
to itself. A child with `left`, `top`, `right` or `bottom` is pinned that
far from the edge (both of a pair: stretched between them, e.g. a full-size
backdrop with all four 0); otherwise `justify` places it horizontally and
`align` vertically. Later children are drawn over earlier ones.

### Style

| Property | |
|---|---|
| `background` | colour or `shell.gradient(direction, c1, c2[, c3, c4])`, direction `"vertical"`, `"horizontal"` or `"diagonal"` |
| `border`, `border_width` | hairline outline (width 1 by default) |
| `radius` | corner radius |
| `color` | text colour (text and pip) |
| `opacity` | 0..1 for the widget and its children |
| `scale` | drawing scale about the widget's centre, default 1 |
| `rotate` | drawing rotation about the widget's centre, in degrees clockwise |
| `translate` | `{x, y}` drawing offset in pixels |
| `shadow` | `{x, y, colour}`: a hard copy of the widget's shape, offset behind it; `false` for none |
| `hover` | a table of style properties used while the pointer is over the widget |
| `font` | Pango font, e.g. `"Sans SemiBold 9.5"`; default `"Sans Medium 9.5"` |
| `letter_spacing` | points, default 0.25 |
| `text_align` | 0 left .. 1 right, within the text's box |
| `ellipsize` | default true |
| `wrap`, `max_lines` | break text into lines at `max_width` (or `width`), ellipsizing after `max_lines` if set |

Colours are `"#RGB"`, `"#RRGGBB"`, `"#RRGGBBAA"` or `{r, g, b, a}` in 0..1.

`scale`, `rotate` and `translate` move only what is drawn; layout and
hit-testing use the untransformed box.

### Animation

Views return a new tree on every repaint; an animated widget is matched
with its previous self and springs from its old look to its new one.
Position and size (relative to the parent, so children ride along with a
moving parent), `background` (gradients included), `border`, `color`,
`opacity`, `scale`, `rotate`, `translate` and `shadow` all animate.

| Property | |
|---|---|
| `animate` | `true`, a preset (`"smooth"`, `"snappy"`, `"bouncy"`, `"gentle"`) or `{stiffness = n, damping = n}`; `false` turns it off |
| `enter` | style a new widget starts from, e.g. `{opacity = 0, scale = 0.96, translate = {0, -6}}` |
| `exit` | style a closing popup's widgets go to before the popup goes away |
| `key` | identity for matching; by default the `id`, else the parent's key and the child's position |

`enter`, `exit` and `hover` animate with `"smooth"` without `animate`.
`key` is for widgets that need a stable identity but must not become
`ctx.hovered` targets, such as a dot that comes and goes inside a pill.

```lua
shell.row {
    id = "clock", animate = "snappy",
    background = open and t.accent_deep or a(WHITE, 0.03),  -- fades
    enter = { opacity = 0, translate = { 0, -8 } },          -- slides in
}
```

While springs move, the shell repaints on every frame from the last tree
without running the view; when they settle it goes back to repainting only
on changes.

### Interaction

| Property | |
|---|---|
| `id` | names the widget; `ctx.hovered` reports it when the pointer is over it |
| `on_click` | `function(button, info)` with `button` = `"left"`, `"right"` or `"middle"` |
| `on_drag` | `function(info)`: called when the left button goes down on the widget, on every move while it is held (also outside the widget) and once more with `info.done = true` on release |
| `on_scroll` | `function(dy, dx, info)`: wheel or touchpad scrolling over the widget, in pixels, positive down and right |

`info` holds the widget's box (`x`, `y`, `width`, `height`), the pointer
inside it (`px`, `py`), the same as 0..1 fractions of its size (`fx`, `fy`,
clamped) and the `output`. The deepest widget with the handler under the
pointer gets the event; a left press goes to `on_drag` before `on_click`.

```lua
shell.stack {
    width = 200, height = 24,
    on_drag = function(info) set_volume(info.fx * 100) end,
    on_scroll = function(dy) set_volume(volume - dy / 2) end,
    ...
}
```

## Compositor state and actions

State comes from the compositor over the `shady-shell-v1` protocol.

| Function | Returns |
|---|---|
| `shell.workspaces()` | `{"main", "x", ...}` |
| `shell.active_workspace()` | name |
| `shell.windows()` | list of `{id, app_id, title, workspace, focused, maximized, fullscreen}` |
| `shell.focused()` | the focused window or `nil` |

| Action | |
|---|---|
| `shell.workspace(name)` | switch workspace |
| `shell.focus(id)` | focus a window (switching to its workspace) |
| `shell.maximize(id)` / `shell.fullscreen(id)` | toggle |
| `shell.move(id, workspace)` | move a window |
| `shell.close_window(id)` | ask a window to close |
| `shell.cycle()` | focus the next window |
| `shell.quit()` | end the Shady session |

Window arguments may also be the window tables themselves.

## Other data

| Function | |
|---|---|
| `shell.date(format)` | `strftime` of the local time; views using it repaint each minute, or each second if the format shows seconds |
| `shell.poll(command, ms[, fallback])` | the trimmed output of `sh -c command`, rerun every `ms` (at least 100); `fallback` or `""` until the first run ends |
| `shell.listen(command[, fallback])` | the latest line printed by a long-running command, restarted if it exits |
| `shell.apps(query, limit)` | `.desktop` applications matching the query: `{name, exec}` |
| `shell.launch(app)` / `shell.spawn(command)` | run an app or a command |
| `shell.theme` | the palette: `accent`, `accent_2`, `accent_deep`, `surface`, `text`, `text_dim`, `danger` (from the `SHADY_SHELL_*` variables) |
| `shell.mix(a, b, t)` | blend two colours |
| `shell.alpha(colour, a)` | multiply a colour's alpha |
| `shell.log(...)` | write to the shell's log |
| `shell.key(keys)` | press a compositor shortcut such as `"Super+t"` (the IPC `key` command); false when not connected |
| `shell.fonts(dir)` | make the font files in `dir` (relative to the config file) available by family name; call it before the views use them |
| `shell.data_dir`, `shell.config_path` | where the default UI and your config live |

`shell.poll` and `shell.listen` start their command the first time a view
asks for it and keep it running for as long as the config is loaded. Any
change in their value repaints the views. Together with `shadyctl
subscribe` (see [IPC_API.md](IPC_API.md)) they cover most status widgets
without native code:

```lua
local volume = shell.poll("wpctl get-volume @DEFAULT_AUDIO_SINK@", 2000)
local battery = shell.poll("cat /sys/class/power_supply/BAT0/capacity", 30000)
```

## Notifications

The shell can be the desktop's notification server
(`org.freedesktop.Notifications`, spec 1.2, over sd-bus). A config asks for
it; it keeps running across reloads. If another daemon owns the name, it
returns false and the shell leaves that daemon alone.

| Function | |
|---|---|
| `shell.notification_server{timeout = ms}` | serve notifications; `timeout` (default 5000) applies when a client asks for the server's default. Critical ones without a timeout stay until dismissed. |
| `shell.notifications()` | the history, newest first (at most 50): `{id, app, summary, body, icon, urgency, time, active, actions}`, `urgency` being `"low"`, `"normal"` or `"critical"`, `time` seconds since the epoch, `actions` a list of `{key, label}` |
| `shell.dismiss_notification(id)` | close it as dismissed by the user |
| `shell.invoke_notification(id, key)` | tell its client the action was chosen, then dismiss it; by convention `"default"` is a click on the notification itself |
| `shell.clear_notifications()` | forget the history, closing the active ones |

Every change (one arrives, expires, closes or is replaced) emits the
`notifications` event and repaints the views. Active notifications are the
ones to show as toasts; the default UI opens a `toasts` popup while there
are any:

```lua
shell.notification_server()
shell.on("notifications", function()
    local any = false
    for _, n in ipairs(shell.notifications()) do any = any or n.active end
    if any ~= shell.is_open("toasts") then
        if any then shell.open("toasts") else shell.close("toasts") end
    end
end)
```

## Events

`shell.on(name, fn)` registers a handler:

| Event | When |
|---|---|
| `"launcher"` | the compositor asked for the launcher (`shady.toggle_launcher()`), or the shell started with `SHADY_SHELL_OPEN_LAUNCHER=1` |
| `"change"` | the compositor's workspaces or windows changed |
| `"notifications"` | a notification arrived, expired, closed or was replaced (see Notifications) |

## Shader effects

With the GL renderer (the default), any bar, popup or widget can be drawn
through a GLSL ES 1.0 fragment shader. Effects are skipped, with one log
line, on the `wl_shm` renderer.

```lua
shell.bar {
    name = "top",
    shader = shell.shader("shaders/aurora.frag"),
    uniforms = { strength = 0.55, accent = shell.theme.accent },
    view = function(ctx)
        return shell.row {
            shell.row { shader = shell.shader("shaders/glow.frag"),
                        uniforms = { color = "#28e6ff", strength = 0.9 }, radius = 9, ... },
        }
    end,
}
```

- `shell.shader(path)` finds a fragment shader by absolute path, next to
  the config, or in the data directory, which ships
  `shaders/aurora.frag` and `shaders/glow.frag`.
  [`shell/examples/aurora.lua`](../shell/examples/aurora.lua) puts both on
  the default UI.
- A **surface shader** (`shader` on `shell.bar`/`shell.popup`) covers the
  whole surface. Its `uniforms` may be a table or a function returning one,
  called on every repaint.
- A **widget shader** (`shader` on a widget) covers the widget's rectangle
  after the surface is drawn. Its `uniforms` is a table.
- Both receive the surface's cairo drawing as `u_tex`, so they can keep,
  tint, distort or replace it. A shader that fails to compile is logged
  and the content is drawn as if it had none.

Shaders declare only the uniforms they use. The shell supplies:

| Uniform | Type | |
|---|---|---|
| `u_tex` | `sampler2D` | the surface's drawing, premultiplied RGBA |
| `u_time` | `float` | seconds since the shell started |
| `u_size` | `vec2` | the effect's size in logical px |
| `u_rect` | `vec4` | its x, y, width, height within the surface |
| `u_resolution` | `vec2` | its size in buffer px |
| `u_scale` | `float` | buffer scale |
| `u_mouse` | `vec2` | pointer position relative to the effect, `-1` when outside the surface |
| `u_hover` | `float` | 1 while the pointer is inside the effect |
| `u_radius` | `float` | the widget's `radius` |

and two varyings: `v_uv`, the surface position to sample `u_tex` at, and
`v_local`, running 0..1 across the effect (both with y pointing down). A
precision statement is added for you.

Lua uniforms become `u_<name>`: a number is a `float`, a colour a `vec4`,
and a list of 1–4 numbers a `float` to `vec4`.

Output premultiplied colour, like `u_tex`. A shader that declares `u_time`
is animated: the shell composites it every frame from the last drawing,
without running any view, so an animated bar costs GPU time but almost no
CPU. Shader files are watched with the config, and saving one reloads it.

## Compositor values

`shell.compositor_value(key[, fallback])` returns a value the compositor
published, from its Lua (`shady.publish`) or a compositor plugin
(`publish_value`), and views that read it repaint when it changes. The
shell follows them over the IPC socket (``, set for programs
the compositor starts) and reconnects if the compositor restarts it. This
is how a bar follows a rice's state, such as the current hour or palette:

```lua
local accent = shell.compositor_value("afterglow.accent", shell.theme.accent)
```

## Native plugins

| Function | |
|---|---|
| `shell.plugin(name[, options])` | load a native plugin once per shell process; returns true, or false and a message |
| `shell.value(key[, fallback])` | a value a plugin published (`"sysinfo.cpu"`); views reading it repaint when it changes |
| `shell.action(name[, argument])` | run a plugin action; returns false if none is registered |
| `shell.widget(type, props)` | a widget drawn by a plugin, sized with `width`/`height` or by its container |

Plugins outlive config reloads. Writing them is covered in
[SHELL_PLUGIN_API.md](SHELL_PLUGIN_API.md);
[`shell/examples/sysinfo.lua`](../shell/examples/sysinfo.lua) adds a CPU
graph to the default bar with the bundled `sysinfo` plugin, through the
default UI's `shell.options.bar_extra` hook.

## Frosted glass

Shader effects in the shell cannot see what is behind a surface. The
compositor can: in spatial mode, `shady.layer_effect(name, { blur = 18 })`
in the compositor's `init.lua` blurs the scene behind every layer surface
whose namespace is `name`. A bar's or popup's namespace is its `name` here
(`"shady-shell"`, `"quick"`, `"menu"`, `"launcher"` in the default UI).
Glass shows through translucent backgrounds, so pair it with
[`shell/examples/glass.lua`](../shell/examples/glass.lua), which sets
`shell.options.bar_opacity` and `panel_opacity`. See
[LUA_API.md](LUA_API.md) for the options.

## 3D placement

The compositor can also tilt, turn or recede any layer surface in spatial
mode, with clicks mapped to where it is drawn:
`shady.layer_transform("shady-shell", { tilt = -30, depth = 40 })` in the
compositor's `init.lua` leans the default bar back from its top edge. Like
`layer_effect`, it selects surfaces by namespace. See
[LUA_API.md](LUA_API.md).
