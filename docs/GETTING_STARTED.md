# Getting started

Shady is two programs: `shady`, the compositor, and `shady-shell`, the
desktop shell drawn on top of it. A **rice** pairs them with a look:
Horizon (the default), Objects, Afterglow or Neon Transit. This page gets
one running, shows the keys, and points at where to change things.

You need Linux with a GPU that does OpenGL ES 2 (any Mesa or NVIDIA driver
does). The easy path is [Nix](https://nixos.org/download); without it,
see [Building without Nix](#building-without-nix).

## Try it in a window

From any Wayland session (GNOME, KDE, Sway, niri, ...):

```sh
nix run github:gloaming-dusk/shady
```

This builds Shady and opens Horizon inside a window of your current
desktop, with a terminal. Open applications from that terminal, or with
`Super+D`. Press **`Super+Shift+/`** to see every shortcut. Close the window
or press `Super+Shift+Escape` to leave.

Another rice:

```sh
SHADY_RICE=objects nix run github:gloaming-dusk/shady
```

The same command run from a text console (Ctrl+Alt+F3, logged in) starts
Shady full screen on that console.

## From a checkout

```sh
git clone https://github.com/gloaming-dusk/shady
cd shady
nix develop          # wlroots 0.20.2, Lua 5.4, Meson, ...
./tools/build.sh     # builds into build/
./rices/horizon/run.sh
```

Each rice has a `run.sh` (`rices/objects/run.sh`, `rices/afterglow/run.sh`,
`rices/neon-transit/run.sh`) and a README with its keys and options. Other
ways to start the compositor from `build/`:

| Command | |
|---|---|
| `WLR_BACKENDS=wayland ./build/shady -s foot` | the bare compositor with a terminal, nested |
| `WLR_BACKENDS=wayland ./build/shady --safe` | the plain 2D path: no 3D renderer, physics or effects |
| `./tools/run-native.sh --safe -s foot` | full screen on a text console, 2D path first |
| `./tools/run-native.sh -s './build/shady-shell & exec foot'` | full screen with the shell |

`-s` runs a command once the compositor is up. Shady prints its
`WAYLAND_DISPLAY`, so more clients can be started from another terminal.

On a console, run from a real login on a VT, not from a terminal inside
another desktop. If Shady cannot take the seat, start it through seatd:
`seatd-launch ./build/shady --native`. If `run-native.sh` says
`build/shady` is outdated, rebuild with `./tools/build.sh`.

## Installing on NixOS

The flake has a NixOS module that adds a Shady session to your login
manager:

```nix
{
  inputs.shady.url = "github:gloaming-dusk/shady";

  outputs = { nixpkgs, shady, ... }: {
    nixosConfigurations.my-machine = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./configuration.nix
        shady.nixosModules.default
        {
          programs.shady.enable = true;
          programs.shady.rice = "horizon";       # or "objects", "afterglow", "neon-transit"
          programs.shady.greeter.enable = true;  # optional: log in through the Shady greeter
        }
      ];
    };
  };
}
```

Pick **Shady** (or **Shady (safe)**, the 2D path) at login.
`programs.shady.softwareCursor = true` helps on virtual machines whose
cursor planes misbehave, and `programs.shady.extraPackages` replaces the
tools installed alongside the session (a terminal, `grim`, ...). The
greeter is described in [GREETER.md](GREETER.md). The session logs to the
journal: `journalctl -t shady`.

## Building without Nix

Install the development packages for wlroots **0.20.2** (exactly the 0.20
series), wayland, wayland-protocols, wlr-protocols, libxkbcommon, libinput,
pixman, libdrm, EGL/GLES2, Lua 5.4, cairo, pango and fontconfig, plus Meson,
Ninja and pkg-config. Then:

```sh
meson setup build
ninja -C build
sudo ninja -C build install   # optional; adds the shady and shady-safe sessions
```

After installing, `shady-session` starts the session with the rice named
by `SHADY_RICE` (default `horizon`).

## Keys

`Super+Shift+/` lists the shortcuts of the running session: the
compositor's, the rice's and those of each loaded plugin. What follows are
the compositor's defaults; rices add their own (Horizon: `Super+D` for the
launcher, `Super+Return` for a terminal, `Super+1…3` for workspaces,
`Super+T` to move the sky to the next hour).

**Everywhere**

| Keys | |
|---|---|
| `Super+Shift+/` | show the shortcuts |
| `Alt+Tab` | focus the next window |
| `Alt+F11` | close the focused window |
| `Escape` | quit (rices move this to `Super+Shift+Escape`) |
| `Super` + drag | move the window under the pointer |
| `Super` + right-drag | resize it from the nearest corner |
| drag just outside a window's edge | resize it from that edge |

**The 3D camera** (spatial mode)

| Input | |
|---|---|
| `Alt` + right-drag / middle-drag | orbit / pan |
| `Alt` + scroll | zoom |
| `Alt+Shift` + scroll | move the focused window in depth |
| `Alt` + arrows, `Alt+Q` / `Alt+E` | pan, turn |
| `Alt+=` / `Alt+-`, `Alt+0` | zoom, reset |
| `F5` | show collision and picking |

**First person**

| Input | |
|---|---|
| `F2` | enter or leave first-person mode |
| `F3` | capture or release the pointer |
| `F4` | window gravity on or off |
| `W A S D`, mouse, `Space` | walk, look, jump |
| click / right-click | pick up or drop the window in view / throw it |
| scroll while holding | bring it closer or push it away |

A window thrown out of the world comes back by itself; `shady.respawn_all()`
brings back all of them.

Each binding can be changed in `config.lua` (`shady.bind("quit",
"Super+Shift+Escape")`, `"none"` removes one); the names are listed in
[LUA_API.md](LUA_API.md#shadybindaction-shortcut).

## Making it yours

Shady reads three Lua files from `~/.config/shady/`. Anything missing comes
from the rice, so start with one and add the others when you need them.

| File | Runs | For |
|---|---|---|
| `config.lua` | before the compositor starts | settings, modules, plugins, compositor key names |
| `init.lua` | once it is running | your key bindings, window rules, reacting to events |
| `shell.lua` | in `shady-shell` | bars, panels, the launcher; reloads when saved |

```lua
-- ~/.config/shady/config.lua
shady.set("window_corner_radius", 14)
shady.set("window_opacity", 0.95)
shady.plugins.load("frozen-window")       -- Super+Z freezes a window
shady.bind("quit", "Super+Shift+Escape")
```

```lua
-- ~/.config/shady/init.lua
shady.bind("Super+Return", function() shady.spawn("foot") end, "Open a terminal")
shady.rule({ app_id = "firefox", workspace = "web" })
shady.on("window.mapped", function(w) shady.log("opened " .. w.app_id) end)
```

```lua
-- ~/.config/shady/shell.lua: keep the default UI, change its colours
shell.theme.accent = "#ff9a5c"
dofile(shell.data_dir .. "/default.lua")
```

A rice's own files (`rices/horizon/` and the others) show what a complete
setup looks like. The full references are
the [Lua API](LUA_API.md), the [Shell Lua API](SHELL_LUA_API.md) and the
[Plugin manager](PLUGIN_MANAGER.md); [API overview](API.md) says which one
fits a task.

## When something is wrong

- **Black screen or a crash on start:** try `--safe` (or the **Shady (safe)**
  session). If that works, the 3D renderer is at fault; the log says why.
- **No pointer on a virtual machine:** `SHADY_SOFTWARE_CURSORS=1`, or
  `programs.shady.softwareCursor` on NixOS.
- **"the spatial module requires the GLES2 renderer":** the 3D path needs
  GLES2; unset `WLR_RENDERER=pixman`, or use `--safe`.
- **The shell does not appear:** run `shady-shell` from a terminal inside
  Shady to see its errors; a broken `shell.lua` falls back to the default UI.

Shady is experimental. Bug reports with the log are welcome at
<https://github.com/gloaming-dusk/shady/issues>.
