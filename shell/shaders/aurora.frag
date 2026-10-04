// Aurora curtains drifting through a bar's background.
//
// A surface shader: it runs over the whole bar and gets the bar's cairo
// drawing as u_tex. Bright pixels (text, pills, the badge) are left alone so
// the bar stays readable; the light only shows in the dark glass.
//
// Uniforms: strength (0..1), accent and accent_2 (colours).

uniform sampler2D u_tex;
uniform float u_time;
uniform vec2 u_size;
uniform float u_strength;
uniform vec4 u_accent;
uniform vec4 u_accent_2;

varying vec2 v_uv;
varying vec2 v_local;

float wave(float x, float speed, float freq, float phase) {
    return sin(x * freq + u_time * speed + phase +
        1.7 * sin(x * freq * 0.37 - u_time * speed * 0.6));
}

void main() {
    vec4 c = texture2D(u_tex, v_uv);
    // One unit per ~40 logical px, so the bands keep their size on any bar.
    float x = v_local.x * u_size.x / 40.0;
    float a = wave(x, 0.35, 0.9, 0.0) * 0.5 + 0.5;
    float b = wave(x, -0.22, 1.7, 2.1) * 0.5 + 0.5;
    float curtain = pow(a * b, 1.5);
    // Curtains hang from the top and fade toward the bottom edge.
    float fall = mix(1.0, 0.35, v_local.y);
    vec3 aurora = mix(u_accent.rgb, u_accent_2.rgb, b) * curtain * fall;

    float luma = dot(c.rgb, vec3(0.299, 0.587, 0.114)) / max(c.a, 0.001);
    float keep = 1.0 - smoothstep(0.35, 0.75, luma);
    // Premultiplied: light is added in proportion to coverage and never
    // exceeds it.
    vec3 rgb = min(c.rgb + aurora * u_strength * c.a * keep, vec3(c.a));
    gl_FragColor = vec4(rgb, c.a);
}
