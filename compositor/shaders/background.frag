precision mediump float;
uniform vec3 u_top;
uniform vec3 u_horizon;
uniform vec3 u_bottom;
varying float v_y;
varying vec2 v_ndc;

vec3 sky_color(float y, vec2 ndc) {
    float horizon = smoothstep(0.18, 0.58, y);
    vec3 lower = mix(u_bottom, u_horizon, horizon);
    float upper_mix = smoothstep(0.48, 1.0, y);
    vec3 color = mix(lower, u_top, upper_mix);
    /* Soft horizon bloom: the brightest band of the gradient glows a touch,
     * giving the scene a light source instead of a flat backdrop. */
    float band = exp(-pow((y - 0.50) * 5.5, 2.0));
    color += u_horizon * band * 0.35;
    /* Gentle elliptical vignette keeps attention on the window field. */
    float vignette = smoothstep(1.55, 0.35, length(ndc * vec2(0.82, 1.0)));
    return color * mix(0.72, 1.0, vignette);
}

/* Interleaved-gradient noise; +/- half an 8-bit step hides banding in the
 * very dark gradients without visible grain. */
float dither() {
    return fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) - 0.5;
}

void main() {
    vec3 color = sky_color(v_y, v_ndc);
    gl_FragColor = vec4(color + dither() / 255.0, 1.0);
}
