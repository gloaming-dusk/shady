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

Shady binds attribute location 0 to `a_pos`. Window shaders may also declare `a_uv` at
attribute location 2. For normal BOX rendering, `a_uv` is unused and `u_use_vertex_uv`
is `0`. For MESH representations, `a_uv` contains the plugin-provided texture UV and
`u_use_vertex_uv` is `1`.

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

### Post-processing the scene

`shader_draw_fullscreen_scene` draws the same way, but first copies what the
output shows so far into a texture:

| Uniform | Type | Meaning |
|---|---|---|
| `u_scene` | sampler2D | everything drawn before this call, on texture unit 0 |
| `u_scene_size` | vec2 | that texture's size in pixels |

```glsl
uniform sampler2D u_scene;
uniform vec2 u_scene_size;

void main() {
    vec2 uv = gl_FragCoord.xy / u_scene_size;  /* unchanged image */
    uv += 0.01 * sin(uv.yx * 40.0);            /* ...or warp it */
    gl_FragColor = vec4(texture2D(u_scene, uv).rgb, 1.0);
}
```

```c
if (SHADY_API_HAS(api, shader_draw_fullscreen_scene))
    api->shader_draw_fullscreen_scene(host, program);
```

- It works only inside a render hook and returns false anywhere else. The
  stage chooses what the scene contains: `SHADY_RENDER_STAGE_AFTER_WINDOWS`
  sees the background, floor, environment and windows.
- Output alpha 1 replaces the pixel. Blending is the same as for
  `shader_draw_fullscreen`.
- The texture is RGB only.
- Each call copies the whole output once, so draw only while the effect is
  visible. `examples/plugins/black_hole.c` stops calling it once its
  aftershock has decayed.

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

A plugin may also assign another live window as an auxiliary shader source with `window_set_shader_source`. When that source is renderable, Shady binds its current client surface to texture unit 1 as `u_portal_tex`, sets `u_portal_available` to 1, and supplies its pixel size through `u_portal_size`. If no live source is available, `u_portal_available` is 0 and shaders must ignore `u_portal_tex`.

If either client texture is an external EGL texture, Shady copies it to an ordinary 2D texture before the custom shader runs. This extra copy occurs only on the custom-shader path.

### Host-provided uniforms

If declared by the shader, Shady automatically supplies:

| Uniform | Type | Meaning |
|---|---|---|
| `u_tex` | sampler2D | current client content on texture unit 0 |
| `u_portal_tex` | sampler2D | optional live source-window content on texture unit 1 |
| `u_portal_available` | float | 1 when the auxiliary source is live/renderable, otherwise 0 |
| `u_portal_size` | vec2 | auxiliary source texture width/height in pixels |
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
| `u_frame_px` | vec2 | decorated frame (title bar + client) size in logical px; `0,0` when the rounded frame does not apply |
| `u_frame_shape` | vec2 | rounded-frame corner radius and border width, both in px |
| `u_params` | vec4[4] | per-window values set by the shader's owner with `window_set_shader_params`; zero otherwise |

Uniforms are optional. Shady checks whether each uniform exists before assigning it.

### Rounded frames

Free-floating decorated windows are drawn as one rounded rectangle
(`window_corner_radius`) whose outline runs around the title bar and client
together. Shaders opt in by declaring `u_frame_px`; Shady then also blends opaque
clients so the shader can anti-alias the corners. `u_frame_px` is `0,0` for
maximized/fullscreen windows, folded and MESH representations, in which case the
legacy `u_border_width` client border applies. When the rounded frame is active,
`u_border_width` is still supplied for the client pass but shaders should draw the
outline from `u_frame_shape.y` instead, so it continues across the title bar pass.

`shaders/window.frag` contains a reference `frame_distance()` helper: it maps the
pass's `v_uv` into frame coordinates through `u_frame_rect`, evaluates a rounded-box
signed distance in px, discards fragments outside it and scales premultiplied colour
by the anti-aliased coverage. Shaders that do not declare `u_frame_px` keep
rendering square windows.

### Vertex coordinates

`a_pos` is a 3-component local geometry position. MESH-aware shaders can additionally
use `a_uv` as an independent 2-component client-texture coordinate. The host sets
`u_use_vertex_uv` to select whether separate UVs are active.

For frame-aware effects, follow the same pattern as the built-in shader:

```glsl
attribute vec3 a_pos;
attribute vec2 a_uv;
uniform float u_use_vertex_uv;

vec2 local_pos = a_pos.xy;
vec2 local_uv = mix(local_pos, a_uv, step(0.5, u_use_vertex_uv));
vec2 frame_pos = u_frame_rect.xy + local_pos * u_frame_rect.zw;
vec3 pos = vec3(frame_pos, a_pos.z);
```

Use `local_uv` for texture sampling and `frame_pos`/`a_pos` for geometry. This distinction
allows a deformable mesh to move vertices without forcing its texture UVs to move with
them.

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
