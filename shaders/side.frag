precision mediump float;
uniform vec3 u_light_dir;
uniform vec4 u_base_color;
uniform vec2 u_wobble;
varying vec3 v_normal;
void main() {
    vec3 n = normalize(v_normal);
    vec3 l = normalize(u_light_dir);
    float diffuse = max(dot(n, l), 0.0);
    float rim = pow(1.0 - abs(n.z), 2.0);
    float light = 0.24 + diffuse * 0.76;
    vec3 color = u_base_color.rgb * light;
    color += vec3(0.035, 0.075, 0.13) * rim;
    gl_FragColor = vec4(color, u_base_color.a);
}
