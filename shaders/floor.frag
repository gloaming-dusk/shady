#extension GL_OES_standard_derivatives : enable
precision mediump float;
varying vec2 v_world;
uniform vec3 u_base_color;
uniform vec3 u_grid_color;
uniform float u_grid_strength;
uniform float u_major_strength;
uniform float u_fade_start;
uniform float u_fade_end;
uniform float u_fog; /* 0 = opaque floor, 1 = far floor fully melts away */

/* Floor plane extent (see shady_world_floor): fully faded before the edge. */
const float FLOOR_HALF_EXTENT = 6.0;

float dither() {
    return fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) - 0.5;
}

void main() {
    vec2 g = abs(fract(v_world * 10.0 - 0.5) - 0.5) / max(fwidth(v_world * 10.0), vec2(0.0001));
    float line = 1.0 - min(min(g.x, g.y), 1.0);
    vec2 major_g = abs(fract(v_world * 2.0 - 0.5) - 0.5) / max(fwidth(v_world * 2.0), vec2(0.0001));
    float major = 1.0 - min(min(major_g.x, major_g.y), 1.0);
    float dist = length(v_world);
    float fade = 1.0 - smoothstep(u_fade_start, max(u_fade_end, u_fade_start + 0.001), dist);
    /* Fine lines collapse into moire long before major lines do. */
    float fine_fade = fade * (1.0 - smoothstep(u_fade_start * 0.6, u_fade_end * 0.8, dist));
    vec3 grid = u_grid_color * (line * u_grid_strength * fine_fade + major * u_major_strength * fade);
    /* Faint pool of reflected grid light under the window field. */
    float pool = 1.0 - smoothstep(0.0, max(u_fade_end, 1.0) * 0.75, dist);
    vec3 color = u_base_color + u_grid_color * pool * pool * 0.045 + grid;

    /* Horizon fog fades the floor's coverage rather than mixing towards a
     * fixed colour, so the far floor dissolves into whatever was drawn behind
     * it: the gradient background, a panorama sky or a plugin sky hook. */
    float fog = u_fog * smoothstep(max(u_fade_start, 0.5) * 1.4, FLOOR_HALF_EXTENT * 0.95, dist);
    float alpha = 1.0 - fog;
    if (alpha <= 0.004) discard;
    color += dither() / 255.0;
    gl_FragColor = vec4(color * alpha, alpha);
}
