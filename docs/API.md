# Shady API Documentation

Shady exposes four extension surfaces:

- [Lua API](LUA_API.md) — configuration, runtime scripting, events, window/workspace control.
- [Headless / Automation API](HEADLESS_API.md) — deterministic keyboard, pointer, timer, screenshot, and CI helpers.
- [C Plugin API](C_PLUGIN_API.md) — native modules, events, window/output access, hot reload, render hooks.
- [Shader API](SHADER_API.md) — standalone shader files, fullscreen passes, and per-window shader replacement.

## Which API should I use?

Use Lua when you are configuring Shady, writing window rules, reacting to compositor events, or scripting behavior without rebuilding a shared library.

Use the headless automation API for regression tests. It is part of the Lua runtime but is documented separately because its semantics are test-oriented.

Use the C plugin API when you need native performance, module state, input hooks, render hooks, custom window effects, or hot-reloadable extensions.

Use the shader API together with the C plugin API when a plugin owns custom GLSL.

## Build-time availability

Some APIs depend on enabled modules. Query runtime capabilities before using optional features:

```lua
if shady.has_capability("spatial") then
    shady.camera("yaw", 0.15)
end
```

C plugins use `api->has_capability(host, "...")` for the same purpose.

## Compatibility policy

Public C headers live under `include/shady/`. Existing fields in `struct shady_plugin_api_v1` are append-only: new API entries are added at the end instead of reordering earlier fields.

Lua APIs are runtime APIs and currently do not carry a numeric compatibility version.

Shader programs are GLES2-style GLSL. Plugins should treat host-provided uniforms as optional: only uniforms declared by the shader are assigned.
