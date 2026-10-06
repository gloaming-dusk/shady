precision highp float;
uniform float u_time;
uniform vec2 u_resolution;
varying vec2 v_uv;
mat2 rotate(float a) { float s=sin(a), c=cos(a); return mat2(c,-s,s,c); }
float hash(vec2 p) { return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }
void main() {
    vec2 uv = (v_uv - .5) * vec2(u_resolution.x / max(u_resolution.y,1.), 1.);
    vec3 ro = vec3(0., .38, 3.5);
    vec3 rd = normalize(vec3(uv * 1.8, -2.));
    ro.yz = rotate(-.16) * ro.yz;
    rd.yz = rotate(-.16) * rd.yz;
    vec3 color = vec3(.008,.012,.032);
    float t = 1.3;
    // Fixed work budget: emissive toroidal filaments, no texture assets.
    for (int i=0; i<48; ++i) {
        vec3 p = ro + rd * t;
        p.xz = rotate(u_time * .025) * p.xz;
        vec3 q = p;
        q.xy = rotate(.56) * q.xy;
        float ring = length(vec2(length(q.xz)-1.45,q.y));
        float ring2 = length(vec2(length(p.xy)-1.25,p.z));
        float a = atan(q.z,q.x);
        float ribs = .55 + .45 * sin(a*24. - u_time*1.2);
        vec3 hue = mix(vec3(.02,.65,1.),vec3(.72,.12,1.),.5+.5*sin(a*2.+u_time*.15));
        color += hue * ((.009 / (.006 + ring*ring)) * .08 + exp(-ring*40.) * .25) * ribs;
        color += vec3(.7,.15,.85) * ((.004 / (.006 + ring2*ring2)) * .08 + exp(-ring2*40.) * .22);
        t += .09;
    }
    float halo = exp(-length(uv-vec2(0.,.04))*2.8);
    color += vec3(.03,.025,.10) * halo;
    vec2 cells = floor(v_uv * u_resolution / 3.);
    float star = step(.9985,hash(cells));
    color += star * (.35+.3*sin(u_time*.4+hash(cells+2.)*6.)) * vec3(.65,.8,1.);
    color *= 1. - .42*smoothstep(.25,1.,length(uv));
    color = vec3(1.) - exp(-color*1.35);
    gl_FragColor = vec4(color,1.);
}
