#ifdef GL_OES_standard_derivatives
#extension GL_OES_standard_derivatives : enable
#endif
precision mediump float;

/* Window colour while it falls into the black hole (see the .vert file). */

uniform sampler2D u_tex;
uniform vec4 u_tint;
uniform float u_has_alpha;
uniform float u_brightness;
uniform vec4 u_border_color;
uniform vec4 u_frame_rect;
uniform vec2 u_frame_px;
uniform vec2 u_frame_shape;

varying vec2 v_uv;
varying vec3 v_normal;
varying vec4 v_fx;

/* Rounded decorated frame, identical to the stock window shader, so the
 * window looks unchanged when the fall starts. Negative inside. */
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
	float fall = v_fx.x;
	float heat = v_fx.y;
	float fade = v_fx.z;
	float white = step(0.5, v_fx.w);

	vec4 color = texture2D(u_tex, v_uv);
	if (u_has_alpha < 0.5) color.a = 1.0;

	float coverage = 1.0;
	if (u_frame_px.x > 0.0) {
		float d = frame_distance();
		coverage = clamp(0.5 - d / frame_aa(d), 0.0, 1.0);
		float border_px = u_frame_shape.y;
		float border = step(0.0001, border_px) *
			clamp(0.5 + (d + border_px) / frame_aa(d), 0.0, 1.0) * u_border_color.a;
		color.rgb = color.rgb * (1.0 - border) + u_border_color.rgb * border;
		color.a = color.a + border * (1.0 - color.a);
	}

	/* Black hole: light loses energy climbing out of the well, so the window
	 * dims and shifts red; the leading edge glows like accretion matter.
	 * White hole: the reverse, a blue-white flash that settles to normal. */
	vec3 redshift = color.rgb * vec3(1.0, 0.38, 0.18);
	vec3 blueshift = mix(color.rgb, vec3(0.80, 0.90, 1.0), 0.65 * fall) * (1.0 + 0.6 * fall);
	color.rgb = mix(color.rgb, mix(redshift, blueshift, white), smoothstep(0.0, 0.6, fall));
	/* Falling in, the light is red-shifted away almost entirely before the
	 * window crosses the shadow, so it vanishes into the dark. */
	float dim = 1.0 - 0.92 * smoothstep(0.35, 0.95, fall);
	color.rgb *= mix(dim, 1.0, white);
	vec3 hot = mix(vec3(1.0, 0.55, 0.20), vec3(0.65, 0.85, 1.0), white);
	color.rgb += hot * heat * 0.9 * color.a * (1.0 - smoothstep(0.8, 1.0, fall));

	color.rgb *= u_tint.rgb * u_brightness;
	/* Premultiplied output. */
	float alpha = u_tint.a * fade * coverage;
	if (alpha <= 0.0) discard;
	gl_FragColor = vec4(color.rgb * alpha, color.a * alpha);
}
