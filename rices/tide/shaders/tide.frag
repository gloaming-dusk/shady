// Tide's wallpaper: a night sea seen from above. Two soft glows of the
// accents drift in deep blue, and slow swells cross the lower half as faint
// lines of light. It has no u_time, so it is drawn once per size and costs
// nothing afterwards.
//
// The surface's own drawing (u_tex) goes on top, so the lock screen can
// stand on the same sea.
//
// Uniforms: accent, accent_2, base (colours).

uniform sampler2D u_tex;
uniform vec2 u_size;
uniform vec4 u_accent;
uniform vec4 u_accent_2;
uniform vec4 u_base;

varying vec2 v_uv;
varying vec2 v_local;

float glow(vec2 p, vec2 c, vec2 r) {
    vec2 d = (p - c) / r;
    return exp(-dot(d, d));
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 p = v_local;
    float aspect = u_size.x / max(u_size.y, 1.0);
    vec2 q = vec2(p.x * aspect, p.y);

    vec3 deep = u_base.rgb;
    vec3 top = deep * 0.75;
    vec3 col = mix(top, deep * 1.25 + vec3(0.010, 0.012, 0.030), smoothstep(0.0, 1.0, p.y));

    col += u_accent.rgb * 0.16 * glow(q, vec2(0.22 * aspect, 0.30), vec2(0.55, 0.42));
    col += u_accent_2.rgb * 0.13 * glow(q, vec2(0.80 * aspect, 0.78), vec2(0.70, 0.40));
    col += mix(u_accent.rgb, u_accent_2.rgb, 0.5) * 0.05 * glow(q, vec2(0.55 * aspect, 0.55), vec2(1.2, 0.25));

    // Swells: thin bright crests on long, slightly bent waves.
    float y = p.y + 0.035 * sin(q.x * 2.3 + 1.3) + 0.02 * sin(q.x * 5.1);
    float bands = 0.0;
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        float at = 0.56 + fi * 0.085 + fi * fi * 0.004;
        float w = 0.0022 + fi * 0.0009;
        bands += exp(-pow((y - at) / w, 2.0)) * (0.30 - fi * 0.04);
    }
    float fade = smoothstep(0.45, 0.62, p.y) * (1.0 - smoothstep(0.92, 1.0, p.y));
    col += mix(u_accent_2.rgb, u_accent.rgb, p.x) * bands * fade * 0.22;

    // A little grain so the gradients do not band.
    col += (hash(p * u_size) - 0.5) / 255.0 * 1.5;
    vec4 ui = texture2D(u_tex, v_uv);
    gl_FragColor = vec4(ui.rgb + col * (1.0 - ui.a), 1.0);
}
