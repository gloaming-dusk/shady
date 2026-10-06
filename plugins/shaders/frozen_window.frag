#ifdef GL_OES_standard_derivatives
#extension GL_OES_standard_derivatives : enable
#endif
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

/*
 * Frost and meltwater for the frozen-window plugin (see the .vert file for
 * u_params). All patterns live in frame pixels, so the frost runs on
 * unbroken across the title bar and client passes and keeps its scale when
 * the window is resized.
 *
 * Freezing: a noisy front advances from the frame edges toward the centre.
 * Behind it the glass is frosted (blurred, refracted, whitened) and laced
 * with crystal facets and needle feathers; the leading edge glows. The
 * centre is only hazed, so the window stays readable.
 * Thawing: the same front recedes and meltwater drips run down the glass.
 */

uniform sampler2D u_tex;
uniform vec4 u_tint;
uniform float u_has_alpha;
uniform float u_brightness;
uniform float u_time;
uniform vec3 u_light_dir;
uniform vec4 u_border_color;
uniform vec4 u_frame_rect;
uniform vec2 u_frame_px;
uniform vec2 u_frame_shape;
uniform vec4 u_params[4];

varying vec2 v_uv;
varying vec3 v_normal;

const vec3 FROST = vec3(0.86, 0.94, 1.00);
const vec3 ICE_EDGE = vec3(0.55, 0.85, 1.00);

float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.xx + p3.yz) * p3.zy);
}

float noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),
		mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p) {
	float v = 0.0;
	float a = 0.5;
	for (int i = 0; i < 4; i++) {
		v += a * noise(p);
		p = p * 2.03 + vec2(17.1, 9.2);
		a *= 0.5;
	}
	return v / 0.9375;
}

/* x = distance to the nearest cell point, y = to the second nearest,
 * z = random id of the nearest cell. */
vec3 voronoi(vec2 x) {
	vec2 n = floor(x);
	vec2 f = fract(x);
	float f1 = 8.0;
	float f2 = 8.0;
	float id = 0.0;
	for (int j = -1; j <= 1; j++) {
		for (int i = -1; i <= 1; i++) {
			vec2 g = vec2(float(i), float(j));
			vec2 r = g + hash22(n + g) - f;
			float d = dot(r, r);
			if (d < f1) {
				f2 = f1;
				f1 = d;
				id = hash12(n + g + 7.3);
			} else if (d < f2) {
				f2 = d;
			}
		}
	}
	return vec3(sqrt(f1), sqrt(f2), id);
}

