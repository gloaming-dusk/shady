precision mediump float;
uniform float u_time;
uniform vec2 u_resolution;
varying vec2 v_uv;
void main() {
    float scan = 0.5 + 0.5 * sin((v_uv.y * u_resolution.y + u_time * 42.0) * 0.12);
    float vignette = 1.0 - smoothstep(0.25, 0.78, distance(v_uv, vec2(0.5)));
    float alpha = 0.018 + scan * 0.012 * vignette;
    gl_FragColor = vec4(0.08, 0.75, 1.0, alpha);
}
