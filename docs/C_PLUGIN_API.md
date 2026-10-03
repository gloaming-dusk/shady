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
