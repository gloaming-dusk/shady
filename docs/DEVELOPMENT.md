# Development

How the tree is laid out, how to build smaller or instrumented variants, and
how the test suites run. [GETTING_STARTED.md](GETTING_STARTED.md) covers the
ordinary build; [ARCHITECTURE.md](ARCHITECTURE.md) the plugin boundaries.

## Repository layout

```text
compositor/           the shady compositor
  src/
    modules/
      lua/            embedded Lua runtime and Shady scripting API
      physics/        cube gravity and world collision
      fps/            first-person movement, grabbing and throwing
      window_motion/  neutral core adapters for plugin-driven motion
      close_animation/ close state machine
      scene_effects/  floor and shadow effects
      environment/    format-neutral environment scene, loader registry, sky
    plugin/           loading, host APIs, representation and motion drivers
    render/           GLES2 pipeline, math, picking and debug rendering
    ipc/              the IPC socket, its JSON, published values, shortcuts
    world/            shared world and collider representation
  shaders/            the renderer's GLSL
  protocols/          shady-shell-v1 and vendored protocol XML
  ctl/                shadyctl
include/shady/        public plugin and shell-plugin ABI headers
plugins/              native feature plugins (window-motion, afterglow, ...)
  shaders/            their GLSL
  loaders/            environment asset loader plugins (obj/; gltf/ etc. later)
shell/                shady-shell: default Lua UI, the shortcut overlay, examples, shaders
  src/                its C sources
  plugins/            shell plugins (sysinfo)
greeter/              the greetd login screen (see GREETER.md)
  src/                the greetd shell plugin
rices/                complete looks: Horizon, Objects, Afterglow, Neon Transit; archive/
examples/             small plugins and configs to start from
data/                 session and greeter launchers, wayland-sessions entries
nix/                  Nix package, NixOS module and VM tests
tests/                unit and headless tests
tools/                build, run and regression scripts
assets/               development environments, sky images, media
docs/                 documentation, also rendered by the project site
```

Installed data under `share/shady` mirrors this tree (`compositor/shaders`,
`plugins/shaders`, `shell`, `greeter`, `rices`), so plugins and rices find
their files through `SHADY_ROOT` in a checkout and in an installation alike.

## Build options

`./tools/build.sh` configures `build/` (or reconfigures it, refreshing
cached dependency lookups) and runs Ninja. Meson exposes the top-level
modules and the spatial subfeatures:

```text
-Dspatial=enabled|disabled|auto
-Dlua=enabled|disabled|auto
-Dshell=enabled|disabled|auto
-Dphysics=enabled|disabled|auto
-Dfps=enabled|disabled|auto
-Dwindow_motion=enabled|disabled|auto
-Dclose_animation=enabled|disabled|auto
-Dscene_effects=enabled|disabled|auto
```

All of minimal desktop, spatial only, spatial with window motion, spatial
with physics and the full stack with Lua are valid shapes. A genuinely small
build drops the spatial stack, runtime Lua and the shell; the bootstrap Lua
config engine stays, so `config.lua` still works:

```sh
meson setup build-minimal -Dspatial=disabled -Dlua=disabled -Dshell=disabled
ninja -C build-minimal
```

The minimal build uses the wlroots scene renderer directly and does not
need GLES2, so it also runs with `WLR_RENDERER=pixman`.

## Module host

Built-in modules and native plugins share one module descriptor
(`include/shady/module.h`). A module owns its runtime state and optional
per-window state, and declares capabilities it provides, requires, or
optionally consumes (which only affects ordering). The resolver does a
stable dependency sort, rejects missing required capabilities, duplicate
active providers and cycles, initializes in dependency order and destroys
in reverse.

State follows the same boundary. `spatial` owns only shared camera, world
and timing state plus each window's Z. Physics owns gravity and velocity,
FPS owns capture, grab and expansion, window-motion owns wobble and tilt,
close-animation its per-window state machine. Rendering and picking read
those through module APIs instead of storing them in window objects. The
world owns collision geometry; physics and interaction query it rather
than knowing about individual environment objects.

The built-in modules are `desktop-protocols`, `workspace`, `spatial`,
`physics`, `fps`, `close-animation`, `scene-effects` and `lua`; the shipped
`window-motion` plugin registers the same way when built. Lua scripts
feature-detect with `shady.has_capability(...)`.

## Plugin reload

Plain hot-**unload** is limited to stateless plugins, since removing a
stateful module leaves nowhere to keep its state. Hot-**reload** supports
stateful plugins through `SHADY_PLUGIN_ABI_V2`, whose descriptor adds a
schema version and snapshot/restore callbacks for module and per-window
state. Snapshot memory belongs to Shady, so it survives the old `.so` being
unmapped, and the new plugin receives the old schema version to migrate.

```text
stage a fresh copy of the new .so
        -> dlopen and validate name/capabilities/ABI
        -> snapshot old module + live-window state
        -> stop/destroy old instance
        -> allocate/init new state
        -> restore snapshots
        -> dlclose old .so
        -> start new instance
```

The staged copy sits beside the original plugin, or in a private temporary
file for read-only installations. A broken replacement is rejected before
the working plugin is torn down; if init or restore fails after teardown
begins, Shady re-inits the old plugin from the same snapshot before
reporting failure. A reload must keep every capability the plugin provided
and may not silently drop state. Event subscriptions, published values,
shortcuts and other resources belong to the shared object that created them
and are removed before `dlclose()`; an unload or reload requested from an
event callback is deferred to the idle phase so the current dispatch
finishes first. `examples/plugins/counter.c` is the reference V2 plugin.

## Tests

| Command | Covers |
|---|---|
| `meson test -C build` | unit tests: math, physics collision, config, module resolver, plugin contracts |
| `./tests/spatial-suite.sh` | the 3D stack under ASan/UBSan with software GLES2: FPS mode, plugins, IPC, shortcuts, layer effects, the shell, Neon Transit and Afterglow |
| `./tools/daily-driver.sh all` | the 2D path under ASan/UBSan: two headless outputs, output removal with a live window, window churn, an idle-render check |
| `nix flake check` | NixOS VM tests: the session and the greeter start and show their UI |

The spatial suite builds `build-spatial-asan/`; single headless tests run
on their own, e.g. `SHADY_SPATIAL_TEST_BUILD_DIR=build-spatial-asan
./tests/headless-ipc.sh`. Writing such tests is described in
[HEADLESS_API.md](HEADLESS_API.md).

The idle-render check leaves a headless compositor untouched for a second
and fails if it keeps drawing: the renderer is demand-driven, waking on
commits, input, configuration changes and module actions, and asks for
continuous frames only while something moves (physics, FPS movement, wobble
settling, close animations). `SHADY_RENDER_STATS=1` logs per-output
scheduling, coalesced requests, frame callbacks, CPU frame timing and why
continuous frames were requested.

`./tools/test.sh` runs the development build nested with
`tools/test-shady.lua`, which binds a few scripting examples: F6 gravity,
F7 fold/unfold, F8 window dump, F9 respawn, `Ctrl+Alt+Q` quit.

## Legacy configuration

The original INI-style loader still works for compatibility:

```ini
physics_enabled = true
window_gravity = false
bind.fps_toggle = F2
bind.gravity_toggle = F4
```

Load one with `shady --legacy-config /path/to/config`; `-c` selects the
Lua `config.lua`, which is the primary path.
