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

The spatial environment is configured the same way. `environment_path` is
dispatched to the loader plugin registered for its extension, and can be
changed at runtime. See [Environment API](ENVIRONMENT_API.md).

```lua
shady.set("environment", true)
shady.set("environment_path", os.getenv("HOME") .. "/rooms/test-room.obj")
```

### `shady.bind(action, shortcut)`

Register a compositor key binding.

```lua
shady.bind("quit", "Super+Shift+Escape")
shady.bind("cycle_windows", "Super+Tab")
```

Available action names are compositor-defined.

### `shady.rule(table)`

Register a window rule. Rules may match `app_id` and/or `title`, and can set workspace/maximized/fullscreen state.

```lua
shady.rule({
    app_id = "firefox",
    workspace = "web",
    maximized = true,
})
```

The first matching rule is applied.

### `shady.spawn(command) -> boolean`

Run a shell command through `/bin/sh -lc`.

### `shady.quit()`

Terminate the compositor from the event loop.

### `shady.toggle_launcher()`

Ask `shady-shell` to toggle its launcher through the shell protocol.

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

### Window methods

```lua
window:focus()                  -- -> boolean
window:close()                  -- -> boolean
window:maximize([enabled])      -- -> boolean
window:set_fullscreen([enabled])-- -> boolean
window:move_to_workspace(name)  -- -> boolean
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
