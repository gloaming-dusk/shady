/* Ray tracing and time-driven noise need more range than mediump on GPUs
 * that execute it at 16-bit precision. Keep a GLES2 fallback. */
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

/*
 * Black hole (or white hole) post-process for the black-hole plugin.
 *
 * Drawn once per frame after the windows with shader_draw_fullscreen_scene,
 * so u_scene holds everything rendered so far. Every pixel is a world-space
 * camera ray; the hole sits at the 3D point the windows fall into.
 *
 *   - gravitational lensing: rays are bent towards the hole by ~ R / b and the
 *     scene is re-sampled along the bent ray, producing an Einstein ring and
 *     a mirrored image of whatever is behind the hole
 *   - frame dragging: the bent image is also twisted around the hole
 *   - shockwaves: expanding rings that refract the scene and split colour
 *   - accretion disk with a lensed far side, photon ring, halo, shadow
 *   - flash and screen shake driven by the plugin's events
 *
 * Cheap analytic approximations throughout, not a geodesic integrator.
 */

uniform sampler2D u_scene;
uniform vec2 u_resolution;
uniform float u_time;
uniform vec4 u_cam_pos;
uniform vec4 u_cam_fwd;
uniform vec4 u_cam_right;
uniform vec4 u_cam_up;
uniform vec2 u_lens;   /* tan(fov_y / 2), aspect */
uniform vec4 u_hole;   /* centre xyz, shadow radius at full size */
uniform vec4 u_state;  /* size (may overshoot 1), mode (0 black, 1 white), spin, lens strength */
uniform vec4 u_fx;     /* shake, flash, ring flare, rumble */
uniform vec4 u_shock0; /* age seconds (< 0 = unused), strength */
uniform vec4 u_shock1;
uniform vec4 u_shock2;

varying mediump vec2 v_uv;

const float PI = 3.14159265;
const float SHOCK_SPEED = 1.7;  /* world units per second */
const float SHOCK_WIDTH = 0.07;

