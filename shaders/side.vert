attribute vec3 a_pos;
attribute vec3 a_normal;
uniform mat4 u_mvp;
uniform mat4 u_model;
uniform vec2 u_wobble;
varying vec3 v_normal;
void main() {
    vec3 pos = a_pos;
    float bend_x = sin(pos.y * 3.14159265);
    float bend_y = sin(pos.x * 3.14159265);
    float cx = pos.x - 0.5;
    float cy = pos.y - 0.5;
    pos.x += u_wobble.x * bend_x * (0.75 + 0.25 * cos(cy * 3.14159265));
    pos.y += u_wobble.y * bend_y * (0.75 + 0.25 * cos(cx * 3.14159265));
    pos.x += u_wobble.y * cy * 0.18 * bend_y;
    pos.y += u_wobble.x * cx * 0.18 * bend_x;
    float depth_shape = sin(pos.x * 3.14159265) * sin(pos.y * 3.14159265);
    pos.z += (u_wobble.x * cy - u_wobble.y * cx) * 0.65 * depth_shape;
    gl_Position = u_mvp * vec4(pos, 1.0);
    gl_Position.y = -gl_Position.y;
    v_normal = normalize(mat3(u_model) * a_normal);
}
