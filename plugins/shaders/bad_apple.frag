// bad-apple: the finished frame as a board of flip-dot tiles.
//
// Each tile is a card turning about its horizontal axis. Its front carries
// the part of the desktop behind it, its back is black. u_tiles holds every
// tile's angle in half turns (0 the front, 1 the back), one texel per tile of
// the video's grid. The grid is as tall as the output and centred
// horizontally; tiles beside it stay on their fronts.
//
// Uniforms: u_tiles, u_grid (columns, rows), u_fade (0..1, the board fading
// in over the plain desktop).

precision highp float;

uniform sampler2D u_scene;
uniform vec2 u_scene_size;
uniform sampler2D u_tiles;
uniform vec2 u_grid;
uniform float u_fade;

varying vec2 v_uv;

const float PI = 3.14159265;
// Space between tiles and the rounding of their corners, in tile widths.
const float GAP = 0.07;
const float CORNER = 0.16;

float rounded_box(vec2 p, vec2 half_size, float r) {
	vec2 q = abs(p) - (half_size - vec2(r));
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
	// Pixels from the top left of the output: Shady renders with a flipped
	// Y, so gl_FragCoord already counts down from the top.
	vec2 p = gl_FragCoord.xy;
	vec3 scene = texture2D(u_scene, p / u_scene_size).rgb;

	float tile = u_scene_size.y / u_grid.y;
	float x0 = (u_scene_size.x - u_grid.x * tile) * 0.5;
	vec2 g = vec2((p.x - x0) / tile, p.y / tile);
	vec2 cell = floor(g);
	vec2 f = fract(g) - 0.5;

	float turn = 0.0;
	if (cell.x >= 0.0 && cell.x < u_grid.x)
		turn = texture2D(u_tiles, (cell + 0.5) / u_grid).r;
	float angle = turn * PI;
	float c = cos(angle);
	float s = sin(angle);

	// The card seen edge-on is a thin line; its face is squashed by cos.
	float squash = max(abs(c), 0.02);
	vec2 local = vec2(f.x, f.y / squash);
	float d = rounded_box(local, vec2(0.5 - GAP), CORNER);
	float aa = 1.5 / tile;
	float face = 1.0 - smoothstep(-aa, aa, d * squash);

	vec3 board = vec3(0.015, 0.015, 0.02);
	vec3 col = board;
	if (c >= 0.0) {
		// Front: the desktop that lies under this tile, unsquashed.
		vec2 src = vec2((cell.x + 0.5 + local.x) * tile + x0, (cell.y + 0.5 + local.y) * tile);
		vec3 front = texture2D(u_scene, src / u_scene_size).rgb;
		// Turning away from the light dims it; the top edge catches it.
		float light = mix(0.55, 1.0, c) + 0.25 * s * smoothstep(0.1, -0.5, local.y);
		col = mix(board, front * light, face);
	} else {
		// Back: matte black with a faint sheen that moves as it turns.
		float sheen = 0.07 * (1.0 - abs(c)) + 0.035 * smoothstep(0.5, -0.5, local.y) * -c;
		col = mix(board, vec3(0.03, 0.03, 0.04) + sheen, face);
	}

	gl_FragColor = vec4(mix(scene, col, u_fade), 1.0);
}
