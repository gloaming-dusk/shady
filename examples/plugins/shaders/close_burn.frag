precision mediump float;

uniform sampler2D u_tex;
uniform vec4 u_tint;
uniform float u_has_alpha;
uniform float u_brightness;
uniform float u_time;
uniform vec4 u_border_color;
uniform vec2 u_border_width;

varying vec2 v_uv;
varying vec2 v_frame_uv;
varying float v_burn_progress;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float value_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    v += value_noise(p) * 0.58;
    p = p * 2.03 + 17.1;
    v += value_noise(p) * 0.27;
    p = p * 2.01 + 31.7;
    v += value_noise(p) * 0.15;
    return v;
}

void main() {
    vec4 color = texture2D(u_tex, v_uv);
    if (u_has_alpha < 0.5)
        color.a = 1.0;

    color.rgb *= u_tint.rgb * u_brightness;
    color.rgb *= u_tint.a;
    color.a *= u_tint.a;

    float p = clamp(v_burn_progress, 0.0, 1.0);

    /* Burn rises from bottom to top, with noisy tongues and holes. */
    vec2 noise_uv = vec2(v_frame_uv.x * 7.0,
                         v_frame_uv.y * 9.0 - u_time * 0.45);
    float n = fbm(noise_uv);
    float fine = value_noise(v_frame_uv * 31.0 + vec2(u_time * 0.15, 0.0));
    float threshold = p * 1.22 - 0.10 + (n - 0.5) * 0.18 + (fine - 0.5) * 0.035;

    float signed_front = threshold - v_frame_uv.y;
    float burned = smoothstep(0.015, 0.075, signed_front);

    float ember_outer = 1.0 - smoothstep(0.00, 0.095, abs(signed_front));
    float ember_inner = 1.0 - smoothstep(0.00, 0.035, abs(signed_front));
    float ember = max(ember_outer * 0.70, ember_inner);

    /* Char the pixels just ahead of the transparent region. */
    float char_band = (1.0 - burned) *
        (1.0 - smoothstep(0.025, 0.15, abs(signed_front)));
    color.rgb *= 1.0 - char_band * 0.72;

    vec3 orange = vec3(1.00, 0.20, 0.015);
    vec3 yellow = vec3(1.00, 0.82, 0.16);
    vec3 flame = mix(orange, yellow,
        clamp(ember_inner + fine * 0.35, 0.0, 1.0));
    color.rgb += flame * ember * (0.55 + 0.55 * n);

    /* Small flickering tongues above the burn front. */
    float tongue_noise = fbm(vec2(v_frame_uv.x * 14.0 + u_time * 0.35,
                                  v_frame_uv.y * 18.0 - u_time * 1.8));
    float tongues = smoothstep(0.70, 0.94, tongue_noise) *
        (1.0 - smoothstep(0.02, 0.18, v_frame_uv.y - threshold));
    color.rgb += vec3(1.0, 0.28, 0.025) * tongues * 0.28;

    /* Preserve compositor border until the fire reaches it. */
    float border_enabled = step(0.000001,
        max(u_border_width.x, u_border_width.y));
    float border_x = min(v_uv.x, 1.0 - v_uv.x) /
        max(u_border_width.x, 0.000001);
    float border_y = min(v_uv.y, 1.0 - v_uv.y) /
        max(u_border_width.y, 0.000001);
    float border_mask = (1.0 - smoothstep(0.72, 1.0,
        min(border_x, border_y))) * border_enabled * (1.0 - burned);
    float border_alpha = clamp(border_mask * u_border_color.a, 0.0, 1.0);
    color.rgb = color.rgb * (1.0 - border_alpha) +
        u_border_color.rgb * border_alpha;
    color.a = color.a + border_alpha * (1.0 - color.a);

    /* Burned material disappears completely. */
    float survive = 1.0 - burned;
    color.rgb *= survive;
    color.a *= survive;

    /* Ensure the final frame is completely gone even in quiet noise pockets. */
    float final_fade = 1.0 - smoothstep(0.94, 1.0, p);
    color.rgb *= final_fade;
    color.a *= final_fade;

    gl_FragColor = color;
}
