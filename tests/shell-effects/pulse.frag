// Widget shader: grey that changes every frame.
uniform float u_time;
void main() {
    float v = fract(u_time * 0.77);
    gl_FragColor = vec4(v, v, v, 1.0);
}
