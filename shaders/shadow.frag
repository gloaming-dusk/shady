precision mediump float;
uniform float u_softness;
uniform float u_opacity;
uniform vec4 u_floor_bounds;
varying vec2 v_uv;
varying vec2 v_shadow_xz;
void main() {
    if (v_shadow_xz.x < u_floor_bounds.x || v_shadow_xz.x > u_floor_bounds.y || v_shadow_xz.y < u_floor_bounds.z || v_shadow_xz.y > u_floor_bounds.w) discard;
    float edge = min(min(v_uv.x, 1.0-v_uv.x), min(v_uv.y, 1.0-v_uv.y));
    float feather = smoothstep(0.0, u_softness, edge);
    float core = smoothstep(0.0, u_softness * 2.2, edge);
    float alpha = u_opacity * mix(0.48, 1.0, core) * feather;
    gl_FragColor = vec4(0.0, 0.0, 0.0, alpha);
}
