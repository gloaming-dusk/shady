attribute vec3 a_pos;
varying vec2 v_uv;
void main() {
    v_uv = a_pos.xy;
    gl_Position = vec4(
        a_pos.x * 2.0 - 1.0,
        a_pos.y * 2.0 - 1.0,
        0.0,
        1.0
    );
}
