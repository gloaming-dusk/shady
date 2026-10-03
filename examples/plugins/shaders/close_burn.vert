precision mediump float;

attribute vec3 a_pos;

uniform mat4 u_mvp;
uniform vec4 u_frame_rect;
uniform float u_close_progress;
uniform float u_time;

varying vec2 v_uv;
varying vec2 v_frame_uv;
varying float v_burn_progress;

void main() {
    vec2 local_uv = a_pos.xy;
    vec2 frame_uv = u_frame_rect.xy + local_uv * u_frame_rect.zw;
    vec3 pos = vec3(frame_uv, a_pos.z);

    float p = smoothstep(0.0, 1.0, clamp(u_close_progress, 0.0, 1.0));
    float burn_front = p * 1.18 - 0.08;
    float front_distance = abs(frame_uv.y - burn_front);
    float curl = exp(-front_distance * 22.0) * p;

    float edge = sin(frame_uv.x * 3.14159265) * sin(frame_uv.y * 3.14159265);
    pos.z += curl * edge * (0.055 + 0.018 * sin(u_time * 8.0 + frame_uv.x * 13.0));
    pos.x += curl * 0.012 * sin(frame_uv.y * 31.0 + u_time * 6.0);
    pos.y += curl * 0.010 * cos(frame_uv.x * 27.0 - u_time * 5.0);

    v_uv = vec2(local_uv.x, 1.0 - local_uv.y);
    v_frame_uv = frame_uv;
    v_burn_progress = p;

    gl_Position = u_mvp * vec4(pos, 1.0);
    gl_Position.y = -gl_Position.y;
}
