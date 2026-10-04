// Magenta over the layer's top half, green over its bottom half.
vec4 effect() {
    return local().y < 0.5 ? vec4(1.0, 0.0, 1.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0);
}
