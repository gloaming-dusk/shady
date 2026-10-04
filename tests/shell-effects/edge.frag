// Surface shader: the bar's content, with its last 10% painted u_color.
uniform sampler2D u_tex;
uniform vec4 u_color;
varying vec2 v_uv;
void main() {
    gl_FragColor = v_uv.x > 0.9 ? u_color : texture2D(u_tex, v_uv);
}
