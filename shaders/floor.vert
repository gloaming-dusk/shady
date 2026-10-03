attribute vec3 a_pos;
uniform mat4 u_vp;
varying vec2 v_world;
void main() {
    v_world = a_pos.xz;
    gl_Position = u_vp * vec4(a_pos, 1.0);
    gl_Position.y = -gl_Position.y;
}
