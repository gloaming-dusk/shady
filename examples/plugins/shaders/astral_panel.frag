precision mediump float;
uniform sampler2D u_tex;
uniform float u_time;
uniform float u_has_alpha;
uniform vec4 u_tint;
uniform float u_brightness;
varying vec2 v_uv;
void main() {
    vec4 content = texture2D(u_tex, v_uv);
    if (u_has_alpha < .5) content.a = 1.;
    vec2 edge = min(v_uv, 1.-v_uv);
    float d = min(edge.x,edge.y);
    float rim = 1.-smoothstep(.003,.014,d);
    float halo = exp(-d*65.) * .12;
    float travel = pow(.5+.5*sin((v_uv.x+v_uv.y)*12.-u_time*1.4),6.);
    vec3 glow = mix(vec3(.05,.88,1.),vec3(.78,.25,1.),v_uv.y);
    float scan = sin(v_uv.y*700.+u_time*.5)*.006;
    vec3 rgb = content.rgb*u_tint.rgb*u_brightness + glow*(rim*(.35+.65*travel)+halo) + scan;
    float alpha = max(content.a,rim);
    gl_FragColor = vec4(rgb*u_tint.a,alpha*u_tint.a);
}
