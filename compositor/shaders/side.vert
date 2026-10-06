attribute vec3 a_pos;
attribute vec3 a_normal;
uniform mat4 u_mvp;
uniform mat4 u_model;
uniform vec2 u_wobble;
uniform vec2 u_corner; /* rounded-frame radius, normalized per axis */
varying vec3 v_normal;
varying float v_depth;

void main() {
    vec3 pos = a_pos;
    vec2 n2 = a_normal.xy;
    /* Walls run along the axis perpendicular to their normal. The mesh
     * encodes that coordinate in [-1, 0] / [0, 1] / [1, 2] for start corner,
     * straight span and end corner respectively. */
    bool along_y = abs(a_normal.x) > 0.5;
    float e = along_y ? pos.y : pos.x;
    float r_along = along_y ? u_corner.y : u_corner.x;
    float r_across = along_y ? u_corner.x : u_corner.y;
    float fixed_c = along_y ? pos.x : pos.y;
    float outward = fixed_c > 0.5 ? 1.0 : -1.0;
    float along = r_along + clamp(e, 0.0, 1.0) * (1.0 - 2.0 * r_along);
    float across = fixed_c;
    if (e < 0.0 || e > 1.0) {
        /* Each wall owns 45 degrees of the quarter circle; neighbours meet
         * on the diagonal. */
        float theta = (e < 0.0 ? -e : e - 1.0) * 0.78539816;
        float side = e < 0.0 ? -1.0 : 1.0;
        float corner_along = e < 0.0 ? r_along : 1.0 - r_along;
        float corner_across = fixed_c > 0.5 ? 1.0 - r_across : r_across;
        along = corner_along + side * sin(theta) * r_along;
        across = corner_across + outward * cos(theta) * r_across;
        /* Pre-scale by the normalized radius so the model's non-uniform
         * width/height scale turns this back into the true arc normal. */
        vec2 arc = vec2(outward * cos(theta), side * sin(theta));
        vec2 scaled = arc * vec2(max(r_across, 1e-5), max(r_along, 1e-5));
        n2 = along_y ? scaled : scaled.yx;
    }
    pos.xy = along_y ? vec2(across, along) : vec2(along, across);

    float bend_x = sin(pos.y * 3.14159265);
    float bend_y = sin(pos.x * 3.14159265);
    float cx = pos.x - 0.5;
    float cy = pos.y - 0.5;
    pos.x += u_wobble.x * bend_x * (0.75 + 0.25 * cos(cy * 3.14159265));
    pos.y += u_wobble.y * bend_y * (0.75 + 0.25 * cos(cx * 3.14159265));
    pos.x += u_wobble.y * cy * 0.18 * bend_y;
    pos.y += u_wobble.x * cx * 0.18 * bend_x;
    float depth_shape = sin(pos.x * 3.14159265) * sin(pos.y * 3.14159265);
    pos.z += (u_wobble.x * cy - u_wobble.y * cx) * 0.65 * depth_shape;
    gl_Position = u_mvp * vec4(pos, 1.0);
    gl_Position.y = -gl_Position.y;
    v_normal = normalize(mat3(u_model) * vec3(n2, a_normal.z));
    v_depth = -a_pos.z;
}
