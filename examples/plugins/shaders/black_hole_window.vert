precision mediump float;

/*
 * Window falling into (or out of) the black-hole plugin's singularity.
 *
 * All motion happens here; the window's real layout position never changes.
 * The plugin computes the orbit on the CPU and passes it per window through
 * u_params (see window_set_shader_params). Units are frame pixels with +y up,
 * matching the window model's local [0,1] frame.
 *
 *   u_params[0] = translation x, translation y, spin (radians), fall (0..1)
 *   u_params[1] = toward-hole direction x, y (unit, pre-spin), depth (local z), mode
 *   u_params[2] = frame size estimate w, h (used when u_frame_px is 0),
 *                 tremble amplitude in pixels
 *
 * mode 0 = black hole (swallow), 1 = white hole (emit).
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
/* fall, heat (side facing the hole), fade, mode */
varying vec4 v_fx;

void main() {
	vec2 local_pos = a_pos.xy;
	vec2 local_uv = mix(local_pos, a_uv, step(0.5, u_use_vertex_uv));
	vec2 uv = u_frame_rect.xy + local_pos * u_frame_rect.zw;
	v_uv = vec2(local_uv.x, 1.0 - local_uv.y);

	float fall = clamp(u_params[0].w, 0.0, 1.0);
	vec2 toward = u_params[1].xy;
	vec2 frame = u_frame_px.x > 0.0 ? u_frame_px : max(u_params[2].xy, vec2(1.0));

	/* Work in pixels around the frame centre so stretching keeps its shape. */
	vec2 q = (uv - 0.5) * frame;
	float half_extent = 0.5 * max(frame.x, frame.y);

	/* Spaghettification: stretch along the line to the hole, squeeze across it.
	 * The near side leads, so the window visibly pours into the singularity. */
	float along = dot(q, toward);
	vec2 across = q - toward * along;
	along *= 1.0 + 2.4 * fall;
	along += half_extent * 0.9 * fall;
	across *= 1.0 - 0.7 * fall;
	q = toward * along + across;

	/* Swirl: points nearer the hole are dragged further round the orbit. */
	vec2 side = vec2(-toward.y, toward.x);
	float lead = clamp(along / max(half_extent * 3.0, 1.0), -1.0, 1.0);
	q += side * lead * lead * half_extent * 0.55 * fall;

	/* Collapse towards a point; keep a sliver so triangles stay valid. */
	q *= 1.0 - 0.975 * pow(fall, 0.65);

	/* Shudder before being torn away: the gravity is already pulling. */
	float tremble = u_params[2].z;
	q += tremble * vec2(
		sin(u_time * 71.0 + uv.y * 9.0 + uv.x * 3.0),
		cos(u_time * 63.0 + uv.x * 11.0 - uv.y * 2.0));

	float spin = u_params[0].z;
	float s = sin(spin), c = cos(spin);
	q = vec2(q.x * c - q.y * s, q.x * s + q.y * c);
	q += u_params[0].xy;

	vec3 pos = vec3(q / frame + 0.5, a_pos.z + u_params[1].z);

	/* Heat builds on the edge facing the hole, as matter does in a real disk. */
	float facing = clamp(dot(normalize(uv - 0.5 + 1e-4), toward) * 0.5 + 0.5, 0.0, 1.0);
	float heat = pow(facing, 3.0) * smoothstep(0.05, 0.7, fall);
	float fade = 1.0 - smoothstep(0.80, 1.0, fall);
	v_fx = vec4(fall, heat, fade, u_params[1].w);
	v_normal = normalize(mat3(u_model) * vec3(0.0, 0.0, 1.0));

	gl_Position = u_mvp * vec4(pos, 1.0);
	gl_Position.y = -gl_Position.y;
}
