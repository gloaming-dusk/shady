# C Plugin API

Public plugin headers:

```c
#include <shady/plugin.h>
#include <shady/event.h>
#include <shady/module.h>
```

Plugins are shared libraries loaded at runtime.

## ABI entry points

### ABI v1

```c
const struct shady_module *shady_plugin_entry_v1(
    uint32_t host_abi,
    const struct shady_plugin_api_v1 *api,
    shady_host host);
```

A plugin should reject unsupported ABI versions and validate `api->struct_size` before using append-only fields.

### ABI v2

V2 wraps a module descriptor and adds state migration callbacks for hot reload:

```c
const struct shady_plugin_v2 *shady_plugin_entry_v2(
    uint32_t host_abi,
    const struct shady_plugin_api_v1 *api,
    shady_host host);
```

`struct shady_plugin_v2` provides module/window snapshot size, save, and restore callbacks plus a schema version.

## Module descriptor

`struct shady_module` defines:

- `name`
- `provides`
- `requires`
- `optional_requires`
- `state_size`
- `toplevel_state_size`
- lifecycle callbacks
- window lifecycle callbacks
- input hooks
- picking hook
- movement hook
- per-frame `tick`

Lifecycle order is conceptually:

```text
load -> init -> start -> ... -> stop -> destroy -> unload
```

Use `state_size` for private per-module state and `toplevel_state_size` for per-window state. Retrieve them through:

```c
api->module_state(host, "my-module");
api->window_state(window, "my-module");
```

## Capabilities

Declare capabilities with null-terminated string arrays:

```c
static const char *provides[] = {
    "my.effect",
    NULL,
};

static const char *requires[] = {
    "spatial.window-state",
    NULL,
};
```

At runtime:

```c
api->has_capability(host, "spatial");
```

## Windows

Enumeration:

```c
size_t n = api->window_count(host);
shady_window w = api->window_at(host, i);
```

Inspection includes title, app ID, mapped/visible/maximized/fullscreen state, workspace, size and position.

Actions include focus, close, maximize, fullscreen, moving across workspaces, and changing XYZ position.

Opaque handles must never be dereferenced by plugins.

## Outputs, seat, modules, workspaces

The API provides enumeration and inspection helpers for outputs and modules, the current seat, workspace listing/switching, and per-output render scheduling.

Use `api->schedule_render(host)` when plugin state changes in a way that requires all outputs to redraw.

## Events

Subscribe with:

```c
api->subscribe_event(host, SHADY_EVENT_WINDOW_MAPPED, on_event, user_data);
```

or obtain a removable subscription handle:

```c
shady_subscription_id id =
    api->subscribe_event_handle(host, SHADY_EVENT_WINDOW_MAPPED,
                                on_event, user_data);

api->unsubscribe_event(host, id);
```

Events are defined in `include/shady/event.h`.

Plugin-owned subscriptions are also cleaned up when the plugin unloads.

## Window visual state

The public API exposes compositor-managed window effect controls:

- water deformation: `window_set_water_effect`
- water surface lighting: `window_set_water_surface`
- border override/reset
- close-effect override/reset

These setters change compositor-owned state; they do not expose raw internal structs.

## Render and shader API

Shader and render APIs are host-owned. Plugins receive numeric handles rather than raw OpenGL object ownership.

Create/destroy programs:

```c
shady_shader_program program = api->shader_program_create(
    host, "plugin/shaders/effect.vert", "plugin/shaders/effect.frag");

api->shader_program_destroy(host, program);
```

Uniform helpers:

- `shader_uniform_float`
- `shader_uniform_int`
- `shader_uniform_vec2`
- `shader_uniform_vec4`

Fullscreen render hooks:

```c
static void draw(shady_host host,
                 const struct shady_render_context *ctx,
                 void *data) {
    api->shader_uniform_float(host, program, "u_time", ctx->time_seconds);
    api->shader_draw_fullscreen(host, program);
}

hook = api->render_hook_add(
    host, SHADY_RENDER_STAGE_OVERLAY, draw, NULL);
```

Stages:

- `SHADY_RENDER_STAGE_AFTER_BACKGROUND`
- `SHADY_RENDER_STAGE_BEFORE_WINDOWS`
- `SHADY_RENDER_STAGE_AFTER_WINDOWS`
- `SHADY_RENDER_STAGE_OVERLAY`

Remove with `render_hook_remove`.

## Spatial window representations

