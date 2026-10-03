# Shader API

Shady uses file-based GLES shaders. Core shaders live in `shaders/`, and plugins are encouraged to keep shader files next to plugin source:

```text
examples/plugins/
├── my_effect.c
└── shaders/
    ├── my_effect.vert
    └── my_effect.frag
```

## Core shader files

The compositor's built-in renderer is also file-based. Examples include:

- `window.vert`, `window.frag`, `window_ext.frag`
- `side.vert`, `side.frag`
- `shadow.vert`, `shadow.frag`
- `floor.vert`, `floor.frag`
- `background.vert`, `background.frag`
- `titlebar.vert`, `titlebar.frag`
- `copy.vert`, `copy.frag`, `copy_ext.frag`
- `debug.vert`, `debug.frag`

Core shaders are loaded from `SHADY_SHADER_DIR`, defined by the build.

## Creating a plugin shader

```c
program = api->shader_program_create(host,
    "/absolute/path/effect.vert",
    "/absolute/path/effect.frag");
```

The host compiles and links the program. A zero handle means failure.

Shady binds attribute location 0 to `a_pos`.

## Fullscreen shaders

Fullscreen shaders are intended for render hooks.

A minimal vertex shader:

```glsl
attribute vec2 a_pos;
varying vec2 v_uv;

void main() {
    v_uv = a_pos * 0.5 + 0.5;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
```

Example fragment shader:

```glsl
precision mediump float;

uniform float u_time;
uniform vec2 u_resolution;
varying vec2 v_uv;

void main() {
    float pulse = 0.5 + 0.5 * sin(u_time);
    gl_FragColor = vec4(v_uv.x, v_uv.y, pulse, 0.15);
}
```

Set uniforms from the render callback and call:

```c
api->shader_draw_fullscreen(host, program);
```

The host preserves the important GL state around the draw.

## Per-window shaders

A window shader runs on Shady's subdivided window mesh and replaces the built-in window shader for that window.

Attach it:

```c
api->window_set_shader(host, window, program);
```

Detach it:

```c
api->window_reset_shader(host, window);
```

### Texture contract

The current window content is bound to texture unit 0 as a normal `GL_TEXTURE_2D` sampler named `u_tex`.

If the client texture is an external EGL texture, Shady copies it to an ordinary 2D texture before the custom shader runs. This extra copy occurs only on the custom-shader path.

### Host-provided uniforms

If declared by the shader, Shady automatically supplies:

| Uniform | Type | Meaning |
|---|---|---|
| `u_tex` | sampler2D | current client content |
| `u_mvp` | mat4 | full model-view-projection matrix |
| `u_model` | mat4 | window model matrix |
| `u_frame_rect` | vec4 | x, y, width, height sub-rect inside full frame coordinates |
| `u_time` | float | compositor shader time in seconds |
| `u_resolution` | vec2 | output buffer width/height |
| `u_window_size` | vec2 | logical client width/height |
| `u_has_alpha` | float | 1 for alpha content, 0 for forced opaque |
| `u_wobble` | vec2 | compositor wobble state |
| `u_water` | vec4 | amplitude, frequency, speed, phase |
| `u_water_surface` | vec4 | fresnel, specular, caustic, tint |
| `u_border_color` | vec4 | window border color |
| `u_border_width` | vec2 | normalized border thickness |
| `u_close_progress` | float | close animation progress |
| `u_close_effect` | vec4 | style, strength, direction x/y |
| `u_tint` | vec4 | compositor tint and global opacity |
| `u_effect_strength` | float | configured effect strength |
| `u_brightness` | float | configured/focus-adjusted brightness |
| `u_light_dir` | vec3 | compositor light direction |

Uniforms are optional. Shady checks whether each uniform exists before assigning it.

### Vertex coordinates

`a_pos` is a 3-component position from Shady's subdivided window mesh.

For frame-aware effects, follow the same pattern as the built-in shader:

```glsl
vec2 local_uv = a_pos.xy;
vec2 uv = u_frame_rect.xy + local_uv * u_frame_rect.zw;
vec3 pos = vec3(uv, a_pos.z);
```

This is important for windows with compositor title bars: the client and titlebar occupy sub-regions of the same logical frame.

### UV orientation

Client textures conventionally use:

```glsl
v_uv = vec2(local_uv.x, 1.0 - local_uv.y);
```

## Resource ownership

Shader programs are owned by Shady, associated with the plugin shared object that created them.

A plugin should normally destroy programs in `destroy()`, but Shady also cleans up remaining programs, hooks, and window associations during unload/hot reload.

A plugin cannot attach another plugin's shader handle to its window association.

## Fallback behavior

If a custom per-window shader is missing or cannot be used, Shady falls back to the built-in window shader.

## Reference examples

- `examples/plugins/shader_overlay.c` — fullscreen overlay shader and render hook.
- `examples/plugins/shaders/overlay.vert`
- `examples/plugins/shaders/overlay.frag`
- `examples/plugins/water_windows.c` — per-window shader association.
- `examples/plugins/shaders/water_window.vert`
- `examples/plugins/shaders/water_window.frag`

The water-window example is the best starting point for a plugin that deforms both geometry and application pixels.
