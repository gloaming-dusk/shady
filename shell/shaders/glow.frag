// A light that runs around a widget's rounded edge and pulses gently;
// brighter while the pointer is over the widget.
//
// A widget shader: it covers the widget's rectangle and samples u_tex for
// the widget's own drawing underneath.
//
// Uniforms: color (the light), strength (0..1).

uniform sampler2D u_tex;
uniform float u_time;
uniform vec2 u_size;
uniform float u_radius;
uniform float u_hover;
uniform vec4 u_color;
uniform float u_strength;

varying vec2 v_uv;
varying vec2 v_local;

// Signed distance to a rounded rectangle centred on the origin.
float rounded_box(vec2 p, vec2 half_size, float r) {
    vec2 q = abs(p) - (half_size - vec2(r));
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec4 c = texture2D(u_tex, v_uv);
    vec2 p = (v_local - 0.5) * u_size;
    float d = rounded_box(p, u_size * 0.5, u_radius);

    // A bright spot sweeps around the rim; the whole rim breathes.
    float angle = atan(p.y, p.x * u_size.y / max(u_size.x, 1.0));
    float sweep = pow(0.5 + 0.5 * sin(angle - u_time * 2.4), 6.0);
    float breathe = 0.75 + 0.25 * sin(u_time * 2.0);
    float rim = exp(-abs(d + 1.0) * 1.1) * (0.35 + 0.65 * sweep) * breathe;
    float light = rim * u_strength * (1.0 + 0.6 * u_hover) * step(d, 0.5);

    float alpha = max(c.a, light);
    vec3 rgb = min(c.rgb + u_color.rgb * light, vec3(alpha));
    gl_FragColor = vec4(rgb, alpha);
}
