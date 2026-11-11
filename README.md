# shady

![Tide, the default desktop, as Gloam ships it: terminals in a strip of scrolling columns under a glass bar, and a notification](https://raw.githubusercontent.com/gloaming-dusk/gloam/main/rices/tide/preview.jpg)

*Tide as [Gloam](https://github.com/gloaming-dusk/gloam) ships it: Shady's scrolling columns and keys, under shady-shell's glass bar.*

Shady is an experimental **3D Wayland compositor** built on wlroots 0.20.2 and inspired by TinyWL. It can run as a conventional compositor through its wlroots scene-graph path, or turn normal xdg-shell applications into objects inside a shared perspective 3D world.

Windows can move through depth, fold into cubes, collide with authored environments, be picked up and thrown, or receive native-plugin-driven surface effects. Lua configures and scripts it, and a public C plugin API extends spatial behavior without putting every experiment into the compositor core.

Shady is the compositor only. The rest of a desktop lives in repositories of its own:

| | |
|---|---|
| [shady-shell](https://github.com/gloaming-dusk/shady-shell) | the Lua-scripted desktop shell: bars, launcher, notifications, lock screen |
| [shady-greeter](https://github.com/gloaming-dusk/shady-greeter) | the greetd login screen |
| [Gloam](https://github.com/gloaming-dusk/gloam) | the whole desktop: Shady, the shell, Tide's look, the greeter, services and applications |
| [rices](#rices) | other looks |

## Try it

From any Wayland desktop, with [Nix](https://nixos.org/download):

```sh
nix run github:gloaming-dusk/shady      # the compositor, Tide's layout and keys, a terminal
nix run github:gloaming-dusk/gloam      # the whole desktop, with its shell
```

Press **`Super+Shift+/`** to see every shortcut. [Getting started](docs/GETTING_STARTED.md)
covers building from a checkout, installing on NixOS or elsewhere, the keys
and your own configuration.

## Tide, the default

With nothing configured, Shady is Tide: niri's strip of scrolling columns
and its keys, borders and rounded corners, window rules, idle, and
screenshots ([rices/tide](rices/tide)). Tide's shell (its bar, control center,
launcher, lock and login screens) comes with Gloam. Without a shell the keys
still work, with whatever is installed: `fuzzel` or `wofi` for the
launcher, `swaylock`, `wpctl`, `brightnessctl`, `playerctl`, and Shady's own
screenshots.

- **Build on it.** `shady.base("tide")` at the top of your `config.lua` and
  `init.lua` ([Customizing](docs/CUSTOMIZING.md)), and `shell.base("tide", { ... })`
  in `shell.lua`. Tide's updates keep reaching you.
- **Take it apart.** `shady-rice eject` copies Tide into `~/.config/shady`, and
  all of it is yours to rewrite.
- **Start from nothing.** `SHADY_RICE=none` is the bare compositor.

## Rices

Other looks are a setting away once installed: `SHADY_RICE=afterglow`, or
`programs.shady.rice` and `programs.shady.rices` in the NixOS module. A rice
is a directory in `share/shady/rices/<name>` on `XDG_DATA_DIRS`,
`~/.local/share/shady/rices` or `$SHADY_RICE_PATH`.

- [Afterglow](https://github.com/gloaming-dusk/shady-rice-afterglow): the desktop above an open sea at sunset, cycling through four hours with `Super+T`
- [Horizon](https://github.com/gloaming-dusk/shady-rice-horizon): the shell lives in Afterglow's sky, with a 3D-turned glass control center and a constellation launcher
- [Objects](https://github.com/gloaming-dusk/shady-rice-objects),
  [Neon Transit](https://github.com/gloaming-dusk/shady-rice-neon-transit),
  [Gensokyo](https://github.com/gloaming-dusk/shady-rice-touhou),
  [Stage](https://github.com/gloaming-dusk/shady-rice-miku) and
  [retired ones](https://github.com/gloaming-dusk/shady-rice-archive)

## Highlights

- A custom GLES2 renderer that places Wayland surfaces in a perspective 3D world, with a plain 2D scene-graph path (`--safe`) as the compatibility baseline
- Windows with physics: gravity, wobble, collision with authored OBJ environments, first-person navigation, picking up and throwing
- Window bodies defined by plugins: cubes, jelly, folded paper and origami share one shape for drawing, picking and collision
- Frosted glass, GLSL backdrop effects and 3D placement for layer surfaces, which [shady-shell](https://github.com/gloaming-dusk/shady-shell) and other shells use
- A scrolling tiling layout (`layout = "scrolling"`): niri's strip of columns, with touchpad swipes, animated scrolling and workspace slides, and a 3D overview of the strip
- Rewind: the desktop records each layout it settles into; `Super+BackSpace` scrubs back through them, the world fading like an old photograph, and `Return` restores that moment
- A shortcut overlay (`Super+Shift+/`) listing every key of the session: the compositor's, the rice's and each plugin's
- Lua 5.4 for configuration (`config.lua`), runtime scripting (`init.lua`) and headless UI automation
- A public C plugin API with hot reload and state migration, and an IPC socket with the `shadyctl` CLI for scripts and other bars
- Input methods, fractional scaling, session lock, screencopy and the standard toplevel and workspace protocols ([Desktop integration](docs/DESKTOP.md))
- ASan/UBSan regression suites for the 2D and 3D paths

Run Lua in the live compositor with `shadyctl lua`:

```sh
shadyctl lua 'shady.workspace("two")'
shadyctl lua 'return shady.current_workspace(), shady.workspaces()'
shadyctl lua -f script.lua
printf 'return shady.focused_window().title\n' | shadyctl lua -
```

Commands share the runtime used by `init.lua`, including its globals and
functions. Return values are printed as a JSON array; errors exit with status 1.
See the [IPC API](docs/IPC_API.md#running-lua) for details.

## Plugins

Load one with `shady.plugins.load("<name>")` in `config.lua`. The rices
load several.

| Plugin | Keys | |
|---|---|---|
| `rewind` | `Super+BackSpace` | scrub back through earlier layouts of the desktop and restore one (Afterglow, Horizon) |
| `spatial-overview` | `Super+O` | 3D overview of the workspace's windows |
| `border-accent` | `Super+B` | accent the focused window's border |
| `afterglow` | `Super+T`, `Super+Shift+T` | procedural sky and sea through four hours (Afterglow, Horizon) |
| `focus-depth` | | the focused window comes forward in depth |
| `fps-cube`, `fps-jelly`, `fps-squash`, `fps-folded-paper`, `fps-origami` | | window bodies in first-person mode |
| `close-slide-fade` | | another close animation |
| `window-motion` | | wobble and tilt (loaded by default) |
| `obj-loader` | | loads `.obj` environments when one is configured |

More are in [shady-plugins](https://github.com/gloaming-dusk/shady-plugins):
`water-windows`, `frozen-window`, `black-hole`, `window-portal`,
`magnetic-windows`, `window-constellation`, `bad-apple` and others.

Writing one: [C plugin API](docs/C_PLUGIN_API.md), starting from
`examples/plugins/hello.c`.

## Documentation

- [Getting started](docs/GETTING_STARTED.md): run, install, keys, configure
- [Customizing](docs/CUSTOMIZING.md): build on Tide, take it apart, or start from nothing
- [API overview](docs/API.md): which interface fits which job
- [Lua API](docs/LUA_API.md) · [IPC API](docs/IPC_API.md) · [Headless / automation API](docs/HEADLESS_API.md)
- [C plugin API](docs/C_PLUGIN_API.md) · [Plugin manager](docs/PLUGIN_MANAGER.md) · [Shader API](docs/SHADER_API.md) · [Environment API](docs/ENVIRONMENT_API.md)
- [Desktop integration](docs/DESKTOP.md)
- The shell's own: [Shell Lua API](https://github.com/gloaming-dusk/shady-shell/blob/main/docs/SHELL_LUA_API.md) · [Shell plugin API](https://github.com/gloaming-dusk/shady-shell/blob/main/docs/SHELL_PLUGIN_API.md) · [Greeter](https://github.com/gloaming-dusk/shady-greeter)
- [Development](docs/DEVELOPMENT.md): layout, build options, tests · [Architecture](docs/ARCHITECTURE.md)

Shady is the compositor of [Gloam](https://github.com/gloaming-dusk/gloam), a
desktop. Its site, <https://gloaming-dusk.github.io/>, renders these `docs/*.md`
files directly; its source is in
[gloaming-dusk.github.io](https://github.com/gloaming-dusk/gloaming-dusk.github.io).

## What this repository holds

- `shady`, the compositor (`compositor/`), its plugins (`plugins/`) and the
  public plugin headers (`include/shady/`),
- `shadyctl`, the IPC client, and `shady-session` / `shady-rice`,
- Tide's compositor side (`rices/tide`),
- the flake with the package and the NixOS module (`flake.nix`, `nix/`).

Shady carries a copy of the `shady-shell-v1` protocol
(`compositor/protocols/shady-shell-v1.xml`) for its server side;
[shady-shell](https://github.com/gloaming-dusk/shady-shell) owns it.

Nothing here is tied to a distribution: any NixOS system can use the module,
and other systems build with Meson.

## Status

Shady is a research compositor, not a production desktop: expect rough
edges, especially with multiple outputs and in first-person mode. Areas
under active work include per-window scripting actions, player collision
against triangle geometry, richer environment semantics, better shadow
receivers, multi-output behavior and rendering polish. Changes are listed
in [CHANGELOG.md](CHANGELOG.md).

## License

Apache-2.0; see [LICENSE](LICENSE). The imported TinyWL example is CC0;
see [its license](LICENSES/tinywl-CC0.txt).
