precision mediump float;

attribute vec3 a_pos;
attribute vec2 a_uv;

uniform mat4 u_mvp;
uniform vec4 u_frame_rect;
uniform float u_use_vertex_uv;
uniform float u_time;
uniform vec2 u_wobble;
uniform float u_close_progress;

varying vec2 v_uv;

void main() {
    vec2 local_pos = a_pos.xy;
    vec2 local_uv = mix(local_pos, a_uv, step(0.5, u_use_vertex_uv));
    vec2 frame_uv = u_frame_rect.xy + local_pos * u_frame_rect.zw;
    vec3 pos = vec3(frame_uv, a_pos.z);

    float bx = sin(frame_uv.y * 3.14159265);
    float by = sin(frame_uv.x * 3.14159265);
    pos.x += u_wobble.x * bx;
    pos.y += u_wobble.y * by;

    float close_p = smoothstep(0.0, 1.0, clamp(u_close_progress, 0.0, 1.0));
    pos.xy = mix(pos.xy, vec2(0.5), close_p * 0.94);

    v_uv = vec2(local_uv.x, 1.0 - local_uv.y);
    gl_Position = u_mvp * vec4(pos, 1.0);
    gl_Position.y = -gl_Position.y;
}
