attribute vec3 a_pos;
uniform mat4 u_mvp;
varying vec2 v_uv;
void main() {
    v_uv = vec2(a_pos.x, 1.0 - a_pos.y);
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    gl_Position.y = -gl_Position.y;
}