FPS interaction and folded-window geometry are separate concerns. A plugin can assign
an optional 3D representation to a window:

```c
struct shady_window_representation rep = {
    .struct_size = sizeof(rep),
    .kind = SHADY_WINDOW_REPRESENTATION_BOX,
    .width = 0.16f,
    .height = 0.16f,
    .depth = 0.16f,
    .hide_titlebar = true,
};

api->window_set_representation(host, window, &rep);
```

The representation is used while the window is folded in first-person mode. Rendering,
ray picking, debug geometry, held-window movement and physics collision all consume the
same representation contract.

For a static shape use:

- `window_set_representation`
- `window_reset_representation`
- `window_representation`

`SHADY_WINDOW_REPRESENTATION_BOX` uses world-space width/height/depth. A cube is only
one possible box; a plugin can use a flattened or stretched box without changing FPS
input code.

### Dynamic provider API

For state-dependent geometry or custom collision policy, install a provider:

```c
static bool model(shady_host host, shady_window window,
        const struct shady_representation_context *ctx,
        struct shady_representation_model *model,
        void *state, void *data) {
    if (ctx->held) {
        model->width *= 1.1f;
        model->height *= 0.8f;
    }
    return true;
}

static const struct shady_window_representation_provider provider = {
    .struct_size = sizeof(provider),
    .base = {
        .struct_size = sizeof(provider.base),
        .kind = SHADY_WINDOW_REPRESENTATION_BOX,
        .width = 0.24f,
        .height = 0.065f,
        .depth = 0.13f,
        .hide_titlebar = true,
    },
    .model = model,
};

api->window_set_representation_provider(host, window, &provider);
```

`shady_representation_context` supplies logical output size, client size, the default
world-space center and tilt, plus interaction state (`first_person`, `folded`, `held`,
`focused`). The callback must not retain pointers to the context or output structures.

The model callback receives a valid base-derived model first. It may modify center,
width/height/depth, tilt, and titlebar visibility. Returning `false`, returning invalid
sizes, or omitting the callback falls back to the provider's base representation.

A collision callback can override the axis-aligned physical body:

```c
static bool collision(shady_host host, shady_window window,
        const struct shady_representation_context *ctx,
        struct shady_collision_box *box,
        void *state, void *data) {
    box->half[1] *= 0.9f;
    return true;
}
```

If no collision callback is supplied, Shady derives the collision box directly from
the resolved model or active mesh bounds. Provider collision is authoritative for the
legacy AABB body used by broad-phase physics, held-window sweeps, and collision debug
rendering. Renderer and ray picking use the resolved model.

For a tighter physical body, a provider may additionally expose a closed convex hull:

```c
static bool collision_hull(shady_host host, shady_window window,
        const struct shady_representation_context *ctx,
        struct shady_collision_hull *hull,
        void *state, void *data) {
    hull->struct_size = sizeof(*hull);
    hull->vertices = my_vertices;
    hull->vertex_count = my_vertex_count;
    hull->indices = my_indices;
    hull->index_count = my_index_count;
    hull->revision = revision;
    return true;
}
```

`shady_collision_vertex` contains local `x/y/z` coordinates in the same representation
space consumed by `shady_window_box_model`: window-style shapes normally use `x/y` in
`[0,1]` and `z` in `[-1,0]`. The index buffer is required, uses `uint16_t`, and must
describe a closed convex triangle surface. The host currently accepts up to 256 hull
vertices and 1,536 hull indices.

When a valid hull is present, its transformed world-space bounds become the physics
broad-phase AABB. Against authored world triangles, Shady then runs hull-vs-triangle SAT
using the triangle normal, hull face normals, and edge cross-product axes. This removes
AABB false positives while retaining the existing fast box sweep for coarse world box
colliders and floor support. The same convex movement helper is used while an FPS window
is held, so free-fall/throw physics and grab movement cannot disagree about the physical
shape. Debug collision rendering also draws the actual convex triangle edges when a hull
is present; providers without a hull continue to show the resolved collision box.

The hull therefore takes precedence over the `collision()` box callback for the actual
convex body; `collision()` remains the compatibility and box-only path when no hull is
supplied.

