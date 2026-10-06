#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

/*
 * Afterglow: a low sun over a mirror-still sea.
 *
 * Every pixel is a real world-space view ray built from the host camera, so
 * the horizon, sun and sea stay put while the orbit camera moves. Above the
 * horizon: layered twilight gradient, sun disk with Mie halo, thin lit cloud
 * streaks and stars. Below: the sky mirrored in gently rippled water with a
 * glitter path under the sun.
 */

uniform vec2 u_resolution;
uniform float u_time;
uniform vec4 u_cam_pos;     /* xyz */
uniform vec4 u_cam_fwd;     /* xyz */
uniform vec4 u_cam_right;   /* xyz */
uniform vec4 u_cam_up;      /* xyz */
uniform vec2 u_lens;        /* tan(fov_y / 2), aspect */
uniform vec4 u_zenith;      /* rgb */
uniform vec4 u_mid;         /* rgb */
uniform vec4 u_horizon;     /* rgb */
uniform vec4 u_glow;        /* rgb: sun-side horizon glow; a: strength */
uniform vec4 u_sun_dir;     /* xyz unit; w: disk visibility 0..1 */
uniform vec4 u_sun_color;   /* rgb; a: halo strength */
uniform vec4 u_night;       /* x: stars, y: clouds, z: sea level, w: sea brightness */

varying vec2 v_uv;

const float PI = 3.14159265;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; i++) {
        v += a * noise(p);
        p = p * 2.03 + vec2(17.1, 3.7);
        a *= 0.5;
    }
    return v;
}

/* Sky colour along a direction that points at or above the horizon. */
vec3 sky(vec3 d, bool with_stars) {
    float elev = max(d.y, 0.0);
    float sun_cos = dot(d, u_sun_dir.xyz);
    float sun_side = 0.5 + 0.5 * sun_cos;

    /* Three-stop twilight gradient, compressed towards the horizon. */
    float h = pow(elev, 0.42);
    vec3 col = mix(u_horizon.rgb, u_mid.rgb, smoothstep(0.0, 0.45, h));
    col = mix(col, u_zenith.rgb, smoothstep(0.35, 1.0, h));

    /* Warm glow hugging the horizon, strongest towards the sun. */
    float belt = exp(-elev * 9.0) * (0.25 + 0.75 * pow(sun_side, 4.0));
    col += u_glow.rgb * belt * u_glow.a;
    /* Anti-twilight: faint rose belt opposite the sun. */
    col += u_glow.rgb * vec3(0.55, 0.35, 0.65) * exp(-elev * 14.0) *
        pow(1.0 - sun_side, 3.0) * 0.18 * u_glow.a;

    /* Sun: Mie halo + soft-edged disk with limb darkening. */
    float halo = pow(max(sun_cos, 0.0), 24.0) * 0.30 + pow(max(sun_cos, 0.0), 400.0) * 0.55;
    col += u_sun_color.rgb * halo * u_sun_color.a;
    float disk_r = 0.0105;
    float ang = acos(clamp(sun_cos, -1.0, 1.0));
    float disk = 1.0 - smoothstep(disk_r * 0.82, disk_r, ang);
    float limb = mix(0.72, 1.0, sqrt(max(1.0 - ang / disk_r, 0.0)));
    vec3 core = mix(u_sun_color.rgb, vec3(1.0, 0.97, 0.90), 0.55) * 2.4 * limb;
    col = mix(col, core, disk * u_sun_dir.w);

    /* Thin stratus streaks, lit from below by the low sun. */
    if (u_night.y > 0.001) {
        float az = atan(d.x, -d.z);
        vec2 cp = vec2(az * 2.6 + u_time * 0.004, elev * 34.0);
        float c = fbm(cp * vec2(1.0, 1.0) + vec2(0.0, fbm(cp * 0.5) * 1.6));
        float band = smoothstep(0.02, 0.07, elev) * (1.0 - smoothstep(0.16, 0.32, elev));
        float cover = smoothstep(0.52, 0.78, c) * band * u_night.y;
        vec3 lit = mix(u_mid.rgb * 0.55, u_sun_color.rgb * 1.05, pow(sun_side, 1.5));
        lit = mix(lit, u_glow.rgb, 0.25 * pow(sun_side, 4.0));
        col = mix(col, lit, cover * 0.85);
    }

    /* Stars: one candidate per cell on a lat/long grid, twinkling slowly. */
    if (with_stars && u_night.x > 0.001) {
        float az = atan(d.x, -d.z);
        vec2 sp = vec2(az * 60.0, asin(clamp(d.y, -1.0, 1.0)) * 60.0);
        vec2 cell = floor(sp);
        float rnd = hash12(cell);
        if (rnd > 0.86) {
            vec2 center = cell + 0.5 + (vec2(hash12(cell + 7.1), hash12(cell + 3.3)) - 0.5) * 0.6;
            float dist = length(sp - center);
            float size = mix(0.06, 0.16, hash12(cell + 11.7));
            float twinkle = 0.65 + 0.35 * sin(u_time * (0.8 + rnd * 2.0) + rnd * 40.0);
            float star = smoothstep(size, 0.0, dist) * twinkle;
            vec3 tint = mix(vec3(0.75, 0.82, 1.0), vec3(1.0, 0.86, 0.72), hash12(cell + 5.9));
            float fade = smoothstep(0.04, 0.30, elev) * u_night.x;
            col += tint * star * fade * 1.4;
        }
    }
    return col;
}

