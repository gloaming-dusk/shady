precision mediump float;

uniform sampler2D u_tex;
uniform sampler2D u_portal_tex;
uniform float u_portal_available;
uniform vec2 u_portal_size;
uniform vec2 u_window_size;
uniform float u_time;
uniform float u_has_alpha;
uniform vec4 u_tint;
uniform float u_brightness;
uniform vec4 u_border_color;
uniform vec2 u_border_width;

varying vec2 v_uv;
varying float v_portal_lens;

void main() {
    vec2 uv = v_uv;
    vec4 base = texture2D(u_tex, uv);
    if (u_has_alpha < 0.5) base.a = 1.0;

    vec2 p = uv - vec2(0.5);
    float target_aspect = max(u_window_size.x, 1.0) / max(u_window_size.y, 1.0);
    vec2 metric = p;
    metric.x *= target_aspect;
    float radius = length(metric);

    float inner = 1.0 - smoothstep(0.285, 0.305, radius);
    float ring = 1.0 - smoothstep(0.010, 0.026, abs(radius - 0.305));
    float halo = 1.0 - smoothstep(0.025, 0.085, abs(radius - 0.305));

    vec2 portal_uv = uv;
    float source_aspect = max(u_portal_size.x, 1.0) / max(u_portal_size.y, 1.0);
    if (source_aspect > target_aspect) {
        portal_uv.x = (portal_uv.x - 0.5) * (target_aspect / source_aspect) + 0.5;
    } else {
        portal_uv.y = (portal_uv.y - 0.5) * (source_aspect / target_aspect) + 0.5;
    }

    vec2 radial = metric / max(radius, 0.0001);
    radial.x /= target_aspect;
    float edge_refraction = exp(-abs(radius - 0.285) * 45.0);
    float pulse = 0.5 + 0.5 * sin(u_time * 2.3);
    vec2 swirl = vec2(-radial.y, radial.x) *
        (0.004 + 0.003 * pulse) * edge_refraction;
    portal_uv = clamp(portal_uv + radial * 0.012 * edge_refraction + swirl,
        vec2(0.002), vec2(0.998));

    vec4 portal = texture2D(u_portal_tex, portal_uv);
    float available = step(0.5, u_portal_available);
    float portal_mix = inner * available;
    vec4 color = mix(base, portal, portal_mix);

    vec3 cyan = vec3(0.08, 0.82, 1.00);
    vec3 violet = vec3(0.63, 0.20, 1.00);
    vec3 portal_glow = mix(cyan, violet,
        0.5 + 0.5 * sin(u_time * 1.6 + atan(metric.y, metric.x) * 3.0));
    color.rgb += portal_glow * ring * available * (0.23 + 0.10 * pulse);
    color.rgb += portal_glow * halo * available * 0.045;

    float lens_glass = v_portal_lens * available;
    color.rgb += vec3(0.03, 0.07, 0.10) * lens_glass * 0.18;

    float border_enabled = step(0.000001,
        max(u_border_width.x, u_border_width.y));
    float border_x = min(uv.x, 1.0 - uv.x) /
        max(u_border_width.x, 0.000001);
    float border_y = min(uv.y, 1.0 - uv.y) /
        max(u_border_width.y, 0.000001);
    float border_mask = (1.0 - smoothstep(0.72, 1.0,
        min(border_x, border_y))) * border_enabled;
    float border_alpha = clamp(border_mask * u_border_color.a, 0.0, 1.0);
    color.rgb = color.rgb * (1.0 - border_alpha) +
        u_border_color.rgb * border_alpha;
    color.a = color.a + border_alpha * (1.0 - color.a);

    color.rgb *= u_tint.rgb * u_brightness;
    color.rgb *= u_tint.a;
    color.a *= u_tint.a;

    gl_FragColor = color;
}