Hull callbacks are synchronous and follow the same ownership/state/hot-reload rules as
`model`, `mesh`, and `collision`. `revision` follows the same rule as mesh revision: bump
it whenever hull vertices, indices, or their meaning changes. Shady caches validated
hull topology and the expensive scale/tilt transform by revision and shape parameters.
Pure world-space translation does not invalidate that cache; the cached center-relative
hull is simply translated to the window's current center. Provider detach, replacement,
window destruction, and hot reload clear all representation caches.

For non-convex physical shapes, a provider may expose a compound made from convex parts:

```c
static bool collision_compound(shady_host host, shady_window window,
        const struct shady_representation_context *ctx,
        struct shady_collision_compound *compound,
        void *state, void *data) {
    compound->struct_size = sizeof(*compound);
    compound->parts = parts;
    compound->part_count = part_count;
    compound->revision = revision;
    return true;
}
```

A compound contains 1–8 `shady_collision_hull` parts in the same representation-local
coordinate system. The total budget across all parts is 256 vertices and 1,536 indices.
Each part must still be a closed convex indexed triangle surface. `compound.revision`
must change whenever any part's geometry, topology, pointer meaning, or membership
changes.

`collision_compound()` takes precedence over `collision_hull()`. If it is absent or
returns no valid compound, Shady automatically promotes a valid single hull to a
one-part compound. Physics, held-window movement, and debug visualization therefore use
one resolved compound path regardless of which provider API was used.

The broad-phase body is the union AABB of all parts, while authored world triangles are
tested against each convex part separately. Empty space between disjoint parts remains
empty instead of collapsing into the union AABB. Compound topology and scale/tilt
transforms are revision-cached; translation alone reuses the same cached center-relative
parts.

The bundled `fps-cube` plugin deliberately exposes both APIs: an 8-vertex single hull as
a fallback and a two-part compound whose two half-cubes union to the same visible cube.
This exercises single-hull compatibility, multi-part physics, caching, debug rendering,
and hot reload without changing the visible shape.

### Per-window provider state and animation

Providers can ask Shady to allocate zeroed state independently for every window:

```c
struct spring_state {
    float value;
    float velocity;
};

static bool state_init(shady_host host, shady_window window,
        void *state, void *data) {
    struct spring_state *s = state;
    s->value = 0.2f;
    return true;
}

static bool update(shady_host host, shady_window window, float dt,
        void *state, void *data) {
    struct spring_state *s = state;
    s->velocity += (0.0f - s->value) * 50.0f * dt;
    s->velocity *= 0.85f;
    s->value += s->velocity * dt;
    return true; /* request another frame */
}

static const struct shady_window_representation_provider provider = {
    .struct_size = sizeof(provider),
    .base = { /* ... */ },
    .state_size = sizeof(struct spring_state),
    .state_init = state_init,
    .update = update,
    .model = model,
};
```

`state_size` may be zero. When non-zero, Shady allocates and zeroes the memory before
`state_init`. `state_init` returning `false` rejects the provider attachment. Optional
`state_destroy` runs before the window disappears, the provider is reset/replaced, or
the owning plugin DSO is unloaded/reloaded, after which the host frees the state.

`update(dt)` is called at most once per compositor simulation tick for each mapped
window. Returning `true` asks Shady to schedule another frame; returning `false` allows
the compositor to become idle when nothing else is animating. Geometry and collision
callbacks may read or modify the same state, but time integration belongs in `update`
because model/picking/collision resolution can happen multiple times per frame.

An event callback can access the provider's host-owned state with
`window_representation_state(host, window, &provider, &size)`. The same provider
descriptor pointer used for installation is required.

Provider callbacks run synchronously on Shady's compositor thread. They should be fast,
non-blocking, and must not call OpenGL directly. Render shaders remain a separate API.
The provider descriptor itself is also the ownership anchor: keep it in plugin-owned
static/storage for as long as it is installed, and pass the same descriptor pointer to
`window_reset_representation_provider`. Shady verifies that the descriptor and callbacks
belong to the same loaded DSO. Provider callbacks and user data are detached before the
owning shared object is unloaded or replaced during hot reload.

Only one plugin may own a representation/provider for a window at a time. Replacing a
static representation with a provider (or vice versa) is allowed for the same plugin;
a different plugin's active representation is not overwritten.

The repository includes three provider examples:

- `examples/plugins/fps_cube.c` — fixed 0.16 × 0.16 × 0.16 model with an explicit collision callback.
- `examples/plugins/fps_squash.c` — flattened body with host-owned per-window state and dynamic model/update callbacks.
- `examples/plugins/fps_jelly.c` — spring state integrated in `update(dt)` and used to deform both the visible model and its derived collision body.

