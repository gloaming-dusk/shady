attribute vec2 a_pos;
varying float v_y;
void main() {
    v_y = a_pos.y * 0.5 + 0.5;
    gl_Position = vec4(a_pos, 0.999, 1.0);
}
