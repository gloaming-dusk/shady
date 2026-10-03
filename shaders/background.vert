attribute vec2 a_pos;
varying float v_y;
varying vec2 v_ndc;
void main() {
    v_y = a_pos.y * 0.5 + 0.5;
    v_ndc = a_pos;
    gl_Position = vec4(a_pos, 0.999, 1.0);
}
