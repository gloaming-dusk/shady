#extension GL_OES_EGL_image_external : require
precision mediump float;

uniform samplerExternalOES u_tex;
uniform vec4 u_tint;
uniform float u_has_alpha;
uniform float u_time;
uniform vec3 u_light_dir;
uniform float u_effect_strength;
uniform float u_brightness;
uniform vec4 u_water; /* amplitude, frequency, speed, phase */

varying vec2 v_uv;
varying vec3 v_normal;

void main() {
	vec2 uv = v_uv;
	float water_phase = u_time * u_water.z + u_water.w;
	float water_edge = sin(uv.x * 3.14159265) * sin(uv.y * 3.14159265);
	vec2 water_offset = vec2(
		sin(uv.y * u_water.y + water_phase),
		cos(uv.x * u_water.y * 0.91 - water_phase * 0.77)
	) * (u_water.x * 0.10) * water_edge;
	uv = clamp(uv + water_offset, 0.0, 1.0);

	float pulse = 0.5 + 0.5 * sin(u_time * 1.7);
	float aberration = (0.0007 + 0.0013 * pulse) * u_effect_strength;

	vec4 center = texture2D(u_tex, uv);

	float r = texture2D(
		u_tex,
		clamp(uv + vec2(aberration, 0.0), 0.0, 1.0)
	).r;

	float g = center.g;

	float b = texture2D(
		u_tex,
		clamp(uv - vec2(aberration, 0.0), 0.0, 1.0)
	).b;

	vec4 color = vec4(r, g, b, center.a);

	if (u_has_alpha < 0.5) {
		color.a = 1.0;
	}

	float edge_distance = min(
		min(uv.x, 1.0 - uv.x),
		min(uv.y, 1.0 - uv.y)
	);

	float edge = 1.0 - smoothstep(
		0.0,
		0.025,
		edge_distance
	);

	float soft_edge = 1.0 - smoothstep(
		0.0,
		0.09,
		edge_distance
	);

	vec3 neon_a = vec3(0.10, 0.45, 1.00);
	vec3 neon_b = vec3(0.75, 0.15, 1.00);

	float wave = 0.5 + 0.5 * sin(
		u_time * 1.25 +
		uv.y * 6.0 +
		uv.x * 2.0
	);

	vec3 neon = mix(neon_a, neon_b, wave);

	color.rgb += neon * edge * (0.08 + 0.05 * pulse) * u_effect_strength;
	color.rgb += neon * soft_edge * 0.018 * u_effect_strength;

	float scan = 0.985 + 0.015 * sin(
		uv.y * 900.0 + u_time * 2.0
	);

	color.rgb *= mix(1.0, scan, u_effect_strength);

	color.rgb *= u_tint.rgb;
	color.rgb *= u_brightness;
	color.a *= u_tint.a;

	/* Light follows the normal of the deformed 3D sheet. */
	vec3 n = normalize(v_normal);
	vec3 l = normalize(u_light_dir);
	float diffuse = max(dot(n, l), 0.0);
	float facing = clamp(abs(n.z), 0.0, 1.0);
	float surface_light = 0.86 + diffuse * 0.14;
	float grazing = (1.0 - facing) * 0.055;
	color.rgb *= mix(1.0, surface_light, u_effect_strength);
	color.rgb += vec3(0.08, 0.16, 0.28) * grazing * u_effect_strength;

	gl_FragColor = color;
}