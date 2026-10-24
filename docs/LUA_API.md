# Lua API

Shady embeds Lua 5.4. The global API table is named `shady`.

Lua is used for two related jobs:

1. bootstrap/configuration;
2. runtime scripting after the compositor has started.

## Core functions

### `shady.log(message)`

Write an informational message to the compositor log.

### `shady.set(key, value)`
### `shady.config(key, value)`

Set a Shady configuration value. The two names are aliases.

Boolean values are converted to `"true"` / `"false"`; other values are accepted according to the configuration parser.

```lua
shady.set("spatial_mode", true)
shady.set("window_titlebar_height", 28)
shady.set("window_opacity", 0.92)
```

Window glass (`window_opacity`, `window_tint`, `window_brightness`,
`window_effect_strength`) shades the windows behind the focused one. The
focused window shows exactly what its client drew, so text and colours in
the window you work in stay true; `shady.set("window_focused_exact", false)`
applies the glass to it too.

`output_scale` sets the scale of every output (0.5-4). Left at 0, each
output gets one guessed from its panel's physical size the way niri and
GNOME do it: about 135 DPI on panels under 20" and 110 DPI on larger
monitors, in quarter steps (a 14" 2880x1800 laptop panel gets 1.75, monitors
around 100 DPI get 1). `SHADY_OUTPUT_SCALE` overrides both, and outputs can
still be changed at runtime with wlr-output-management tools (`wlr-randr`).

`cursor_theme` and `cursor_size` choose the pointer; left empty and 0 they
follow `XCURSOR_THEME` and `XCURSOR_SIZE` (24 when unset). When the theme
cannot be found Shady falls back to the Adwaita theme it was built with, and
it exports both variables so clients draw the same cursor.

`touchpad_tap` (default on) turns tap-to-click on for touchpads, and
`touchpad_natural_scroll` (default off) reverses their scrolling.
`xkb_layout`, `xkb_variant` and `xkb_options` set the keyboard layout
(`"us,kr"`, `"grp:alt_shift_toggle"`; empty follows `XKB_DEFAULT_*`),
`numlock` turns Num Lock on at start, and `repeat_rate` / `repeat_delay`
(25/s and 600 ms by default) set key repeat. Set these in `config.lua`:
keyboards are set up when they appear.

`prefer_no_csd` asks clients to leave out their own title bars even with
`window_titlebar` off, as niri's option of that name does: windows get a
border from Shady and nothing else.

