# Environment and asset loader API

Shady can place a static 3D environment in the spatial scene: a visual mesh
drawn behind windows, plus authored collision that window physics collides
with. The core never parses a model format itself. Each file format is
handled by a **loader plugin** that registers for one or more file extensions.
OBJ ships as the first loader, and glTF or anything else can be added the same
way without touching the core.

```c
#include <shady/plugin.h>
#include <shady/environment.h>
```

## Configuration

| Key | Type | Meaning |
| --- | --- | --- |
| `environment` | bool | Enable the configured environment. |
| `environment_path` | string | Model file. Its extension selects the loader. |
| `environment_obj`, `environment_obj_path` | | Older names, kept as aliases of the two keys above. |

```lua
shady.set("environment", true)
shady.set("environment_path", "/home/me/rooms/test-room.obj")
```

The keys can change at runtime. The host reconciles the active scene with the
config once per spatial frame. If no loader handles the extension yet, the host
logs `environment: no loader registered for <path>` and keeps the world empty.
It loads the file as soon as a matching loader registers.

## Ownership model

```text
config: environment_path ──► core environment service ──► loader plugin (by extension)
                                    │      ▲                      │
                                    │      └── scene (borrowed) ◄─┘
                                    ├─► validated deep copy
                                    ├─► GL upload + draw (render)
                                    └─► spatial world colliders (physics, FPS)
```

- The **core** (`src/modules/environment/`) owns the active scene, the
  loader registry, GPU buffers, sky rendering and collision registration. It
  validates and deep-copies everything a plugin hands over.
- A **loader plugin** only parses. It returns a format-neutral
  `struct shady_environment_scene` and frees its own memory in `release()`.
- At most one scene is active. A scene loaded from config belongs to the
  loader's plugin. A procedural scene belongs to the plugin that called
  `set_scene()`.
- Unloading a plugin removes its loaders and clears the scene it owns. On a
  hot reload, the new instance registers again and the configured path is
  reloaded.

## Feature table

```c
#define SHADY_ENVIRONMENT_API "shady.environment"
#define SHADY_ENVIRONMENT_API_VERSION 1u
```

Query it in the plugin entry point, like other versioned feature tables. The
table is available only in builds with the spatial module:

```c
const struct shady_environment_api_v1 *env =
    api->query_api(host, SHADY_ENVIRONMENT_API, SHADY_ENVIRONMENT_API_VERSION);
if (!env || env->struct_size < sizeof(*env)) return NULL;
```

| Function | Purpose |
| --- | --- |
| `register_loader(host, loader)` | Register a loader. Returns a non-zero id, or 0 if the descriptor is invalid, spatial is unavailable, or one of its extensions is already claimed. |
| `unregister_loader(host, id)` | Only the registering plugin may call this. It clears the scene the loader produced. |
| `set_scene(host, scene)` | Install a procedural scene owned by the caller. Fails while another plugin's procedural scene is active. |
| `clear_scene(host)` | Clear the caller's procedural scene. The configured environment, if any, takes over again. |

Call `register_loader()` from your module's `init`, which runs after the
`spatial` module it depends on. Do not call it from the entry point.

## Scene format

```c
struct shady_environment_vertex   { float position[3], normal[3], uv[2]; };
struct shady_environment_box      { float min[3], max[3]; };
struct shady_environment_triangle { float v[3][3]; };

struct shady_environment_scene {
    uint32_t struct_size;
    const struct shady_environment_vertex *vertices; size_t vertex_count;
    const uint32_t *indices;                         size_t index_count;
    const struct shady_environment_box *boxes;       size_t box_count;
    const struct shady_environment_triangle *triangles; size_t triangle_count;
};
```

- Positions are in **world space**. A loader for a scene-graph format such as
  glTF flattens node transforms before submitting.
- The visual mesh is a triangle list. `indices` is optional. Without it,
  `vertex_count` must be a multiple of three. The host de-indexes the mesh into
  one GLES2 buffer, so 32-bit indices need no GL extension.
- `boxes` are coarse colliders used for support and broad phase. `triangles`
  are the exact narrow-phase geometry. Both are optional. The visual mesh is
  **never** used as collision. Hollow visual geometry would trap windows.
- Limits: the world holds 32 boxes, one of which is the floor, and 4096
  collision triangles. A scene over capacity, or one with non-finite values,
  out-of-range indices or inverted boxes, is rejected as a whole.
- The struct is append-only. Materials and textures will be added as new
  members at the end, gated by `struct_size`. Today the host shades the mesh
  with a single lit color and ignores `uv`.

