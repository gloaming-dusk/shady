precision mediump float;
uniform vec3 u_light_dir;
uniform vec4 u_base_color;
uniform vec4 u_edge_color; /* frame accent; alpha 0 keeps the neutral shell */
uniform vec2 u_wobble;
varying vec3 v_normal;
varying float v_depth;
void main() {
    vec3 n = normalize(v_normal);
    vec3 l = normalize(u_light_dir);
    float diffuse = max(dot(n, l), 0.0);
    float rim = pow(1.0 - abs(n.z), 2.0);
    float light = 0.24 + diffuse * 0.76;
    /* Borrow a little of the frame accent so the shell reads as the same
     * material as the outline instead of an unrelated grey box. */
    vec3 base = mix(u_base_color.rgb, u_edge_color.rgb * 0.55, 0.35 * u_edge_color.a);
    vec3 color = base * light;
    color += vec3(0.035, 0.075, 0.13) * rim;
    /* The lip next to the front face catches the accent; the back recedes. */
    float lip = 1.0 - smoothstep(0.0, 0.35, v_depth);
    color += u_edge_color.rgb * u_edge_color.a * lip * 0.22;
    color *= mix(1.0, 0.72, smoothstep(0.3, 1.0, v_depth));
    gl_FragColor = vec4(color, u_base_color.a);
}
