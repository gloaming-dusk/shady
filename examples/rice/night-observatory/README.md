# Night Observatory

A small opinionated Shady rice: calm orbit camera, a configurable dark background,
soft window motion, readable app surfaces, and a native constellation layout plugin.

## Build

From the repository root:

```sh
nix develop -c ninja -C build
nix develop -c ninja -C build libshady-plugin-orbit-layout.so
```

## Run

```sh
SHADY_ROOT=$PWD \
SHADY_LUA_INIT=$PWD/examples/rice/night-observatory/init.lua \
WLR_BACKENDS=wayland \
./build/shady -c ./examples/rice/night-observatory/config.lua
```

For a self-contained headless smoke test, replace `WLR_BACKENDS=wayland` with
`WLR_BACKENDS=headless`.

## Rice controls

| Binding | Action |
|---|---|
| Super + Return | Open a terminal (`$TERMINAL` or `foot`) |
| Super + D | Open an app launcher (`$SHADY_LAUNCHER`, then fuzzel/wofi/bemenu-run fallback) |
| Super + Tab | Cycle focus |
| Super + Q | Close focused window |
| Super + Space | Expand/fold the constellation |
| Super + F | Enter/leave first-person mode |
| Super + Shift + F | Toggle FPS input capture |
| Super + G | Built-in gravity toggle |
| Super + Shift + G | Gravity toggle through Lua |
| Super + R | Respawn lost windows |
| Super + I | Print the current constellation |
| Super + Shift + R | Hot-reload the layout plugin |
| Super + 0 | Reset camera |
| Super + +/- | Zoom |
| Super + Shift + Q | Quit through Lua |

Running `run.sh` with no extra Shady arguments also starts one terminal automatically, so the rice never boots into an unusable empty desktop. Set `SHADY_STARTUP` to choose another default command.

## Visual tuning

The rice keeps the application texture readable and puts the styling around it:

```lua
shady.set("background_top", "#080B14")
shady.set("background_horizon", "#101827")
shady.set("background_bottom", "#05070C")
shady.set("window_tint", "#FFFFFF")
shady.set("window_effect_strength", 0.04)
shady.set("window_brightness", 1.18)
shady.set("floor_base_color", "#070B12")
shady.set("floor_grid_color", "#16405F")
shady.set("floor_grid_strength", 0.11)
shady.set("floor_major_strength", 0.17)
shady.set("floor_fade_start", 0.9)
shady.set("floor_fade_end", 3.2)
```

`window_effect_strength` ranges from `0.0` (clean application texture) to `1.0`
(full chromatic edge/scanline/lighting effect). `window_brightness` accepts `0.25`
to `3.0`. Both `#RRGGBB` and `r,g,b` (0..1) forms are accepted for colors.
The background uses a three-stop fullscreen gradient, while the floor grid has independent base/grid colors, minor/major strengths, and radial fade distances. The old `sky.ppm` and `test-room.obj` setup is still present in `config.lua` as an opt-in example instead of being forced on every launch.

## Shutdown log note

When Shady exits, nested Wayland clients such as `foot` can print `Broken pipe`
and `Hangup` while their compositor socket disappears. That is expected during
shutdown. If the same messages appear while Shady is still running, that is a
separate compositor/client failure worth investigating.

The point of the demo is not the exact aesthetic. It is to show that a rice can
be assembled from a Lua bootstrap, a Lua runtime layer, built-in modules, and a
small hot-reloadable native plugin without changing compositor core code.