This API is plugin-owned. The FPS module itself no longer decides that folded windows
must be cubes.

### Mesh and deformable representations

`SHADY_WINDOW_REPRESENTATION_MESH` lets a provider replace the folded front surface
with plugin-owned triangle geometry while keeping the same model/collision/lifecycle
contract:

```c
static bool mesh(shady_host host, shady_window window,
        const struct shady_representation_context *ctx,
        struct shady_representation_mesh *out,
        void *state, void *data) {
    struct my_state *s = state;
    out->struct_size = sizeof(*out);
    out->vertices = s->vertices;
    out->vertex_count = s->vertex_count;
    out->indices = s->indices;           /* optional uint16 triangle indices */
    out->index_count = s->index_count;   /* multiple of 3 when indices != NULL */
    out->revision = s->revision;
    return true;
}
```

Mesh vertices are `struct shady_representation_vertex { x, y, z, u, v; }`. `x/y/z`
control local geometry while `u/v` are independent client-texture coordinates. Position
and UV therefore do not need to match. If `indices == NULL`, vertices form an unindexed
triangle list and `vertex_count` must be a multiple of three. If `indices != NULL`,
`index_count` must be a multiple of three and every `uint16_t` index must be smaller than
`vertex_count`.

The public API is indexed even though the GLES2 host currently expands indexed topology
into a streaming triangle list before upload. This avoids relying on optional 32-bit
index extensions while allowing plugins to store shared vertices compactly.

The host consumes exactly the same topology for GPU rendering and ray-triangle picking.
Picking barycentrically interpolates each vertex's independent `u/v`, so clicks on a bent
or UV-remapped surface still map back to the intended client coordinates.

`revision` must change whenever vertex contents, index contents, or either buffer's
meaning changes. Keeping the same revision tells Shady that the geometry behind the
returned pointers is unchanged. The host uses this to reuse topology validation and may
reuse other derived data; mutating buffers without bumping `revision` is a provider
contract violation.

The current renderer still uploads mesh triangles as streaming data, but CPU-side
validation is revision-aware. This keeps the API ready for per-window GPU buffer caching
without another ABI change.

Mesh providers still resolve a `shady_representation_model`, which supplies the world
transform and nominal width/height/depth scale. If no `collision` callback is supplied,
Shady transforms the active mesh vertices into world space and derives a conservative
axis-aligned collision box from their actual bounds. A custom collision callback can
still override that result when a different physical body is desired.

The host currently accepts up to 16,384 vertices and 49,152 indices per window
representation. A mesh
callback that fails validation falls back to the normal representation path rather than
calling into invalid geometry.

`examples/plugins/fps_folded_paper.c` demonstrates the full deformable flow: 81 shared
vertices, a 384-index grid, independent UVs, host-owned per-window mesh state,
`update(dt)` deformation, mesh revision updates, mesh-derived collision bounds, GPU
rendering, and triangle-accurate picking.

## Per-window shader replacement

A plugin can replace the renderer used for one window:

```c
api->window_set_shader(host, window, program);
api->window_reset_shader(host, window);
```

Query the current handle with `window_shader(window)`.

Resources are associated with the plugin that created them. On plugin unload/hot reload, Shady removes its hooks, window shader associations, and shader programs.

See [SHADER_API.md](SHADER_API.md) for the shader contract.

## Hot reload

Stateless plugins can reload directly.

Stateful plugins should use ABI v2 snapshot/restore callbacks. Snapshot memory is host-owned so it remains valid while the old shared object is closed and the replacement is loaded.

Reload is rejected if capability/dependency contracts become incompatible.

## Minimal plugin

```c
#include <stddef.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;

static bool init(struct shady_server *server) {
    (void)server;
    api->log(SHADY_PLUGIN_LOG_INFO, "hello from plugin");
    return true;
}

static const char *provides[] = {"example.hello", NULL};

static const struct shady_module module = {
    .name = "hello",
    .provides = provides,
    .init = init,
};

const struct shady_module *shady_plugin_entry_v1(
        uint32_t abi,
        const struct shady_plugin_api_v1 *host_api,
        shady_host host_handle) {
    if (abi != SHADY_PLUGIN_ABI_V1 || !host_api)
        return NULL;

    api = host_api;
    host = host_handle;
    return &module;
}
```

See `examples/plugins/` for larger examples.
