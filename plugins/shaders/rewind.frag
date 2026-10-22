// rewind: the scene drains toward an old photograph the further back the
// cursor is, and a timeline of keyframes runs along the bottom.
precision mediump float;

uniform sampler2D u_scene;
uniform vec2 u_scene_size;
uniform vec4 u_ticks[16]; // keyframe positions 0..1 (oldest..now), -1 unused
uniform float u_mix;      // fade in/out of the whole effect
uniform float u_cursor;   // the selected keyframe's position
uniform float u_time;

varying vec2 v_uv;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// Distance in pixels from p to the segment a-b.
float segment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

void main() {
    vec2 uv = gl_FragCoord.xy / u_scene_size;
    float past = u_mix * (0.3 + 0.7 * (1.0 - u_cursor));

    // A faint lateral split, as if the image were slipping out of register.
    float split = past * 1.6 / u_scene_size.x;
    vec3 scene = vec3(
        texture2D(u_scene, uv + vec2(split, 0.0)).r,
        texture2D(u_scene, uv).g,
        texture2D(u_scene, uv - vec2(split, 0.0)).b);

    float luma = dot(scene, vec3(0.299, 0.587, 0.114));
    vec3 sepia = vec3(luma * 1.07, luma * 0.92, luma * 0.74) + vec3(0.03, 0.015, 0.0);
    vec3 color = mix(scene, sepia, past * 0.85);

    vec2 centered = uv - 0.5;
    float vignette = 1.0 - dot(centered, centered) * 1.5;
    color *= mix(1.0, clamp(vignette, 0.0, 1.0), past);
    color += (hash(gl_FragCoord.xy + fract(u_time) * 91.0) - 0.5) * 0.06 * past;

    // Timeline. Rendering is Y-flipped: v_uv.y grows down the screen.
    vec2 px = vec2(v_uv.x, v_uv.y) * u_scene_size;
    float left = u_scene_size.x * 0.14;
    float right = u_scene_size.x * 0.86;
    float row = u_scene_size.y * 0.9;
    float scale = u_scene_size.y / 1080.0;

    float rail = segment(px, vec2(left, row), vec2(right, row));
    float ink = smoothstep(1.6 * scale, 0.4 * scale, rail) * 0.45;

    for (int i = 0; i < 16; i++) {
        vec4 t = u_ticks[i];
        for (int k = 0; k < 4; k++) {
            float p = k == 0 ? t.x : (k == 1 ? t.y : (k == 2 ? t.z : t.w));
            if (p < 0.0) continue;
            float x = mix(left, right, p);
            float d = segment(px, vec2(x, row - 7.0 * scale), vec2(x, row + 7.0 * scale));
            ink = max(ink, smoothstep(1.8 * scale, 0.5 * scale, d) * 0.8);
        }
    }

    float cx = mix(left, right, u_cursor);
    float dot_d = length(px - vec2(cx, row));
    float knob = smoothstep(7.0 * scale, 5.5 * scale, dot_d);
    float glow = exp(-dot_d / (18.0 * scale)) * 0.6;
    float stem = smoothstep(1.5 * scale, 0.4 * scale,
        segment(px, vec2(cx, row - 26.0 * scale), vec2(cx, row - 9.0 * scale)));

    vec3 amber = vec3(1.0, 0.78, 0.45);
    color = mix(color, vec3(0.96, 0.92, 0.84), ink * u_mix);
    color += amber * glow * u_mix;
    color = mix(color, amber, max(knob, stem * 0.8) * u_mix);

    gl_FragColor = vec4(color, 1.0);
}
