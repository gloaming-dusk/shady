precision mediump float;

attribute vec3 a_pos;
attribute vec2 a_uv;

uniform mat4 u_mvp;
uniform mat4 u_model;
uniform vec4 u_frame_rect; /* x, y, width, height in full-frame local coords */
uniform float u_use_vertex_uv;

uniform vec2 u_wobble;
uniform vec4 u_water; /* amplitude, frequency, speed, phase */
uniform float u_time;

/*
 * 0.0 = normal
 * 1.0 = completely crumpled
 */
uniform float u_close_progress;
uniform vec4 u_close_effect; /* style, strength, direction_x, direction_y */

varying vec2 v_uv;
varying vec3 v_normal;
varying float v_water_wave;

void main() {
	vec2 local_pos = a_pos.xy;
	vec2 local_uv = mix(local_pos, a_uv, step(0.5, u_use_vertex_uv));
	vec2 uv = u_frame_rect.xy + local_pos * u_frame_rect.zw;

	v_uv = vec2(
		local_uv.x,
		1.0 - local_uv.y
	);

	vec3 pos = vec3(uv, a_pos.z);

	float cx =
		uv.x - 0.5;

	float cy =
		uv.y - 0.5;

	/*
	 * ------------------------------------------------------------
	 * Existing wobbly-window deformation
	 * ------------------------------------------------------------
	 */

	float bend_x =
		sin(
			uv.y *
			3.14159265
		);

	float bend_y =
		sin(
			uv.x *
			3.14159265
		);

	pos.x +=
		u_wobble.x *
		bend_x *
		(
			0.75 +
			0.25 *
			cos(
				cy *
				3.14159265
			)
		);

	pos.y +=
		u_wobble.y *
		bend_y *
		(
			0.75 +
			0.25 *
			cos(
				cx *
				3.14159265
			)
		);

	pos.x +=
		u_wobble.y *
		cy *
		0.18 *
		bend_y;

	pos.y +=
		u_wobble.x *
		cx *
		0.18 *
		bend_x;

	/*
	 * True 3D flexible-sheet deformation. The same spring impulse that
	 * bends X/Y now bows the mesh through local Z as well.
	 */
	float depth_shape =
		sin(uv.x * 3.14159265) *
		sin(uv.y * 3.14159265);

	pos.z +=
		(u_wobble.x * cy - u_wobble.y * cx) *
		0.65 *
		depth_shape;

	/* Native-plugin driven water surface. Two travelling waves cross at
	 * different angles; edge falloff keeps the window perimeter stable. */
	float water_edge = sin(uv.x * 3.14159265) * sin(uv.y * 3.14159265);
	float water_phase = u_time * u_water.z + u_water.w;
	float water_a = sin((uv.x * 1.00 + uv.y * 0.63) * u_water.y + water_phase);
	float water_b = cos((uv.x * 0.47 - uv.y * 1.12) * u_water.y * 1.31 - water_phase * 0.73);
	float water_wave = water_a * 0.68 + water_b * 0.32;
	v_water_wave = water_wave * water_edge;
	pos.z += u_water.x * water_wave * water_edge;
	pos.x += u_water.x * 0.16 * cos(water_phase + uv.y * u_water.y) * water_edge;
	pos.y += u_water.x * 0.13 * sin(water_phase * 0.83 + uv.x * u_water.y) * water_edge;

	/*
	 * ------------------------------------------------------------
	 * Close / crumple animation
	 * ------------------------------------------------------------
	 */

	float raw_close = clamp(u_close_progress, 0.0, 1.0);
	float close_style = u_close_effect.x;
	float close_strength = clamp(u_close_effect.y, 0.0, 2.0);
	float crumple_mask = 1.0 - step(0.5, abs(close_style - 0.0));
	float slide_mask = 1.0 - step(0.5, abs(close_style - 1.0));
	float p = clamp(raw_close * crumple_mask * close_strength, 0.0, 1.0);

	/*
	 * Ease-in.
	 *
	 * The first part moves slowly, then the collapse accelerates.
	 */
	float collapse =
		p * p;

	/*
	 * Pull every vertex toward the centre.
	 *
	 * At the end we leave a tiny amount of size so triangles do not
	 * become numerically degenerate too early.
	 */
	float shrink =
		1.0 -
		collapse *
		0.94;

	vec2 centred =
		pos.xy -
		vec2(
			0.5,
			0.5
		);

	centred *=
		shrink;

	pos.xy =
		centred +
		vec2(
			0.5,
			0.5
		);

	/*
	 * Crumpling waves.
	 *
	 * Their amplitude grows during the middle of the animation and
	 * disappears again as the window reaches its final tiny shape.
	 */
	float wrinkle_envelope =
		sin(
			p *
			3.14159265
		);

	float wrinkle1 =
		sin(
			uv.x * 31.0 +
			uv.y * 17.0 +
			p * 11.0
		);

	float wrinkle2 =
		cos(
			uv.x * 19.0 -
			uv.y * 29.0 -
			p * 8.0
		);

	float wrinkle3 =
		sin(
			(uv.x + uv.y) *
			37.0 +
			p * 14.0
		);

	pos.x +=
		wrinkle1 *
		0.055 *
		wrinkle_envelope;

	pos.y +=
		wrinkle2 *
		0.045 *
		wrinkle_envelope;

	/*
	 * Push alternating mesh regions forward/backward in Z.
	 *
	 * This is what gives the paper-fold / crushed-sheet appearance
	 * when the compositor camera is rotated.
	 */
	pos.z +=
		(
			wrinkle1 *
			wrinkle2 +
			wrinkle3 * 0.5
		) *
		0.055 *
		wrinkle_envelope;

	/*
	 * Twist the shrinking window around its centre.
	 */
	float angle =
		p *
		p *
		1.15;

	float s =
		sin(angle);

	float c =
		cos(angle);

	vec2 rotate_pos =
		pos.xy -
		vec2(
			0.5,
			0.5
		);

	rotate_pos =
		vec2(
			rotate_pos.x * c -
			rotate_pos.y * s,

			rotate_pos.x * s +
			rotate_pos.y * c
		);

	pos.xy =
		rotate_pos +
		vec2(
			0.5,
			0.5
		);

	/*
	 * Final "suction" deformation.
	 *
	 * Vertices near the outside are pulled slightly harder than the
	 * centre, giving the last few frames a crushed-ball appearance.
	 */
	float radius =
		length(
			vec2(
				cx,
				cy
			)
		);

	float suction =
		smoothstep(
			0.45,
			1.0,
			p
		);

	pos.x +=
		cx *
		radius *
		0.12 *
		suction;

	pos.y -=
		cy *
		radius *
		0.08 *
		suction;

	/*
	 * Pull the final object slightly toward the camera.
	 */
	pos.z +=
		p *
		p *
		0.08;

	/* Plugin-selectable slide/fade close style. Geometry motion stays simple
	 * and readable; alpha fading is completed in the fragment shader. */
	float slide_p = clamp(smoothstep(0.0, 1.0, raw_close) * slide_mask * close_strength, 0.0, 1.0);
	vec2 slide_center = pos.xy - vec2(0.5);
	slide_center *= 1.0 - min(slide_p * 0.14, 0.28);
	pos.xy = slide_center + vec2(0.5);
	pos.x += u_close_effect.z * slide_p * 0.32;
	pos.y += u_close_effect.w * slide_p * 0.32;
	pos.z += slide_p * 0.045;

	/*
	 * Reconstruct the deformed surface normal from the same local
	 * deformation function using small finite differences. This keeps
	 * lighting attached to the actual wobbly/crumpled sheet.
	 */
	float eps = 0.0025;
	vec2 ux = clamp(uv + vec2(eps, 0.0), 0.0, 1.0);
	vec2 uy = clamp(uv + vec2(0.0, eps), 0.0, 1.0);
	float zx = (u_wobble.x * (ux.y - 0.5) - u_wobble.y * (ux.x - 0.5)) *
		0.65 * sin(ux.x * 3.14159265) * sin(ux.y * 3.14159265);
	float zy = (u_wobble.x * (uy.y - 0.5) - u_wobble.y * (uy.x - 0.5)) *
		0.65 * sin(uy.x * 3.14159265) * sin(uy.y * 3.14159265);
	float z0 = (u_wobble.x * cy - u_wobble.y * cx) * 0.65 * depth_shape;

	float water_edge_x = sin(ux.x * 3.14159265) * sin(ux.y * 3.14159265);
	float water_edge_y = sin(uy.x * 3.14159265) * sin(uy.y * 3.14159265);
	float water_ax = sin((ux.x * 1.00 + ux.y * 0.63) * u_water.y + water_phase);
	float water_bx = cos((ux.x * 0.47 - ux.y * 1.12) * u_water.y * 1.31 - water_phase * 0.73);
	float water_ay = sin((uy.x * 1.00 + uy.y * 0.63) * u_water.y + water_phase);
	float water_by = cos((uy.x * 0.47 - uy.y * 1.12) * u_water.y * 1.31 - water_phase * 0.73);
	zx += u_water.x * (water_ax * 0.68 + water_bx * 0.32) * water_edge_x;
	zy += u_water.x * (water_ay * 0.68 + water_by * 0.32) * water_edge_y;
	z0 += u_water.x * water_wave * water_edge;
	vec3 tangent_x = vec3(eps, 0.0, zx - z0);
	vec3 tangent_y = vec3(0.0, eps, zy - z0);
	vec3 local_normal = normalize(cross(tangent_x, tangent_y));
	v_normal = normalize(mat3(u_model) * local_normal);

	gl_Position =
		u_mvp *
		vec4(
			pos,
			1.0
		);

	gl_Position.y =
		-gl_Position.y;
}