# Night Observatory

A small opinionated Shady rice: calm orbit camera, sky/environment rendering,
soft window motion, and a native constellation layout plugin.

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

The point of the demo is not the exact aesthetic. It is to show that a rice can
be assembled from a Lua bootstrap, a Lua runtime layer, built-in modules, and a
small hot-reloadable native plugin without changing compositor core code.
