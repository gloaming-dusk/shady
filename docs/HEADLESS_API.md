# Headless and Automation API

The headless API is exposed as `shady.automation` from the Lua runtime. It is intended for compositor regression tests and deterministic interaction scripts.

A typical headless test runs Shady with a headless wlroots backend:

```sh
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=1
export WLR_RENDERER=gles2
export LIBGL_ALWAYS_SOFTWARE=1
export SHADY_LUA_INIT="$PWD/tests/my-test.lua"

./build/shady -c tests/my-test-config.lua
```

## Timers

### `shady.automation.after(ms, callback)`

Run `callback` once after `ms` milliseconds.

```lua
shady.automation.after(200, function()
    shady.log("timer fired")
end)
```

`ms` must be non-negative.

## Keyboard

### `shady.automation.key(shortcut) -> boolean`

Press and release a key combination.

```lua
assert(shady.automation.key("Super+Tab"))
```

### `shady.automation.key_down(shortcut) -> boolean`

Send only the press state.

### `shady.automation.key_up(shortcut) -> boolean`

Send only the release state.

The shortcut syntax is the same syntax used by `shady.bind`.

## Text input

### `shady.automation.type_text(text) -> pid | nil, error`

Spawns `wtype` to type text into the focused Wayland client.

```lua
local pid, err = shady.automation.type_text("hello")
assert(pid, err)
```

This requires `wtype` in `PATH`.

## Pointer

### `shady.automation.move_pointer(x, y)`

Move the compositor pointer to logical layout coordinates.

### `shady.automation.click(button)`

Press and release a mouse button. `button` can be `"left"`, `"right"`, `"middle"`, or a numeric Linux input button code.

### `shady.automation.drag(x1, y1, x2, y2 [, button [, steps]])`

Move to the start point, press the button, interpolate to the end point, then release it.

```lua
shady.automation.drag(100, 100, 260, 180, "left", 12)
```

Default button: left. Default steps: 8. Valid step count: 1..256.

### `shady.automation.scroll([vertical [, horizontal]])`

Send pointer-axis scroll events.

```lua
shady.automation.scroll(-15, 0)
```

## Screenshots

### `shady.automation.screenshot(path [, callback]) -> pid | nil, error`

Captures the first available output using `grim`.

Without a callback:

```lua
local pid, err = shady.automation.screenshot("/tmp/shady.png")
assert(pid, err)
```

With a callback:

```lua
shady.automation.screenshot("/tmp/shady.png", function(ok, path)
    assert(ok, "screenshot failed")
    shady.log("saved " .. path)
end)
```

This requires `grim` in `PATH`.

## Recommended event-driven test pattern

Prefer waiting for compositor events instead of fixed startup sleeps.

```lua
local started = false

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "my-test-app" then
        return
    end
    started = true

    shady.automation.after(100, function()
        local x0, y0 = window.x, window.y
        shady.automation.drag(x0 + 40, y0 - 14,
                              x0 + 140, y0 + 36,
                              "left", 8)

        shady.automation.after(80, function()
            assert(window.x ~= x0 or window.y ~= y0)
            shady.quit()
        end)
    end)
end)
```

## Probe client

The repository builds `headless-automation-probe` for test scenarios that need a small xdg-toplevel with controlled app ID/title.

Existing tests under `tests/headless-*.lua` are useful reference implementations.

## Notes

Automation coordinates are compositor logical coordinates, not raw buffer pixels.

The automation API injects events directly into Shady where possible. `type_text` and screenshots intentionally use external Wayland tools.

For spatial tests, visually transformed 3D geometry may not line up with simple 2D coordinates. Prefer tests that use known screen-space states or coordinates derived from the rendered geometry.
