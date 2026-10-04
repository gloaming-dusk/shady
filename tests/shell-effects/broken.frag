// Does not compile; the widget must keep its normal drawing.
void main() {
    gl_FragColor = vec4(1.0, 0.0, 0.0) // missing component and semicolon
}