## Writing a loader

```c
static bool my_load(shady_host host, const char *path,
        struct shady_environment_scene *scene, void *user_data) {
    /* parse path; allocate arrays; fill scene->vertices, ->boxes, ... */
    return true;              /* false: host logs and keeps no geometry */
}

static void my_release(shady_host host, struct shady_environment_scene *scene,
        void *user_data) {
    free((void *)scene->vertices);   /* free what my_load allocated */
    /* ... */
}

static const char *const extensions[] = { "gltf", "glb", NULL };

static const struct shady_environment_loader loader = {
    .struct_size = sizeof(loader),
    .name = "gltf",
    .extensions = extensions,
    .load = my_load,
    .release = my_release,
};

static shady_environment_loader_id loader_id;

static bool module_init(struct shady_server *server) {
    loader_id = env->register_loader(host, &loader);
    return loader_id != 0;
}

static void module_destroy(struct shady_server *server) {
    if (loader_id) env->unregister_loader(host, loader_id);
    loader_id = 0;
}

static const char *const requires[] = { "spatial", NULL };
static const char *const provides[] = { "environment-loader.gltf", NULL };
```

Rules:

- The loader descriptor and both callbacks must live in the registering
  shared object (`static const`). The host keeps referencing them and checks
  this with `dladdr`.
- Extensions are given without the dot and matched case-insensitively. Each
  extension can have only one loader.
- `load()` runs on the compositor thread, at registration or during a frame
  when the config changes. Keep it bounded: the compositor waits for it.
- `release()` is called only after a successful `load()`, and only once the
  host has copied the scene.
- Use `api->log()` for diagnostics. The host already logs a generic failure.

## Adding a loader to the tree

Loaders that ship with Shady live under `loaders/<format>/`:

```text
loaders/
  obj/obj_loader.c        Wavefront OBJ (shipped, auto-loaded)
  gltf/gltf_loader.c      ← a future loader goes here
```

1. Add the source under `loaders/<format>/`. It may include only public
   headers from `include/shady/`.
2. In `meson.build`, add a feature option (see `obj_loader` in
   `meson_options.txt`) and a `shared_module('shady-plugin-<format>-loader', …)`
   installed to `libdir/shady/plugins`. Also add it to `link_depends` of the
   `shady` executable and pass `-DSHADY_HAS_<FORMAT>_LOADER`.
3. In `src/module/builtin.c`, call
   `register_shipped_plugin(server, "libshady-plugin-<format>-loader.so")`
   under that define, and disable the module in safe mode (`src/main.c`).
4. Add a contract test like `tests/obj-loader.c`. It dlopens the plugin
   against a fake host table and checks the scenes it returns.

Third-party loaders don't need any of this. They load like any other plugin:

```lua
shady.plugin("/path/to/libmy-gltf-loader.so")
shady.set("environment_path", "/path/to/scene.glb")
```

## Bundled OBJ loader

`loaders/obj/obj_loader.c` builds `libshady-plugin-obj-loader.so` (module
`obj-loader`, provides `environment-loader.obj`). With
`-Dobj_loader=enabled`, the default, startup loads it before the bootstrap
config. The build-tree executable finds it next to itself; installed builds
look in `libdir/shady/plugins`.

Supported OBJ input:

- `v`, `vt` and `vn` lines.
- `f` faces with `v`, `v/vt`, `v//vn` or `v/vt/vn` references, using positive
  or negative indices. Polygons are fan-triangulated.
- Faces without normals get a flat normal from their winding order.
- `# comments` are allowed anywhere. `mtllib`, `usemtl`, `s` and other
  statements are ignored.

Collision is authored with groups or objects whose names start with
`collision_`:

```obj
g collision_room
v -0.75 -0.62 -1.60
v  0.75 -0.62 -1.60
v  0.75 -0.62 -0.40
f 1 2 3
```

Each `collision_*` group becomes one bounding box, and its triangles become
exact collision triangles. Collision faces are also drawn. To keep a proxy
invisible, give it its own file or make it match the visual shape, as
`assets/test-room.obj` does.

## Tests

- `meson test obj-loader`: the plugin contract, checked against
  `assets/test-room.obj` and generated edge cases.
- `tests/headless-environment.sh`, part of `tests/spatial-suite.sh`: the real
  compositor loads the configured OBJ at startup, reloads it across plugin hot
  reload, reports an unhandled extension, restores it, and drops it on unload.
