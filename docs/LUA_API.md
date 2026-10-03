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

### `shady.reload_plugin(name) -> boolean`

Hot-reload a native plugin by module/plugin name.

### `shady.unload_plugin(name) -> boolean`

Unload a native plugin when its dependency contract allows it.

Plugin loading itself is available from configuration via `shady.plugin(path)`.

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