float hash(vec2 p) {
	/* Bound the dot product even on the mediump fallback as time grows. */
	p = mod(p, 64.0);
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
		mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

vec3 ray_for(vec2 ndc) {
	return normalize(u_cam_fwd.xyz +
		u_cam_right.xyz * ndc.x * u_lens.x * u_lens.y -
		u_cam_up.xyz * ndc.y * u_lens.x);
}

/* Inverse of ray_for: where on screen (0..1) a world direction lands. */
vec2 screen_uv(vec3 d) {
	float f = max(dot(d, u_cam_fwd.xyz), 1e-3);
	vec2 ndc = vec2(dot(d, u_cam_right.xyz) / (f * u_lens.x * u_lens.y),
		-dot(d, u_cam_up.xyz) / (f * u_lens.x));
	return ndc * 0.5 + 0.5;
}

vec3 scene_at(vec2 uv) {
	/* Beyond the screen edge there is nothing to bend in; fade to space. */
	vec2 edge = smoothstep(vec2(-0.02), vec2(0.03), uv) *
		smoothstep(vec2(-0.02), vec2(0.03), 1.0 - uv);
	return texture2D(u_scene, clamp(uv, 0.0, 1.0)).rgb * mix(0.25, 1.0, edge.x * edge.y);
}

/* Rotate v around unit axis k by angle a. */
vec3 rotate(vec3 v, vec3 k, float a) {
	float s = sin(a), c = cos(a);
	return v * c + cross(k, v) * s + k * dot(k, v) * (1.0 - c);
}

/* Disk emission at world point p, seen along ray d. */
vec4 disk(vec3 p, vec3 d, vec3 c, vec3 n, float r_shadow, float white) {
	vec3 rel = p - c;
	float r = length(rel);
	float r_in = r_shadow * 1.55;
	float r_out = r_shadow * 5.2;
	if (r < r_in || r > r_out) return vec4(0.0);

	vec3 ax = normalize(cross(n, abs(n.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
	vec3 ay = cross(n, ax);
	float phi = atan(dot(rel, ay), dot(rel, ax));
	float x = clamp((r - r_in) / (r_out - r_in), 0.0, 1.0);

	/* Keplerian shear: inner gas laps the outer gas, smearing noise into arcs. */
	float spin = u_state.z * (white > 0.5 ? -1.0 : 1.0);
	float omega = 2.4 / pow(r / r_in, 1.5);
	float a = phi + u_time * omega * spin;
	float bands = noise(vec2(a * 3.0 / PI * 4.0, x * 7.0)) * 0.6 +
		noise(vec2(a * 3.0 / PI * 13.0, x * 23.0)) * 0.4;

	float profile = smoothstep(0.0, 0.06, x) * pow(1.0 - x, 1.6);
	vec3 inner = mix(vec3(1.0, 0.93, 0.80), vec3(0.85, 0.95, 1.0), white);
	vec3 outer = mix(vec3(1.0, 0.36, 0.08), vec3(0.30, 0.55, 1.0), white);
	vec3 col = mix(inner, outer, smoothstep(0.0, 0.75, x));

	/* Relativistic beaming: gas moving towards the viewer is brighter. */
	vec3 tangent = normalize(cross(n, rel)) * spin;
	float beam = 1.0 + 0.75 * dot(tangent, -d);

	float intensity = profile * (0.45 + 0.85 * bands) * beam * (2.2 + 1.5 * u_fx.z);
	return vec4(col * intensity, clamp(intensity, 0.0, 1.0));
}

/* Premultiplied "over": a in front of b. */
vec4 over(vec4 a, vec4 b) {
	return vec4(a.rgb + b.rgb * (1.0 - a.a), a.a + b.a * (1.0 - a.a));
}

/* Shockwave displacement profile at impact distance b. */
float shock(vec4 s, float b) {
	if (s.x < 0.0) return 0.0;
	float radius = s.x * SHOCK_SPEED;
	float x = (b - radius) / SHOCK_WIDTH;
	/* The Gaussian is negligible outside this band. Avoid squaring large
	 * distances in the mediump fallback. */
	if (abs(x) > 8.0) return 0.0;
	/* Leading compression, trailing rarefaction, fading as it spreads. */
	return s.y * x * exp(-x * x) * exp(-s.x * 1.6);
}

void main() {
	float size = max(u_state.x, 0.0);
	float white = step(0.5, u_state.y);
	float r_shadow = u_hole.w * size;

	/* Screen shake: jitter the whole view, hole included. */
	float shake = u_fx.x;
	vec2 jitter = (vec2(noise(vec2(u_time * 41.0, 1.3)), noise(vec2(3.7, u_time * 37.0))) - 0.5) *
		shake * 0.035;
	vec2 frag_uv = gl_FragCoord.xy / u_resolution;
	vec2 ndc = (frag_uv + jitter) * 2.0 - 1.0;
	vec3 d = ray_for(ndc);
	vec3 ro = u_cam_pos.xyz;
	vec3 c = u_hole.xyz;

	float tc = dot(c - ro, d);
	vec3 closest = ro + d * max(tc, 0.0);
	vec3 to_c = c - closest;
	float b = max(length(to_c), 1e-4);
	vec3 toward = to_c / b;
	vec3 axis = normalize(c - ro);

	/* --- bend the ray ------------------------------------------------- */
	vec3 bent = d;
	float lens = 0.0;
	if (tc > 0.0 && r_shadow > 1e-5) {
		/* Point-lens equation: a ray at angle theta from the hole sees angle
		 * theta - thetaE^2 / theta. Inside the Einstein radius the image is
		 * mirrored; on it, everything behind the hole smears into a ring. */
		float einstein = 2.3 * r_shadow * u_state.w;
		lens = clamp(einstein * einstein / (tc * b), 0.0, 1.2);
		bent = normalize(d + toward * tan(lens));
		/* Frame dragging twists the image around the hole. */
		float drag = u_state.z * 2.2 * pow(r_shadow / max(b, r_shadow), 2.0);
		bent = rotate(bent, axis, drag * (white > 0.5 ? -1.0 : 1.0));
	}
	/* Shockwaves push the image outwards/inwards as they pass. */
	float wave = shock(u_shock0, b) + shock(u_shock1, b) + shock(u_shock2, b);
	bent = normalize(bent - toward * wave * 0.09);

	/* Chromatic split grows with bending and shock strength. */
	vec2 uv = screen_uv(bent);
	vec2 split = (uv - frag_uv) * (0.02 + 0.2 * abs(wave)) + vec2(abs(wave) * 0.003, 0.0);
	split *= min(1.0, 0.012 / max(length(split), 1e-4));
	vec3 col = vec3(scene_at(uv + split).r, scene_at(uv).g, scene_at(uv - split).b);
	/* Lensed light piles up near the ring. */
	col *= 1.0 + 0.3 * smoothstep(0.1, 0.6, lens) + abs(wave) * 0.6;

	float core = 0.0;
	/* --- hole and disk ----------------------------------------------- */
	if (r_shadow > 1e-5 && tc > 0.0) {
		/* Nearly edge-on disk, tipped towards the camera: lensing lifts the
		 * far side into arcs over and under the shadow. */
		vec3 n = normalize(vec3(0.0, 1.0, 0.22));
		vec4 near_disk = vec4(0.0), far_disk = vec4(0.0);
		float dn = dot(d, n);
		if (abs(dn) > 1e-4) {
			float t = dot(c - ro, n) / dn;
			if (t > 0.0) {
				vec4 e = disk(ro + d * t, d, c, n, r_shadow, white);
				if (t < tc) near_disk = e;
				else far_disk = e;
			}
		}

		vec4 back = vec4(0.0);
		if (b > r_shadow) {
			float bend = clamp(1.9 * r_shadow / b, 0.0, 1.35);
			vec3 d2 = normalize(d + toward * tan(bend));
			float dn2 = dot(d2, n);
			if (abs(dn2) > 1e-4) {
				float t2 = dot(c - closest, n) / dn2;
				if (t2 > 0.0)
					back = disk(closest + d2 * t2, d2, c, n, r_shadow, white) *
						smoothstep(r_shadow, r_shadow * 1.25, b);
			}
		}
		back = over(back, far_disk * step(r_shadow, b));

		/* pow() is undefined for a negative base, even with exponent 2. */
		float ring_distance = clamp((b - r_shadow * 1.04) /
			(r_shadow * (0.04 + 0.05 * u_fx.z)), -8.0, 8.0);
		float ring = exp(-ring_distance * ring_distance);
		float halo = exp(-max(b - r_shadow, 0.0) / (r_shadow * 0.7)) * step(r_shadow, b);
		vec3 ring_col = mix(vec3(1.0, 0.80, 0.52), vec3(0.75, 0.88, 1.0), white);
		vec3 halo_col = mix(vec3(1.0, 0.38, 0.10), vec3(0.35, 0.55, 1.0), white);
		back = over(back, vec4(ring_col * ring * (1.0 + 2.0 * u_fx.z), ring * 0.9));
		back = over(back, vec4(halo_col * halo * (0.35 + 0.6 * u_fx.z), halo * 0.12));

		/* Shadow: captured rays. A white hole blazes instead. */
		core = 1.0 - smoothstep(r_shadow * 0.96, r_shadow * 1.0, b);
		vec3 core_col = white > 0.5
			? vec3(0.92, 0.96, 1.0) * (1.4 - 0.35 * b / r_shadow)
			: vec3(0.0);
		back = mix(back, vec4(core_col, 1.0), core);

		vec4 hole = over(near_disk, back);
		col = hole.rgb + col * (1.0 - hole.a);
	}

	/* --- flash, shock glow, rumble ----------------------------------- */
	vec3 flash_col = mix(vec3(1.0, 0.72, 0.42), vec3(0.85, 0.92, 1.0), white);
	float near_glow = exp(-b / max(u_hole.w * 5.0, 1e-3));
	col += flash_col * u_fx.y * (0.10 + 1.1 * near_glow);
	col += flash_col * abs(wave) * 0.35;
	/* The shadow swallows the flash too; a white hole keeps blazing. */
	col = mix(col, vec3(0.0), core * (1.0 - white));
	/* Deep rumble darkens the edges, as if the light itself were sinking. */
	vec2 vig = frag_uv - 0.5;
	col *= 1.0 - u_fx.w * 0.45 * smoothstep(0.15, 0.75, dot(vig, vig) * 2.0);

	gl_FragColor = vec4(min(col, vec3(1.0)), 1.0);
}
