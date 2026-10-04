# Core and plugin boundaries

The core owns Wayland objects, window lifetime, input routing, output scheduling,
GPU resources, and validation of data crossing the plugin boundary. Optional
features own their algorithms and private simulation state. Renderer consumers
use published visual data rather than interpreting a plugin's state layout.

## Plugin host

The implementation is split by responsibility:

- `src/plugin/manager.c`: the user-facing layer. It resolves names through
  search paths, keeps a record per requested plugin, and loads shipped default
  plugins after `config.lua`. `manager_lua.c` exposes it as `shady.plugins` to
  both Lua states. See [Plugin manager](PLUGIN_MANAGER.md).
- `src/plugin/plugin.c`: shared-object loading, contract checks, snapshots,
  migration, reload rollback, and deferred lifecycle actions.
- `src/plugin/host_api.c`: opaque-object access and the backwards-compatible
  umbrella API, including feature-table lookup.
- `src/plugin/representation.c`: provider lifetime, shape validation, collision
  transforms, and representation queries.
- `src/plugin/motion.c`: motion-driver ownership, command validation, and visual
  publication. It contains no spring or drag simulation.
- `src/plugin/environment.c`: resolves the calling plugin for the
  `shady.environment` table and forwards to the core environment service.

`shady_toplevel` holds an opaque representation pointer. The representation
subsystem allocates descriptors when a plugin sets a shape or attaches a
provider. Large geometry caches are allocated separately on the first mesh or
collision query. Static box overrides do not allocate those caches. Detach and
reset release unused storage; window destruction releases everything. Ordinary
windows do not carry the large hull and compound arrays.

Mesh upload, validation, and GPU-resource cleanup remain host responsibilities:
plugins cannot leave callbacks pointing into an unloaded shared object or pass
unchecked geometry directly to the renderer.

## Public feature APIs

`shady/plugin.h` remains an umbrella header for existing V1/V2 plugins. Public
object handles live in `shady/types.h`; representation types and the grouped
representation API live in `shady/representation.h`. Existing field offsets in
`shady_plugin_api_v1` and the layout of `shady_module` are unchanged.

New plugins check the append-only `query_api` member before using it:

```c
#include <shady/plugin.h>
#include <shady/motion.h>

if (!SHADY_API_HAS(api, query_api)) return NULL;
const struct shady_motion_api_v1 *motion =
    api->query_api(host, SHADY_MOTION_API, SHADY_MOTION_API_VERSION);
if (!motion || motion->struct_size < sizeof(*motion)) return NULL;
```

Names and versions identify a feature contract. Unknown names and unsupported
versions return `NULL`. Returned tables are immutable, host-owned, and remain
valid for the compositor's lifetime. A non-NULL table describes available API
operations; it does not mean an optional driver is active.

| API name | Version | Purpose |
| --- | --- | --- |
| `shady.window-motion` | 1 | Driver attachment, impulses, reset/damping, and visual data |
| `shady.window-representation` | 1 | Static shape overrides and dynamic model/mesh/collision providers |
| `shady.environment` | 1 | Asset-loader registry and procedural static scenes |

The old representation functions remain available in the umbrella table. Both
routes use the same implementation and ownership checks.

## Window motion plugin

`examples/plugins/window_motion.c` is the implementation of `window-motion`.
It is built as `libshady-plugin-window-motion.so` and uses only installed public
headers and opaque host APIs. It owns per-window spring velocities, accumulated
wobble/tilt, and drag history. The host stores only the published wobble, tilt,
and animation-demand flag, shared by rendering, picking, and physics.

The host allows one motion driver. Its immutable descriptor and callbacks must
belong to the same shared object. Other plugins may issue impulses, reset, and
damping commands through the feature API, but only the attached owner may
publish visual data. Commands validate window handles and reject non-finite
numbers. Detach clears the published state, and loader cleanup removes the
driver before `dlclose`.

With `-Dwindow_motion=enabled`, the plugin manager loads the shipped plugin
after `config.lua`, unless the config disabled it. In that case it is never
loaded. `shady.module("window-motion", ...)` keeps working, and capability
resolution is unchanged. The plugin is found through the manager's search path:
the executable's directory in the build tree, and `libdir/shady/plugins` once
installed. Ninja builds the plugin when building `shady`.
`-Dwindow_motion=disabled` omits the shipped plugin. The small core adapters
remain available and behave neutrally without a driver, so a separately loaded
plugin can still provide motion through the public feature API.

The plugin uses ABI V2 with schema 1. Reload snapshots spring and drag state,
detaches the old driver, initializes the new driver, and restores each window's
state and published visual data. Stateful hot unload still follows the loader's
existing restrictions.

## Environment and asset loaders

The core has no model-format parsers. `src/modules/environment/` is a
format-neutral service, created and destroyed with the spatial module:

- `environment.c`: lifetime, config reconciliation (`environment`,
  `environment_path`), scene ownership, and plugin cleanup.
- `loaders.c`: the extension-to-loader registry. Descriptors and callbacks must
  belong to the registering shared object.
- `scene.c`: validation, the deep copy (de-indexed for GLES2), and rebuilding
  the spatial world as the default floor plus the scene's collision.
- `draw.c` and `sky.c`: GL upload/draw of the scene mesh, and the PPM sky.

Rendering calls `shady_environment_sync()` once per frame. A changed path, or a
newly registered or removed loader, triggers a load through the matching loader.
The loader returns borrowed arrays. The host copies them, then calls the
loader's `release()`. Loader cleanup runs before `dlclose`. It removes the
plugin's loaders and clears a scene that plugin owns. The visual mesh is never
used as collision.

The OBJ parser lives in `loaders/obj/obj_loader.c` and uses only public headers.
With `-Dobj_loader=enabled`, the plugin manager loads the shipped
`libshady-plugin-obj-loader.so` as a default plugin, the same way as window
motion. Further formats (glTF, ...) go in `loaders/<format>/`. See
[Environment API](ENVIRONMENT_API.md).

## Initialization and validation

A module whose `init` returns false must undo its partial initialization itself.
The manager calls `destroy` only for successfully initialized modules. Desktop
protocol initialization uses its cleanup path on failure. Core lists are
initialized before any plugin entry point can access the host.

`meson test` includes a public-API motion test that unloads/reopens the shared
object and checks state migration, ABI rejection, damping, drag, reset, and
wobble-disabled behavior. `tests/headless-motion.sh` checks the real host's
ownership and numeric validation and reloads the driver with a live animated
window. `tests/headless-representation-lifetime.sh` exercises cube, indexed mesh,
and animated compound geometry through reload and window destruction.
`meson test obj-loader` checks the OBJ loader plugin against a fake host, and
`tests/headless-environment.sh` drives the real host through loader reload,
unhandled extensions, and unload. The
spatial suite runs these alongside existing rendering regressions.

Physics, FPS controls, close-animation orchestration, and scene effects remain
built-in modules. The motion split demonstrates the public-driver boundary for
further migrations; it does not make those other algorithms independent
plugins yet. Renderer-owned resources and Wayland lifecycle must stay coherent
as additional features move out of the core.
