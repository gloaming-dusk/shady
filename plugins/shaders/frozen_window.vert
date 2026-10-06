#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

/*
 * Window held by the frozen-window plugin. Geometry stays where the layout
 * put it; the only motion is a brief shiver as the cold hits.
 *
 *   u_params[0] = frost (0..1), thawing (0/1), pattern seed, shiver (px)
 *   u_params[1] = client size w, h (frame size fallback when u_frame_px is 0)
 */

attribute vec3 a_pos;
attribute vec2 a_uv;

uniform mat4 u_mvp;
uniform mat4 u_model;
uniform vec4 u_frame_rect;
uniform float u_use_vertex_uv;
uniform vec2 u_frame_px;
uniform vec4 u_params[4];
uniform float u_time;

varying vec2 v_uv;
varying vec3 v_normal;

void main() {
	vec2 local_pos = a_pos.xy;
	vec2 local_uv = mix(local_pos, a_uv, step(0.5, u_use_vertex_uv));
	vec2 uv = u_frame_rect.xy + local_pos * u_frame_rect.zw;
	v_uv = vec2(local_uv.x, 1.0 - local_uv.y);

	vec2 frame = u_frame_px.x > 0.0 ? u_frame_px : max(u_params[1].xy, vec2(1.0));

	/* Whole-window shudder: one rigid jitter so text never smears apart. */
	float shiver = u_params[0].w;
	vec2 jitter = shiver * vec2(sin(u_time * 83.0), cos(u_time * 71.0 + 1.3));

	vec3 pos = vec3(uv + jitter / frame, a_pos.z);
	v_normal = normalize(mat3(u_model) * vec3(0.0, 0.0, 1.0));

	gl_Position = u_mvp * vec4(pos, 1.0);
	gl_Position.y = -gl_Position.y;
}
