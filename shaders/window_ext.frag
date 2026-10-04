#extension GL_OES_EGL_image_external : require
#ifdef GL_OES_standard_derivatives
#extension GL_OES_standard_derivatives : enable
#endif
/* Keep time, subpixel UVs and finite-difference normals at full precision.
 * Half precision can quantize water motion into visible jumps, especially
 * after the compositor has been running for several minutes. Both shader
 * stages use the same precision so interpolated UVs retain that accuracy. */
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

uniform samplerExternalOES u_tex;
uniform vec4 u_tint;

/* 1.0 = sample alpha; 0.0 = force opaque (RGBX / XRGB). */
uniform float u_has_alpha;

/* Seconds since compositor render timer started. */
uniform float u_time;
uniform vec3 u_light_dir;
uniform float u_effect_strength;
uniform float u_brightness;
uniform vec4 u_water; /* amplitude, frequency, speed, phase */
uniform vec4 u_water_surface; /* fresnel, specular, caustic, tint */
uniform vec4 u_border_color;
uniform vec2 u_border_width; /* normalized x/y thickness */
uniform float u_close_progress;
uniform vec4 u_close_effect; /* style, strength, direction_x, direction_y */
uniform vec4 u_frame_rect; /* this pass's sub-rect inside the decorated frame */
uniform vec2 u_frame_px; /* decorated frame size in px; 0 = legacy border */
uniform vec2 u_frame_shape; /* corner radius px, border px */

varying vec2 v_uv;
varying vec3 v_normal;
varying float v_water_wave;

/* Signed distance to the decorated frame's rounded rectangle, in frame px.
 * Negative inside. Uses the undeformed mesh UV so the silhouette stays
 * attached to the window perimeter under wobble and water refraction. */
float frame_distance() {
	vec2 frame_uv = u_frame_rect.xy + vec2(v_uv.x, 1.0 - v_uv.y) * u_frame_rect.zw;
	vec2 half_size = u_frame_px * 0.5;
	float r = min(u_frame_shape.x, min(half_size.x, half_size.y));
	vec2 q = abs(frame_uv * u_frame_px - half_size) - (half_size - vec2(r));
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float frame_aa(float d) {
#ifdef GL_OES_standard_derivatives
	return max(fwidth(d), 0.0001);
#else
	return 1.0;
#endif
}

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
	vec2 water_offset = ((water_offset_large * 0.68 + water_offset_small * 0.06) *
		(u_water.x * 0.095) + slope_refraction * u_water.x * 0.75) * water_edge;
	uv = clamp(uv + water_offset, 0.0, 1.0);

	/*
	 * Subtle animated chromatic aberration.
	 *
	 * Keep this deliberately small: we're rendering real application
	 * contents, so text should remain readable.
	 */
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

	/*
	 * Distance from the closest edge of the window.
	 */
	float edge_distance = min(
		min(uv.x, 1.0 - uv.x),
		min(uv.y, 1.0 - uv.y)
	);

	/*
	 * Thin outer glow.
	 */
	float edge = 1.0 - smoothstep(
		0.0,
		0.025,
		edge_distance
	);

	/*
	 * Slightly wider secondary glow.
	 */
	float soft_edge = 1.0 - smoothstep(
		0.0,
		0.09,
		edge_distance
	);

	/*
	 * Slowly shifting neon color.
	 */
	vec3 neon_a = vec3(0.10, 0.45, 1.00);
	vec3 neon_b = vec3(0.75, 0.15, 1.00);

	float wave = 0.5 + 0.5 * sin(
		u_time * 1.25 +
		uv.y * 6.0 +
		uv.x * 2.0
	);

	vec3 neon = mix(neon_a, neon_b, wave);

	/*
	 * Strong thin edge + subtle halo.
	 */
	color.rgb += neon * edge * (0.08 + 0.05 * pulse) * u_effect_strength;
	color.rgb += neon * soft_edge * 0.018 * u_effect_strength;

	/*
	 * Very subtle scanline modulation.
	 *
	 * UV-based rather than pixel-based so no resolution uniform is needed.
	 */
	float scan = 0.985 + 0.015 * sin(
		uv.y * 900.0 + u_time * 2.0
	);

	color.rgb *= mix(1.0, scan, u_effect_strength);

	/* Tint and exposure are explicit compositor configuration. */
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

	/* Window border uses the original mesh UV, not refracted UV, so it stays
	 * locked to the physical window perimeter even when the water surface
	 * bends application contents underneath it. */
	float frame_enabled = step(0.5, min(u_frame_px.x, u_frame_px.y));
	float border_enabled = step(0.000001, max(u_border_width.x, u_border_width.y)) *
		(1.0 - frame_enabled);
	float border_x = min(v_uv.x, 1.0 - v_uv.x) / max(u_border_width.x, 0.000001);
	float border_y = min(v_uv.y, 1.0 - v_uv.y) / max(u_border_width.y, 0.000001);
	float border_mask = (1.0 - smoothstep(0.72, 1.0, min(border_x, border_y))) *
		border_enabled;
	float frame_coverage = 1.0;
	if (frame_enabled > 0.5) {
		/* Rounded decorated frame: one continuous anti-aliased outline around
		 * title bar and client together, plus a faint inner glow so focused
		 * accents read as light rather than a flat painted stroke. */
		float d = frame_distance();
		float aa = frame_aa(d);
		frame_coverage = clamp(0.5 - d / aa, 0.0, 1.0);
		float border_px = u_frame_shape.y;
		float inner = max(-d - border_px, 0.0);
		border_mask = step(0.0001, border_px) *
			clamp(0.5 + (d + border_px) / aa, 0.0, 1.0);
		float glow = step(0.0001, border_px) * exp(-inner / 5.0) * 0.16;
		color.rgb += u_border_color.rgb * u_border_color.a * glow * color.a;
	}
	float border_alpha = clamp(border_mask * u_border_color.a, 0.0, 1.0);
	color.rgb = color.rgb * (1.0 - border_alpha) + u_border_color.rgb * border_alpha;
	color.a = color.a + border_alpha * (1.0 - color.a);

	float slide_close = 1.0 - step(0.5, abs(u_close_effect.x - 1.0));
	float close_p = clamp(u_close_progress * clamp(u_close_effect.y, 0.0, 2.0), 0.0, 1.0);
	float close_fade = mix(1.0, 1.0 - smoothstep(0.08, 1.0, close_p), slide_close);
	color.rgb *= close_fade;
	color.a *= close_fade;

	/* Premultiplied output: coverage scales colour and alpha together. */
	if (frame_coverage <= 0.0) discard;
	color *= frame_coverage;

	gl_FragColor = color;
}