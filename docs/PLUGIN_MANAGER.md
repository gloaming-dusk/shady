# Plugin manager

Native plugins are managed from Lua. Ask for a plugin by name with
`shady.plugins`. The plugin manager inside the compositor finds the shared
object, loads it, and keeps track of it. There is no separate binary and no
IPC.

```lua
-- config.lua
shady.plugins.path("~/.local/share/shady/plugins")  -- optional extra location
shady.plugins.load("water-windows")                 -- by name
shady.plugins.load("./build/libmy-plugin.so")       -- by path still works
shady.plugins.disable("window-motion")              -- keep a shipped plugin off
```

```lua
-- init.lua (runtime)
for _, p in ipairs(shady.plugins.list()) do
    shady.log(p.name .. " " .. p.state)
end
shady.plugins.reload("water-windows")
```

## Two phases

Shady runs two Lua states, and the API is split to match:

| Phase | Script | `shady.plugins` functions |
| --- | --- | --- |
| Bootstrap | `config.lua` (`-c`) | `path`, `load`, `enable`, `disable`, `list` |
| Runtime | `init.lua` and later | `list`, `reload`, `unload` |

Loading happens only in the bootstrap phase. Plugins must be registered
before modules are resolved, because their `requires`/`provides`
capabilities take part in dependency ordering. Loading new plugins at
runtime would require resolving dependencies again, so it is not supported
yet.

## Bootstrap API

### `shady.plugins.load(spec) -> true`

`spec` is one of:

| Form | Example | Resolves to |
| --- | --- | --- |
| Name | `"obj-loader"` | `libshady-plugin-obj-loader.so` on the search path |
| File name | `"libfoo.so"` | `libfoo.so` on the search path |
| Path (contains `/`) | `"./build/libfoo.so"`, `"~/x/libfoo.so"` | That file. A leading `~/` is expanded. |

Names may contain only letters, digits, `-`, `_` and `.`. Loading something
that is already loaded succeeds and does nothing. On failure, `load` raises
a Lua error and the compositor does not start, the same as `shady.plugin()`.
For an optional plugin, wrap the call:

```lua
if not pcall(shady.plugins.load, "my-optional-plugin") then
    shady.log("running without my-optional-plugin")
end
```

The plugin is still listed with state `failed`, so the failure stays visible
at runtime.

### `shady.plugins.path(dir)`

Add a search directory. User directories are searched first, in the order
they were added. The full search order is:

1. directories from `shady.plugins.path()`
2. `$SHADY_PLUGIN_PATH` (colon-separated)
3. the directory of the `shady` executable (the build tree during development)
4. `$XDG_DATA_HOME/shady/plugins` (default `~/.local/share/shady/plugins`)
5. the install directory, `libdir/shady/plugins`

The first match wins. When nothing matches, the log lists every directory
that was searched.

### `shady.plugins.enable(name)` / `shady.plugins.disable(name)`

`name` is the name you loaded with or the plugin's module name.

- For a **shipped plugin that has not loaded yet**, this decides whether it
  loads at all. A disabled shipped plugin is never `dlopen`ed.
- For a **loaded plugin**, this sets the module override, the same as
  `shady.module(name, enabled)`.

Unknown names raise an error.

### Shipped (default) plugins

Some features ship as plugins: currently `window-motion` and `obj-loader`.
They are loaded **after** `config.lua` finishes, unless the config disabled
them. Because of this, `config.lua` can:

- turn one off without loading it: `shady.plugins.disable("obj-loader")`
- replace it with a different build, by adding a search path that contains
  the same file name, or by calling `shady.plugins.load("/path/to/it.so")`
  first

The older module calls keep working for shipped plugins that have not loaded
yet. `shady.module("window-motion", false)`, `shady.modules({...})` and
`shady.has_module("obj-loader")` all reach the plugin manager.

`--safe` disables every shipped plugin. Plugins that `config.lua` loads
explicitly are still loaded.

## Runtime API

### `shady.plugins.list() -> Plugin[]`

Every plugin the manager knows about, in the order it was requested:

| Field | Meaning |
| --- | --- |
| `name` | Module name once loaded, otherwise the requested spec |
| `spec` | What was requested (`"counter"`, `"./build/libfoo.so"`) |
| `path` | Resolved shared object, if one was found |
| `builtin` | `true` for shipped plugins |
| `state` | See below |

| State | Meaning |
| --- | --- |
| `pending` | Shipped plugin, waiting for `config.lua` to finish (bootstrap only) |
| `disabled` | Shipped plugin that was turned off and never loaded |
| `failed` | Not found, or rejected while loading |
| `inactive` | Loaded, but its module is disabled or not initialized (yet) |
| `active` | Loaded and running |
| `unloaded` | Hot-unloaded at runtime |

### `shady.plugins.reload(name) -> boolean` / `shady.plugins.unload(name) -> boolean`

Hot reload or unload, using the same rules as before: reload migrates state
for ABI v2 plugins, unload requires a stateless plugin with no active
dependents. `name` may be the requested name (`"counter"`) or the module
name (`"counter-plugin"`). The older `shady.reload_plugin()` and
`shady.unload_plugin()` call the same code.

## Implementation

```text
config_lua.c / lua.c ──► plugin/manager_lua.c   (one binding, both phases)
                              │
                         plugin/manager.c        names, search paths, records,
                              │                  shipped defaults, status
                         plugin/plugin.c         dlopen, ABI checks, snapshots,
                                                 reload rollback (unchanged)
```

- `compositor/src/plugin/manager.c` keeps a record per requested plugin: the spec, the
  resolved path, the module name, whether it ships with Shady, and any
  enable/disable decision. The runtime state (`active`, `unloaded`, and so on)
  is read from the module manager, so it is never stored twice.
- `compositor/src/module/builtin.c` declares shipped plugins with
  `shady_plugin_manager_add_default()`. `compositor/src/main.c` calls
  `shady_plugin_manager_load_defaults()` after `config.lua` and safe-mode
  handling, and before modules are resolved.
- To ship a new plugin, build it as `libshady-plugin-<name>.so` into the
  build directory and `libdir/shady/plugins`, then add one
  `shady_plugin_manager_add_default(server, "<name>")` line behind its
  meson feature define.

## Tests

`tests/headless-plugin-manager.sh`, part of `tests/spatial-suite.sh`, checks
the following:

- a shipped plugin disabled in `config.lua` is never loaded
- the legacy `shady.modules()` toggle reaches a pending shipped plugin
- name lookup through `shady.plugins.path()`
- repeated loads succeed and do nothing
- invalid names and missing files are rejected
- bootstrap and runtime states are reported correctly
- reload by requested name, unload by module name, and the old reload alias