`layout` chooses how windows are placed: `"floating"` (the default) leaves
them where they open; `"scrolling"` tiles them in a strip of columns per
workspace that scrolls sideways, as in niri (see [`shady.layout`](#scrolling-layout)).
`layout_gap` (12) is the space around and between tiles,
`layout_default_width` (0.5) a new column's share of the screen,
`layout_center_single` centres a workspace's only column, and
`layout_animation` (14) the spring rate of the strip's motion, 0 for none.
`gesture_fingers` (3) is the touchpad swipe that scrolls the strip
sideways and changes workspace up and down; 0 leaves all swipes to clients.

The spatial environment is configured the same way. `environment_path` is
dispatched to the loader plugin registered for its extension, and can be
changed at runtime. See [Environment API](ENVIRONMENT_API.md).

```lua
shady.set("environment", true)
shady.set("environment_path", os.getenv("HOME") .. "/rooms/test-room.obj")
```

### `shady.bind(action, shortcut)`

In `config.lua`: set the key of a compositor binding.

```lua
shady.bind("quit", "Super+Shift+Escape")
shady.bind("cycle_windows", "Super+Tab")
shady.bind("show_shortcuts", "Super+F1")
```

Available action names are compositor-defined. `"none"` removes a binding, including a
built-in default such as `quit` on Escape:

```lua
shady.bind("quit", "none")
```

| Action | Default | |
|---|---|---|
| `show_shortcuts` | `Super+Shift+slash` | show or hide the shortcut overlay |
| `quit` | `Escape` | end the session |
| `cycle_windows` | `Alt+Tab` | focus the next window |
| `close_window` | `Alt+F11` | close the focused window |
| `fps_toggle`, `fps_capture`, `gravity_toggle`, `debug_ray` | `F2`..`F5` | first-person mode |
| `camera_left`, `camera_right`, `camera_up`, `camera_down` | `Alt+arrows` | pan the camera |
| `camera_yaw_left`, `camera_yaw_right` | `Alt+Q`, `Alt+E` | turn the camera |
| `camera_zoom_in`, `camera_zoom_out`, `camera_reset` | `Alt+=`, `Alt+-`, `Alt+0` | zoom and reset |

### `shady.bind(keys, fn[, description])`

At runtime (`init.lua`): call `fn` when the key combination is pressed.
With a description, the binding is listed in the shortcut overlay under
"Desktop"; a table also chooses the group.

```lua
shady.bind("Super+Return", function() shady.spawn("foot") end, "Open a terminal")
shady.bind("Super+n", toggle_notes, { description = "Show notes", group = "Notes" })
shady.bind("F6", debug_dump) -- not listed
```

### `shady.rule(table)`

Register a window rule. Rules may match `app_id` and/or `title`, and can set workspace/maximized/fullscreen state.

```lua
shady.rule({
    app_id = "firefox",
    workspace = "web",
    maximized = true,
})
```

With the scrolling layout, `floating = true` keeps a window out of the
strip (dialogs and fixed-size windows float anyway), `floating = false`
tiles one that would float, and `width = 0.33` sets the share of the
screen its column opens with.

The first matching rule is applied, while the window maps and before it is focused.
Each workspace also remembers its last focused window and focuses it again when you
switch back.

### `shady.spawn(command) -> boolean`

Run a shell command through `/bin/sh -lc`.

### `shady.quit()`

Terminate the compositor from the event loop.

### `shady.toggle_launcher()`

Ask `shady-shell` to toggle its launcher through the shell protocol.

### Shortcut overlay

`Super+Shift+/` (the `show_shortcuts` binding) sends every listed shortcut to
the shell, which shows them grouped in an overlay. The list holds:

- the compositor's own bindings, as currently configured, under "Shady",
  "Camera" and "First person" (only while those modules are active);
- runtime bindings given a description, under "Desktop" or their group;
- what native plugins declare (`declare_shortcut` in
  [C_PLUGIN_API.md](C_PLUGIN_API.md)), under the plugin's name while it is
  active.

| Function | |
|---|---|
| `shady.describe(keys, description[, group])` | List a shortcut without binding it: keys the shell handles, or one line for several bindings. `keys` may be a free label. `nil` removes it. |
| `shady.shortcuts()` | The list: `{keys, group, description}` each, `keys` spelled as in `"Super+Shift+Z"` |
| `shady.toggle_shortcuts()` | Show or hide the overlay, like the key |

```lua
shady.describe("Super+1…3", "Go to workspace")
for i, name in ipairs({ "main", "web", "chat" }) do
    shady.bind("Super+" .. i, function() shady.workspace(name) end)
end
```

Bindings set up from Lua are forgotten with the Lua state, so a reloaded
`init.lua` lists only what it binds again.

### `shady.layer_effect(namespace, options | nil) -> boolean`

Give every layer-shell surface with this namespace (a bar, a panel, a
launcher; for `shady-shell` the namespace is the bar or popup name) a
backdrop effect: the scene behind it is drawn through a shader before the
surface itself, so translucent bars and panels become frosted glass. It
applies to any layer-shell client, not only `shady-shell`. `nil` removes
the effect.

```lua
shady.layer_effect("shady-shell", { blur = 18, saturation = 1.35, tint = "#08101a40" })
shady.layer_effect("waybar", { blur = 12 })
shady.layer_effect("quick", { shader = "/path/to/refract.frag", uniforms = { strength = 0.6 } })
```

| Option | Default | |
|---|---|---|
| `blur` | 12 | blur radius in logical px (built-in shader) |
| `saturation` | 1 | colour saturation of the blurred backdrop |
| `tint` | none | `#RRGGBBAA` laid over the backdrop |
| `shader` | built-in | a custom fragment shader; see "Layer backdrop effects" in [SHADER_API.md](SHADER_API.md) |
| `uniforms` | | values for a custom shader, as `u_<name>` |

The effect is masked by the surface's own alpha, so rounded or shaped
panels get glass only where they draw. It needs spatial mode and returns
false elsewhere, or when the shader fails to compile.

### `shady.screenshot([path[, delay_ms]]) -> path | nil`

Save the next frame the compositor draws as a PNG, exactly as shown (3D
scene included), without grim. `path` defaults to
`~/Pictures/shady-<time>.png` (`$XDG_PICTURES_DIR` when set). It returns
the path, or `nil` while another capture is pending; once the file is
written the published value `screenshot.saved` holds it
(`screenshot.failed` if writing failed). The Afterglow, Horizon and
Objects rices bind it to `Print`.

```lua
shady.bind("Print", function() shady.screenshot() end, "Save a screenshot")
```

### `shady.publish(key, value | nil) -> boolean` / `shady.value(key) -> string | nil`

Publish a named string for programs outside the compositor: IPC clients
(`values`, `value.changed` in [IPC_API.md](IPC_API.md)) and `shady-shell`
(`shell.compositor_value(key)`). Rices use it so the bar follows their
state, e.g. `shady.publish("rice.mode", "night")`. Keys are 1-128 characters
of `[A-Za-z0-9._-]`, values at most 4096 bytes; numbers and booleans are
published as text and `nil` removes the key. `shady.value` reads any
published value, including those from plugins.

### `shady.layer_transform(namespace, options | nil) -> boolean`

Place layer-shell surfaces with this namespace in 3D: tilt a bar back, turn
a panel toward the centre, or float a dock in depth. The surface is drawn on
a rotated plane through a screen-attached perspective (it does not move with
the 3D camera), and pointer input is mapped through the same plane, so it
is clicked where it appears. `nil` restores it.

```lua
shady.layer_transform("shady-shell", { tilt = -30, depth = 40 })  -- hinged on its top edge
shady.layer_transform("quick", { yaw = -18, depth = 30, pivot = "center" })
```

| Option | Default | |
|---|---|---|
| `tilt`, `yaw`, `roll` | 0 | rotation in degrees about the x, y and z axes |
| `depth` | 0 | logical px away from the viewer; negative comes closer |
| `perspective` | 1.2 × output height | focal length in logical px; smaller is more dramatic |
| `pivot` | `"anchor"` | `"anchor"` hinges on the edge the surface is attached to, `"center"` rotates about its centre |

Positive tilt leans the far edge away from the viewer. Spatial mode only. A
`layer_effect` on the same namespace follows the transformed surface, and
input is resolved in stacking order across transformed and plain layers.

## Runtime objects

### `shady.windows() -> Window[]`

Returns live windows in compositor stacking/runtime order.

### `shady.focused_window() -> Window | nil`

Returns the keyboard-focused window. In headless/spatial situations without physical keyboard focus, Shady falls back to its logical focused mapped window.

### Window properties

A `Window` exposes:

- `title`
- `app_id`
- `mapped`
- `visible`
- `x`, `y`
- `width`, `height`
- `z` when the spatial module is available
- `workspace`
- `maximized`
- `fullscreen`
- `tiled`: in the scrolling layout's strip

### Window methods

```lua
window:focus()                  -- -> boolean
window:close()                  -- -> boolean
window:maximize([enabled])      -- -> boolean
window:set_fullscreen([enabled])-- -> boolean
window:move_to_workspace(name)  -- -> boolean
window:toggle_floating()        -- -> boolean, scrolling layout only
```

If `enabled` is omitted for maximize/fullscreen, the state toggles.

Window userdata can become invalid after destruction. Property access on a dead handle returns `nil`; methods return failure.

## Outputs

### `shady.outputs() -> Output[]`

Each Output exposes:

- `name`
- `scale`
- `width`
- `height`

Width and height are effective/logical output resolution.

## Seat

### `shady.seat() -> Seat`

The Seat object currently exposes `name`.

## Modules and capabilities

### `shady.modules() -> Module[]`

At runtime, calling `shady.modules()` returns module objects with:

- `name`
- `active`

During configuration, `shady.modules({...})` is also used by the bootstrap configuration layer to enable or disable modules.

### `shady.has_capability(name) -> boolean`

Check whether a module capability is currently available.

Common examples include `"spatial"`, `"spatial.physics"`, and `"spatial.fps"`.

## Workspaces

### `shady.workspaces() -> string[]`

List known workspaces.

### `shady.current_workspace() -> string`

Return the active workspace name.

### `shady.workspace(name) -> boolean`

Switch workspace.

## Scrolling layout

With `shady.set("layout", "scrolling")` every workspace is a strip of
columns, wider than the screen when it needs to be, and the screen shows a
window's worth of it. A new window opens as a column right of the focused
one; moving the focus scrolls the strip so the focused column is on
screen, centring it when it cannot be shown next to the previous one
(niri's `center-focused-column "on-overflow"`). A column stacks one or more
windows. The strip lives on the output connected first.

`shady.layout` holds the commands; each returns false when there was
nothing to do:

| Function | |
|---|---|
| `focus(dir)` | `"left"`, `"right"` (columns), `"up"`, `"down"` (windows in the column), `"first"`, `"last"` |
| `move(dir)` | the same directions: move the column, or the window within its column |
| `consume_or_expel(side)` | `"left"`/`"right"`: put the window into the column on that side, or out of its column into a new one |
| `set_width(spec)` | `"+10%"`, `"-10%"`, `"50%"` (of the screen), `"900"` or `"+40"` (pixels) |
| `set_height(spec)` | the same for the window's share of its column |
| `cycle_width([forward])` | step through the preset widths (⅓, ½, ⅔) |
| `presets{...}` | set the preset widths, shares of the screen |
| `reset_height()` | even heights in the column |
| `full_width()` | toggle the column filling the screen (a maximize request from a client does the same) |
| `center()` | centre the focused column |
| `toggle_floating()` | lift the focused window out of the strip, or put it back |
| `workspace(delta[, take])` | the workspace above or below; numbered workspaces (`"1"`, `"2"`) are made as needed, but not past an empty one; with `take` the focused window goes along |
| `overview([on])` | GL renderer: pull the camera back over the whole strip, tilted; a click picks a window and flies back to it |
| `state()` | `{scrolling, columns, active, view, visible}` |

Dragging a tiled window lifts it out of the strip; dragging a tiled
window's edge resizes its column. A fullscreen window takes the whole
output, and the bars (top layer) step aside while it has the focus. Super
and the mouse wheel change workspace, Super+Shift+wheel the column, and a
three-finger swipe scrolls the strip (sideways) or changes workspace.

The layout publishes `layout.strip` for bars: `"<screen width> <view>
<active column>"` followed by `"|<x> <width> <windows>"` for each column,
in logical pixels of the strip; Tide's bar draws its minimap from it.
`layout.overview` is `"1"` while the overview shows.

## Idle

### `shady.idle(seconds, on_idle[, on_resume]) -> id`

Run `on_idle` after that long without input, and `on_resume` at the next
input. A client inhibiting idle (a playing video) holds it back, as does
`shady.idle_inhibit(true)`. `shady.idle_clear()` removes them all;
`shady.idle_inhibited()` tells whether anything holds idling back.

### `shady.output_power(on)`

Turn every output's panel off or back on. Any input turns them on again.

```lua
shady.idle(300, lock_the_screen)
shady.idle(330, function() shady.output_power(false) end)
```

## Events

### `shady.on(name, callback) -> handler_id`

Subscribe to a Shady event.

### `shady.off(handler_id) -> boolean`

Remove a Lua event handler.

Event names currently include:

- `window.created`
- `window.mapped`
- `window.unmapped`
- `window.focused`
- `window.resized`
- `window.destroyed`
- `window.state_changed`
- `window.title_changed` (title or app_id)
- `output.added`
- `output.removed`
- `module.started`
- `module.stopped`
- `workspace.changed`

Window events receive a Window object, output events an Output, module events a Module, and workspace changes receive the workspace name.
A window normally goes `created -> mapped -> focused` and ends `unmapped -> destroyed`.
Window handles check that the window is still alive, so a stale reference reads as
`nil`/`false` instead of touching freed state.

```lua
local id = shady.on("window.mapped", function(window)
    shady.log("mapped: " .. window.app_id)
end)

-- later:
shady.off(id)
```

## Plugin lifecycle

Native plugins are managed through `shady.plugins`. See
[Plugin manager](PLUGIN_MANAGER.md) for name resolution, search paths, shipped
plugins and states.

In `config.lua` (bootstrap):

- `shady.plugins.load(name_or_path)`: load a plugin. Raises on failure.
- `shady.plugins.path(dir)`: add a search directory.
- `shady.plugins.enable(name)` / `shady.plugins.disable(name)`: a disabled
  shipped plugin is never loaded.
- `shady.plugins.list()`
- `shady.plugin(path)`: older name for `shady.plugins.load`.

At runtime:

- `shady.plugins.list() -> Plugin[]`: entries with `name`, `spec`, `path`,
  `builtin`, and `state` (`active`, `inactive`, `unloaded`, `disabled`, `failed`).
- `shady.plugins.reload(name) -> boolean`: hot-reload, with state migration
  for ABI v2.
- `shady.plugins.unload(name) -> boolean`: unload when the dependency
  contract allows it.
- `shady.reload_plugin(name)` / `shady.unload_plugin(name)`: older names for
  the two calls above.

`name` may be either the name used to load the plugin or its module name.

## Spatial API

These functions only exist when the relevant capability is active.

### `shady.camera(property, value)`

Supported properties:

- `yaw`
- `pitch`
- `distance`
- `target_x`
- `target_y`
- `target_z`

### Physics

When `spatial.physics` is available:

- `shady.toggle_gravity()`
- `shady.respawn_all()`

### FPS

When `spatial.fps` is available:

- `shady.toggle_fps()`
- `shady.expand_all()`
- `shady.fold_all()`

## Automation

The test-oriented `shady.automation` namespace is documented separately in [HEADLESS_API.md](HEADLESS_API.md).
