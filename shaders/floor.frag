#extension GL_OES_standard_derivatives : enable
precision mediump float;
varying vec2 v_world;
uniform vec3 u_base_color;
uniform vec3 u_grid_color;
uniform float u_grid_strength;
uniform float u_major_strength;
uniform float u_fade_start;
uniform float u_fade_end;
void main() {
    vec2 g = abs(fract(v_world * 10.0 - 0.5) - 0.5) / max(fwidth(v_world * 10.0), vec2(0.0001));
    float line = 1.0 - min(min(g.x, g.y), 1.0);
    vec2 major_g = abs(fract(v_world * 2.0 - 0.5) - 0.5) / max(fwidth(v_world * 2.0), vec2(0.0001));
    float major = 1.0 - min(min(major_g.x, major_g.y), 1.0);
    float fade = 1.0 - smoothstep(u_fade_start, max(u_fade_end, u_fade_start + 0.001), length(v_world));
    vec3 grid = u_grid_color * (line * u_grid_strength + major * u_major_strength);
    gl_FragColor = vec4(u_base_color + grid * fade, 1.0);
}
