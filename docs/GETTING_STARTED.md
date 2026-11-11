# Getting started

Shady is the compositor. Out of the box it is **Tide**, an everyday desktop
with scrolling columns and niri's keys. A shell drawn on top of it,
[shady-shell](https://github.com/gloaming-dusk/shady-shell), is a package of
its own; [Gloam](https://github.com/gloaming-dusk/gloam) puts the two together
with Tide's bar, control center and login screen. Other **rices** give it
another look. This page gets Shady running, shows the keys, and points at
where to change things.

You need Linux with a GPU that does OpenGL ES 2 (any Mesa or NVIDIA driver
does). The easy path is [Nix](https://nixos.org/download); without it,
see [Building without Nix](#building-without-nix).

## Try it in a window

From any Wayland session (GNOME, KDE, Sway, niri, ...):

```sh
nix run github:gloaming-dusk/shady
```

This builds Shady and opens Tide's layout and keys inside a window of your
current desktop, with a terminal. Open applications from that terminal.
Press **`Super+Shift+/`** to see every shortcut. Close the window or press
`Super+Shift+Escape` to leave.

With its shell, the whole desktop:

```sh
nix run github:gloaming-dusk/gloam
```

The same command run from a text console (Ctrl+Alt+F3, logged in) starts
Shady full screen on that console.

## From a checkout

```sh
git clone https://github.com/gloaming-dusk/shady
cd shady
nix develop          # wlroots 0.20.2, Lua 5.4, Meson, ...
./tools/build.sh     # builds into build/
WLR_BACKENDS=wayland ./build/shady   # Tide, nested
```

`shady` starts `shady-shell` when it finds one (`$SHADY_SHELL_BIN`, next to
itself, or on `PATH`): build [shady-shell](https://github.com/gloaming-dusk/shady-shell)
alongside, and Tide's `shell.lua` from [Gloam](https://github.com/gloaming-dusk/gloam)
is found with `SHADY_RICE_PATH=../gloam/rices`. Rices are repositories of their own
(see the [README](../README.md#rices)); each has a `run.sh` run with
`SHADY_ROOT` set to your checkout. Other ways to start the compositor from
`build/`:

| Command | |
|---|---|
| `WLR_BACKENDS=wayland ./build/shady` | the default desktop, nested |
| `WLR_BACKENDS=wayland ./build/shady -s foot` | the default desktop with a terminal and no shell; `-s` replaces what starts |
| `SHADY_RICE=none WLR_BACKENDS=wayland ./build/shady -s foot` | the bare compositor |
| `WLR_BACKENDS=wayland ./build/shady --safe` | the plain 2D path: no 3D renderer, physics or effects |
| `./tools/run-native.sh --safe -s foot` | full screen on a text console, 2D path first |
| `./tools/run-native.sh -s 'shady-shell & exec foot'` | full screen with the shell |

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
          programs.shady.rice = "tide";          # the default; others come from programs.shady.rices
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
session logs to the journal: `journalctl -t shady`.

This is the compositor. For the shell and the login screen, import the
modules of [shady-shell](https://github.com/gloaming-dusk/shady-shell) and
[shady-greeter](https://github.com/gloaming-dusk/shady-greeter), or use
[Gloam](https://github.com/gloaming-dusk/gloam), which turns on all of it.

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
by `SHADY_RICE` (default `tide`), and `shady-shell` if it is on `PATH`.

## Keys

`Super+Shift+/` lists the shortcuts of the running session: the
compositor's, the rice's and those of each loaded plugin. What follows are
the compositor's, which Tide mostly replaces with niri's: see
[its README](../rices/tide/README.md) (`Super+Space` for the launcher,
`Super+Return` for a terminal, `Super+1…9` for workspaces). The tables below
describe the bare compositor (`SHADY_RICE=none`) and the 3D rices.

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
from Tide, so start with one and add the others when you need them.

| File | Runs | For |
|---|---|---|
| `config.lua` | before the compositor starts | settings, modules, plugins, compositor key names |
| `init.lua` | once it is running | your key bindings, window rules, reacting to events |
| `shell.lua` | in [`shady-shell`](https://github.com/gloaming-dusk/shady-shell), when installed | bars, panels, the launcher; reloads when saved |

Start from Tide and change what you want:

```lua
-- ~/.config/shady/config.lua
shady.base("tide")
shady.set("window_corner_radius", 6)
shady.plugins.load("border-accent")
```

```lua
-- ~/.config/shady/init.lua
shady.base("tide")
shady.bind("Super+b", function() shady.spawn("firefox") end, "Browser")
shady.rule({ app_id = "mpv", floating = true })
```

```lua
-- ~/.config/shady/shell.lua
shell.base("tide", { palette = { accent = "#FF9E64" } })
```

[Customizing](CUSTOMIZING.md) has the rest: everything a config can add to
Tide, `shady-rice eject` to make a copy of it your own, and `SHADY_RICE=none`
to start from the bare compositor. The full references are the
[Lua API](LUA_API.md), the [Shell Lua API](https://github.com/gloaming-dusk/shady-shell/blob/main/docs/SHELL_LUA_API.md) and the
[Plugin manager](PLUGIN_MANAGER.md); [API overview](API.md) says which one
fits a task.

## When something is wrong

- **Black screen or a crash on start:** try `--safe` (or the **Shady (safe)**
  session). If that works, the 3D renderer is at fault; the log says why.
- **No pointer on a virtual machine:** `SHADY_SOFTWARE_CURSORS=1`, or
  `programs.shady.softwareCursor` on NixOS.
- **"the spatial module requires the GLES2 renderer":** the 3D path needs
  GLES2; unset `WLR_RENDERER=pixman`, or use `--safe`.
- **The shell does not appear:** it is a package of its own; check that
  `shady-shell` is on `PATH` (or set `SHADY_SHELL_BIN`), then run it from a
  terminal inside Shady to see its errors. A broken `shell.lua` falls back
  to the plain UI.

Shady is experimental. Bug reports with the log are welcome at
<https://github.com/gloaming-dusk/shady/issues>.