/* Rounded decorated frame, as in the stock window shader. Negative inside. */
float frame_distance(vec2 frame_uv) {
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

vec4 sample_client(vec2 uv) {
	vec4 c = texture2D(u_tex, clamp(uv, 0.0, 1.0));
	if (u_has_alpha < 0.5) c.a = 1.0;
	return c;
}

void main() {
	float frost = clamp(u_params[0].x, 0.0, 1.0);
	float thawing = step(0.5, u_params[0].y);
	float seed = u_params[0].z;

	vec2 frame = u_frame_px.x > 0.0 ? u_frame_px : max(u_params[1].xy, vec2(1.0));
	vec2 frame_uv = u_frame_rect.xy + vec2(v_uv.x, 1.0 - v_uv.y) * u_frame_rect.zw;
	/* Frame pixels, origin bottom-left, +y up. */
	vec2 p = frame_uv * frame;
	/* Client-texture uv per frame pixel; y flips because textures run down. */
	vec2 px_to_uv = vec2(1.0, -1.0) / max(frame * u_frame_rect.zw, vec2(1.0));

	float grow = smoothstep(0.0, 1.0, frost);
	float density = 0.0;
	float front_glow = 0.0;
	float crystal = 0.0;
	float sparkle = 0.0;
	vec2 bend = vec2(0.0);

	vec4 color;
	if (grow > 0.001) {
		float edge_px = min(min(p.x, frame.x - p.x), min(p.y, frame.y - p.y));
		/* 0 at the frame edge, 1 at the centre, scaled per axis so the clear
		 * centre keeps the window's proportions. The superellipse bulges
		 * into the corners: they are cold from two sides and freeze first. */
		vec2 c = abs(frame_uv * 2.0 - 1.0);
		float inward = 1.0 - pow(pow(c.x, 6.0) + pow(c.y, 6.0), 1.0 / 6.0);

		float broad = fbm(p / 95.0 + seed);
		float fine = fbm(p / 23.0 + seed * 1.7);
		float field = inward + (broad - 0.5) * 0.45 + (fine - 0.5) * 0.14;
		/* Fully frozen, the rime reaches about a third of the way in and only
		 * the odd frond crosses the middle, so the window stays readable. */
		float front = grow * 0.42 - 0.06;
		density = (1.0 - smoothstep(front - 0.16, front, field)) * smoothstep(0.0, 0.08, grow);
		front_glow = (1.0 - smoothstep(0.0, 0.05, abs(field - front))) * grow * (1.0 - grow * 0.6);

		/* Crystal facets: bright thin lines along Voronoi cell borders. */
		vec3 big = voronoi(p / 38.0 + seed);
		vec3 small = voronoi(p / 12.0 - seed);
		float facets = 1.0 - smoothstep(0.0, 0.07, big.y - big.x);
		float grains = 1.0 - smoothstep(0.0, 0.10, small.y - small.x);
		/* Feathers: parallel needles inside each large cell, each cell with
		 * its own growth direction, like frost ferns on a cold pane. */
		float angle = big.z * 6.2831853;
		vec2 dir = vec2(cos(angle), sin(angle));
		float needles = pow(abs(sin(dot(p, dir) * 0.85 + big.z * 40.0)), 10.0);
		needles *= smoothstep(0.15, 0.9, fine);
		crystal = facets * 0.35 + grains * 0.15 + needles * 0.50;

		/* Ice bends the light that passes through it. */
		bend = (vec2(broad, fine) - 0.5) * 7.0 * density +
			(big.xy - big.yx) * 2.0 * density;

		/* Pinpoint glints catching the light in the densest frost. */
		float glint = hash12(floor(p / 3.0) + seed);
		sparkle = step(0.996, glint) * density;

		/* Frosted glass: a small cross blur that widens with density. */
		vec2 uv = v_uv + bend * px_to_uv;
		vec2 r = (0.6 + 2.6 * density) * abs(px_to_uv);
		color = sample_client(uv) * 0.36 +
			(sample_client(uv + vec2(r.x, 0.0)) +
			 sample_client(uv - vec2(r.x, 0.0)) +
			 sample_client(uv + vec2(0.0, r.y)) +
			 sample_client(uv - vec2(0.0, r.y))) * 0.16;

		/* Cold grade: drain some colour, then shift toward blue. */
		float luma = dot(color.rgb, vec3(0.299, 0.587, 0.114));
		color.rgb = mix(color.rgb, vec3(luma), 0.35 * grow);
		color.rgb *= mix(vec3(1.0), vec3(0.86, 0.95, 1.08), grow);

		/* A thin haze over everything, opaque rime where the frost is. */
		color = mix(color, vec4(FROST, 1.0), 0.06 * grow);
		color = mix(color, vec4(FROST, 1.0), density * 0.74);
		float body = max(color.a, density);
		color.rgb += FROST * crystal * density * 0.32 * body;
		color.rgb += ICE_EDGE * front_glow * 0.30 * body;
		color.rgb += vec3(1.0) * sparkle * 0.9;
		color.a = max(color.a, sparkle);

		/* Inner rim of the frame catches the cold first. */
		float rim = exp(-edge_px / 7.0) * grow;
		color.rgb += ICE_EDGE * rim * 0.22 * body;

		/* A soft specular sheen on the ice. */
		vec3 n = normalize(v_normal);
		vec3 l = normalize(u_light_dir);
		vec3 view_dir = normalize(vec3(0.16, -0.10, 1.0));
		float spec = pow(max(dot(reflect(-l, n), view_dir), 0.0), 24.0);
		color.rgb += FROST * spec * 0.22 * grow * body;

		/* Meltwater: thin drips sliding down while the ice gives way. */
		if (thawing > 0.5) {
			float lane = 17.0;
			float col = floor(p.x / lane);
			float h = hash12(vec2(col, seed));
			float running = step(0.45, h);
			float dx = abs(fract(p.x / lane) - 0.5) * lane;
			float speed = 60.0 + 90.0 * hash12(vec2(seed, col));
			float span = frame.y + 120.0;
			float head = frame.y - mod(u_time * speed + h * span, span);
			float above = p.y - head;
			float trail = step(0.0, above) * (1.0 - smoothstep(0.0, 70.0, above));
			float bead = 1.0 - smoothstep(2.0, 4.0, length(vec2(dx, above * 0.8)));
			float drip = max(trail * (1.0 - smoothstep(0.8, 2.0, dx)), bead);
			drip *= running * smoothstep(0.0, 0.3, grow);
			vec2 wet_uv = v_uv + (bend + vec2(sign(fract(p.x / lane) - 0.5) * 3.0, 0.0) * drip) * px_to_uv;
			color = mix(color, sample_client(wet_uv), drip * 0.7);
			color.rgb += ICE_EDGE * drip * 0.18 * max(color.a, drip);
		}
	} else {
		color = sample_client(v_uv);
	}

	float coverage = 1.0;
	if (u_frame_px.x > 0.0) {
		float d = frame_distance(frame_uv);
		float aa = frame_aa(d);
		coverage = clamp(0.5 - d / aa, 0.0, 1.0);
		float border_px = u_frame_shape.y;
		float border = step(0.0001, border_px) *
			clamp(0.5 + (d + border_px) / aa, 0.0, 1.0) * u_border_color.a;
		/* The border turns to clear ice as the window freezes. */
		vec3 border_rgb = mix(u_border_color.rgb, ICE_EDGE, grow * 0.85);
		color.rgb = color.rgb * (1.0 - border) + border_rgb * border;
		color.a = color.a + border * (1.0 - color.a);
	}

	color.rgb *= u_tint.rgb * u_brightness;
	/* Premultiplied output: global opacity and coverage scale RGB and A. */
	float alpha = u_tint.a * coverage;
	if (alpha <= 0.0) discard;
	gl_FragColor = color * alpha;
}