void main() {
    vec2 ndc = gl_FragCoord.xy / u_resolution * 2.0 - 1.0;
    /* Shady renders with a flipped Y, so screen-up is -up here. */
    vec3 d = normalize(u_cam_fwd.xyz +
        u_cam_right.xyz * ndc.x * u_lens.x * u_lens.y -
        u_cam_up.xyz * ndc.y * u_lens.x);

    vec3 col;
    if (d.y >= 0.0) {
        col = sky(d, true);
    } else {
        /* Sea: intersect the water plane and mirror the sky. */
        float sea = u_night.z;
        float t = (sea - u_cam_pos.y) / min(d.y, -0.0001);
        vec3 p = u_cam_pos.xyz + d * max(t, 0.0);
        float dist = max(t, 0.0);
        /* Two scrolling ripple layers; amplitude falls with distance so the
         * far sea goes glassy and the horizon stays crisp. */
        vec2 q = p.xz;
        float e = 0.08;
        float r0 = fbm(q * 3.0 + vec2(u_time * 0.05, u_time * 0.03)) +
            0.5 * fbm(q * 9.0 - vec2(u_time * 0.07, 0.0));
        float rx = fbm((q + vec2(e, 0.0)) * 3.0 + vec2(u_time * 0.05, u_time * 0.03)) +
            0.5 * fbm((q + vec2(e, 0.0)) * 9.0 - vec2(u_time * 0.07, 0.0));
        float rz = fbm((q + vec2(0.0, e)) * 3.0 + vec2(u_time * 0.05, u_time * 0.03)) +
            0.5 * fbm((q + vec2(0.0, e)) * 9.0 - vec2(u_time * 0.07, 0.0));
        float amp = 0.055 / (1.0 + dist * 0.45);
        vec3 n = normalize(vec3(-(rx - r0) / e * amp, 1.0, -(rz - r0) / e * amp));
        vec3 r = reflect(d, n);
        r.y = abs(r.y);
        vec3 refl = sky(normalize(r), false);

        /* Water is darker than the sky it mirrors: a deep body colour shows
         * through where we look down into it, the reflection takes over at
         * grazing angles towards the horizon. */
        float fres = 0.02 + 0.98 * pow(1.0 - max(dot(-d, n), 0.0), 5.0);
        vec3 deep = mix(u_zenith.rgb, u_mid.rgb, 0.25) * 0.28;
        col = mix(deep, refl * 0.78, clamp(fres, 0.0, 1.0)) * u_night.w;

        /* Glitter path: sharp sun specular on the ripples, stretched by the
         * grazing view into a vertical column. */
        vec3 hv = normalize(u_sun_dir.xyz - d);
        float spec = pow(max(dot(n, hv), 0.0), 520.0);
        float sun_up = smoothstep(-0.04, 0.02, u_sun_dir.y);
        /* Glitter scales with the halo so a silver moon path stays quieter
         * than the sun's. */
        col += u_sun_color.rgb * spec * (1.2 + 3.0 * u_sun_color.a * u_sun_color.a) * sun_up;
        float sheen = pow(max(dot(normalize(vec3(r.x, 0.0, r.z)),
            normalize(vec3(u_sun_dir.x, 0.0, u_sun_dir.z))), 0.0), 60.0);
        col += u_glow.rgb * sheen * 0.10 * u_glow.a * sun_up;

        /* Haze line: the sea meets the sky without a hard edge. */
        float horizon_haze = exp(d.y * 60.0);
        col = mix(col, sky(vec3(d.x, 0.0, d.z), false), horizon_haze * 0.85);
    }

    /* Highlight shoulder: values above 0.7 roll off smoothly instead of
     * clipping, so the sun keeps its colour and the disk stays brighter
     * than its halo. Shadows and mid-tones are untouched. */
    vec3 over = max(col - 0.7, 0.0);
    col = min(col, 0.7) + 0.3 * (1.0 - exp(-over / 0.3));

    /* Interleaved-gradient dither hides banding in the dark gradients. */
    float dither = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) - 0.5;
    col += dither / 255.0;
    gl_FragColor = vec4(col, 1.0);
}
