// Grey that changes every frame.
vec4 effect() {
    float v = fract(u_time * 0.77);
    return vec4(v, v, v, 1.0);
}
