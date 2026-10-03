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
uniform vec4 u_water_surface; /* fresnel, specular, caustic, tint */

varying vec2 v_uv;
varying vec3 v_normal;
varying float v_water_wave;

void main() {
	vec2 uv = v_uv;
	vec3 n = normalize(v_normal);
	float water_phase = u_time * u_water.z + u_water.w;
	float water_edge = sin(uv.x * 3.14159265) * sin(uv.y * 3.14159265);
	/* Two refraction scales avoid the repeating 'rubber sheet' look: a broad
	 * travelling wave carries the image while a smaller cross-wave creates
	 * fine liquid shimmer. */
	vec2 water_offset_large = vec2(
		sin(uv.y * u_water.y + water_phase),
		cos(uv.x * u_water.y * 0.91 - water_phase * 0.77)
	);
	vec2 water_offset_small = vec2(
		sin((uv.x + uv.y * 1.7) * u_water.y * 1.85 - water_phase * 1.21),
		cos((uv.y - uv.x * 1.3) * u_water.y * 2.15 + water_phase * 0.94)
	);
	vec2 slope_refraction = n.xy * vec2(-1.0, 1.0) * 0.045;
	vec2 water_offset = ((water_offset_large * 0.68 + water_offset_small * 0.18) *
		(u_water.x * 0.095) + slope_refraction * u_water.x * 0.75) * water_edge;
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
	/* Premultiplied-alpha pipeline: global opacity must scale RGB and A. */
	color.rgb *= u_tint.a;
	color.a *= u_tint.a;

	/* Light follows the normal of the deformed 3D sheet. */
	vec3 l = normalize(u_light_dir);
	float diffuse = max(dot(n, l), 0.0);
	float facing = clamp(abs(n.z), 0.0, 1.0);
	float surface_light = 0.86 + diffuse * 0.14;
	float grazing = (1.0 - facing) * 0.055;
	color.rgb *= mix(1.0, surface_light, u_effect_strength);
	color.rgb += vec3(0.08, 0.16, 0.28) * grazing * u_effect_strength;

	/* Liquid surface shading. The deformed normal comes from window.vert,
	 * so highlights travel over the same waves that bend the application
	 * texture instead of looking like a flat post-process overlay. */
	float water_active = step(0.0001, u_water.x);
	vec3 view_dir = normalize(vec3(0.16, -0.10, 1.0));
	float water_fresnel = pow(1.0 - clamp(abs(dot(n, view_dir)), 0.0, 1.0), 2.2);
	vec3 reflected = reflect(-l, n);
	float spec_dot = max(dot(reflected, view_dir), 0.0);
	float water_spec = pow(spec_dot, 11.0);
	float water_sparkle = pow(spec_dot, 42.0);
	float caustic_a = sin(uv.x * 31.0 + uv.y * 17.0 + water_phase * 1.45);
	float caustic_b = sin(uv.x * 19.0 - uv.y * 29.0 - water_phase * 1.08);
	float water_caustic = pow(clamp(0.5 + 0.25 * caustic_a + 0.25 * caustic_b, 0.0, 1.0), 3.0);
	float crest = clamp(v_water_wave * 0.5 + 0.5, 0.0, 1.0);
	float trough = 1.0 - crest;
	vec3 water_tint = vec3(0.02, 0.48, 0.72);
	vec3 water_glint = vec3(0.55, 0.92, 1.0);
	color.rgb = mix(color.rgb,
		color.rgb * (1.0 - 0.13 * u_water_surface.w) + water_tint * 0.40,
		water_active * u_water_surface.w * (0.22 + water_fresnel * 0.42));
	color.rgb += water_tint * water_fresnel * u_water_surface.x * 0.44 * water_active;
	color.rgb += water_glint * water_spec * u_water_surface.y * 0.56 * water_active;
	color.rgb += vec3(0.80, 0.97, 1.0) * water_sparkle * u_water_surface.y *
		(0.16 + crest * 0.18) * water_active;
	color.rgb += water_glint * water_caustic * u_water_surface.z * 0.095 *
		water_edge * water_active;
	/* Crests transmit more light; troughs gain a hint of blue absorption. */
	color.rgb *= 1.0 - trough * u_water_surface.w * 0.045 * water_active;
	color.rgb += water_tint * crest * u_water_surface.w * 0.035 * water_active;

	/* Thin-water transmission: face-on areas are slightly more transparent,
	 * while grazing/Fresnel regions remain denser. Keep RGB premultiplied. */
	float transmission = mix(0.90, 1.0, water_fresnel);
	float water_alpha_scale = mix(1.0, transmission, water_active * u_water_surface.w * 0.55);
	color.rgb *= water_alpha_scale;
	color.a *= water_alpha_scale;

	/* Compress only the water-generated highlight range so glints keep detail
	 * instead of clipping into flat white patches on bright application UI. */
	vec3 water_compressed = color.rgb / (vec3(1.0) + color.rgb * 0.18);
	color.rgb = mix(color.rgb, water_compressed * 1.12,
		water_active * clamp(u_water_surface.y * 0.20, 0.0, 0.30));

	gl_FragColor = color;
}