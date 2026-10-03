precision mediump float;
uniform vec3 u_top;
uniform vec3 u_horizon;
uniform vec3 u_bottom;
varying float v_y;
void main() {
    float horizon = smoothstep(0.18, 0.58, v_y);
    vec3 lower = mix(u_bottom, u_horizon, horizon);
    float upper_mix = smoothstep(0.48, 1.0, v_y);
    vec3 color = mix(lower, u_top, upper_mix);
    gl_FragColor = vec4(color, 1.0);
}
