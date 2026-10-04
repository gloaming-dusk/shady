// A layer backdrop effect for shady.layer_effect (docs/SHADER_API.md):
// the scene behind the surface wobbles as if seen through rippled glass,
// with a faint frost. Animated through u_time.
//
//   shady.layer_effect("shady-shell", {
//       shader = os.getenv("SHADY_ROOT") .. "/examples/layer-effects/refract.frag",
//       uniforms = { strength = 0.6 },
//   })
uniform float u_strength;

vec4 effect() {
    vec2 p = gl_FragCoord.xy;
    vec2 ripple = vec2(sin(p.y * 0.08 + u_time), cos(p.x * 0.05)) * 6.0 * u_strength;
    return mix(scene(p + ripple), vec4(0.9, 0.95, 1.0, 1.0), 0.08);
}
