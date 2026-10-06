# Shell plugin API

`shady-shell` loads native plugins for what its Lua config cannot do well:
talk to system services, keep long-lived sockets open, draw custom
widgets. The header is [`include/shady/shell_plugin.h`](../include/shady/shell_plugin.h);
[`shell/plugins/sysinfo.c`](../shell/plugins/sysinfo.c)
is a complete plugin and [`shell/examples/sysinfo.lua`](../shell/examples/sysinfo.lua)
uses it.

Shell plugins are separate from compositor plugins
([C_PLUGIN_API.md](C_PLUGIN_API.md)). They run in the shell process: a
crashing shell plugin takes down the shell, never the compositor.

## Talking to Lua

A plugin never touches the Lua state. It exchanges three things with the
config:

| Plugin side | Lua side | |
|---|---|---|
| `api->set_value(host, "sysinfo.cpu", "23")` | `shell.value("sysinfo.cpu", fallback)` | strings the plugin publishes; changing one repaints the views |
| `api->add_action(host, "sysinfo.sample", fn, data)` | `shell.action("sysinfo.sample", argument)` | named callbacks; returns false in Lua if nobody registered the name |
| `api->add_widget(host, &type)` | `shell.widget("sysinfo.graph", {width = 60, height = 18, ...})` | widgets drawn by the plugin with cairo |

Prefix names with the plugin's name so plugins do not collide; a second
action or widget with the same name is refused.

## Loading

```lua
local ok, err = shell.plugin("sysinfo", { interval = 1000 })
```

A name is looked up as `libshady-shell-plugin-<name>.so` in
`$SHADY_SHELL_PLUGIN_PATH` (colon-separated), next to the config, in its
`plugins/` subdirectory, in the build directory and in
`<libdir>/shady/shell-plugins`. Anything containing a `/` is a path. The
table becomes the plugin's options.

A plugin is loaded once per shell process. Reloading the Lua config keeps
loaded plugins and their values, so a reload never restarts a service;
calling `shell.plugin` again for a loaded plugin just returns true. A
plugin that cannot be found or refuses to load makes `shell.plugin`
return false and a message; the rest of the config still runs.

## Writing a plugin

```c
#include <shady/shell_plugin.h>

static const struct shady_shell_api_v1 *api;
static shady_shell_host host;

static bool init(shady_shell_host h, const shady_shell_props *options) {
    double interval = api->prop_number(options, "interval", 1000);
    ...
    return true;
}

static const struct shady_shell_plugin plugin = {
    .name = "example",
    .init = init,
    .destroy = NULL,   /* optional */
};

const struct shady_shell_plugin *shady_shell_plugin_entry_v1(uint32_t host_abi,
        const struct shady_shell_api_v1 *host_api, shady_shell_host host_handle) {
    if (host_abi != SHADY_SHELL_PLUGIN_ABI_V1 || !SHADY_SHELL_API_HAS(host_api, theme_color))
        return NULL;
    api = host_api;
    host = host_handle;
    return &plugin;
}
```

Build it as a shared object against cairo, e.g.
`cc -shared -fPIC -Iinclude $(pkg-config --cflags --libs cairo) example.c -o libshady-shell-plugin-example.so`.

The entry point returns NULL to refuse a host it does not support. `init`
receives the options table and returns false to refuse loading; the
plugin is then destroyed. Every API call takes the `host` handle the entry
point received; it identifies the plugin.

## API

| Entry | |
|---|---|
| `log(host, message)` | written to the shell's log as `shady-shell: <plugin>: message` |
| `set_value(host, key, value)` / `value(host, key)` | publish or read a value; `NULL` removes it. Strings are copied. |
| `add_action(host, name, fn, data)` | `fn(argument, data)` runs on `shell.action(name, argument)` |
| `add_widget(host, &type)` | register `type.draw(data, cr, width, height, props)` under `type.name` |
| `timer_add(host, ms, fn, data)` / `timer_cancel` | one-shot timer in the shell's loop; re-add from the callback to repeat |
| `watch_add(host, fd, events, fn, data)` / `watch_remove` | call `fn(fd, revents, data)` when `poll(2)` reports `events` on `fd`; removing a watch from its own callback is fine |
| `prop_string` / `prop_number` / `prop_color` | read options or a widget's properties |
| `redraw(host)` | repaint every view (for widget data that changed without a value changing) |
| `theme_color(host, name, rgba)` | the shell palette: `accent`, `accent_2`, `accent_deep`, `surface`, `text`, `text_dim`, `danger` |

Timers, watches, values, actions and widgets a plugin registers are
released by the shell when the plugin is destroyed (at shell exit, or when
its `init` fails), so `destroy` only needs to free the plugin's own
resources, such as file descriptors it opened.

### Widgets

`draw` gets a cairo context translated to the widget's top-left corner,
in logical pixels, clipped to `width × height`, and the widget's
properties: every string, number and boolean field of the Lua table
(`width`, `height` and any keys the plugin defines). Size widgets with
`width`/`height` or let their container stretch them; a plugin widget has
no natural size. Widget shaders and layout properties work on plugin
widgets like on any other.

## Compatibility

`struct shady_shell_api_v1` is append-only: new entries are added at the
end, and `struct_size` tells a plugin how much of it the running shell
provides. Check any entry newer than the ones you require with
`SHADY_SHELL_API_HAS(api, member)` before calling it.
