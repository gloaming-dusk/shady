/*
 * black-hole: Super+H tears open a black hole in the middle of the scene and
 * every window on the current workspace is ripped into it. The swallowed
 * windows are parked on a hidden "black-hole" workspace; nothing is closed.
 * Press Super+H again and a white hole blasts them back onto the current
 * workspace.
 *
 * Two shaders do the work:
 *   - black_hole.frag is a post-process (shader_draw_fullscreen_scene) that
 *     lenses the whole scene around the hole and draws the disk, shockwaves,
 *     flash and screen shake
 *   - black_hole_window.* moves each window along its orbit, driven per
 *     window through window_set_shader_params, so the windows' real
 *     positions never change and an interrupted animation has nothing to undo
 *
 * Every dramatic moment (opening, each window crossing the horizon, the final
 * collapse, the white-hole blast) "kicks" a shockwave, shake, flash and flare
 * that decay on their own. Unloading or reloading the plugin returns every
 * swallowed window immediately.
 *
 * Requires the spatial module. Shaders are read from
 * $SHADY_ROOT/examples/plugins/shaders (default: the working directory).
 */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <shady/event.h>
#include <shady/plugin.h>

#define MAX_WINDOWS 64
#define MAX_SHOCKS 3
#define HIDDEN_WORKSPACE "black-hole"

/* World units are fractions of the output height; the output centre is the
 * world origin, so the hole sits there and slightly behind the window plane. */
#define HOLE_Z (-0.45f)
#define HOLE_RADIUS 0.11f

#define OPEN_SECONDS 0.45f
#define CLOSE_SECONDS 0.45f
#define FALL_SECONDS 1.05f
#define STAGGER_SECONDS 0.22f
/* Extra orbit angle accumulated while falling, in radians. */
#define ORBIT_SPIN 6.0f
/* Thickness of the window shell: one local z unit is this many logical px. */
#define WINDOW_DEPTH_PX 14.0f
/* Shockwaves are invisible once this old (see SHOCK_SPEED in the shader). */
#define SHOCK_LIFETIME 1.6f

enum phase { PHASE_IDLE, PHASE_OPENING, PHASE_ACTIVE, PHASE_CLOSING };
enum mode { MODE_SWALLOW, MODE_EMIT };

struct falling_window {
	shady_window window;
	float delay;    /* seconds after the hole opens */
	float progress; /* 0 = in place, 1 = inside the hole */
	bool started;
	bool done;
	bool hidden;    /* parked on HIDDEN_WORKSPACE */
};

struct shock {
	float age; /* < 0 = unused */
	float strength;
};

struct hole_state {
	enum phase phase;
	enum mode mode;
	float phase_time;
	float size;  /* hole size; overshoots 1 while opening */
	float clock; /* shader time */
	float logical_w, logical_h;

	/* Impact effects, all decaying towards zero. */
	float shake, flash, flare;
	float rumble; /* sustained low shake while windows fall */
	struct shock shocks[MAX_SHOCKS];

	struct falling_window windows[MAX_WINDOWS];
	size_t count;
};

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static shady_shader_program window_program, hole_program;
static shady_render_hook_id hook;
static struct hole_state hole;

static float clamp01(float v) {
	return v < 0.f ? 0.f : v > 1.f ? 1.f : v;
}

static float smooth(float t) {
	t = clamp01(t);
	return t * t * (3.f - 2.f * t);
}

/* Overshoots to ~1.17 before settling: the hole bursts open. */
static float ease_out_back(float t) {
	t = clamp01(t);
	const float c1 = 2.2f, c3 = c1 + 1.f;
	float u = t - 1.f;
	return 1.f + c3 * u * u * u + c1 * u * u;
}

static void log_info(const char *message) {
	api->log(SHADY_PLUGIN_LOG_INFO, message);
}

static bool window_alive(shady_window window) {
	return window && api->window_valid(host, window) && api->window_mapped(window);
}

static void forget(size_t i) {
	hole.windows[i] = hole.windows[--hole.count];
}

static void reset_state(void) {
	float w = hole.logical_w > 0.f ? hole.logical_w : 1920.f;
	float h = hole.logical_h > 0.f ? hole.logical_h : 1080.f;
	hole = (struct hole_state){ .logical_w = w, .logical_h = h };
	for (size_t i = 0; i < MAX_SHOCKS; i++) hole.shocks[i].age = -1.f;
}

/* ---- impacts ---------------------------------------------------------- */

static void kick(float shock_strength, float shake, float flash, float flare) {
	/* Use a free shock slot, else replace the oldest. */
	size_t slot = 0;
	for (size_t i = 0; i < MAX_SHOCKS; i++) {
		if (hole.shocks[i].age < 0.f) { slot = i; break; }
		if (hole.shocks[i].age > hole.shocks[slot].age) slot = i;
	}
	hole.shocks[slot] = (struct shock){ .age = 0.f, .strength = shock_strength };
	hole.shake = fmaxf(hole.shake, shake);
	hole.flash = fmaxf(hole.flash, flash);
	hole.flare = fmaxf(hole.flare, flare);
}

static bool effects_alive(void) {
	if (hole.size > 0.f || hole.shake > 0.004f || hole.flash > 0.004f || hole.flare > 0.004f)
		return true;
	for (size_t i = 0; i < MAX_SHOCKS; i++)
		if (hole.shocks[i].age >= 0.f) return true;
	return false;
}

static void decay_effects(float dt) {
	hole.shake = fmaxf(hole.shake * expf(-dt * 5.5f), hole.rumble * 0.22f);
	hole.flash *= expf(-dt * 6.0f);
	hole.flare *= expf(-dt * 4.0f);
	for (size_t i = 0; i < MAX_SHOCKS; i++) {
		if (hole.shocks[i].age < 0.f) continue;
		hole.shocks[i].age += dt;
		if (hole.shocks[i].age > SHOCK_LIFETIME) hole.shocks[i].age = -1.f;
	}
}

/* ---- per-window shader parameters ------------------------------------- */

/*
 * Orbit maths in logical pixels with +y up. The window centre starts at
 * offset `rel` from the hole and spirals in: the radius shrinks while the
 * angle advances, faster near the end like a real infall. `fall` is already
 * eased; `tremble` is a vertex jitter in pixels.
 */
static void apply_params(const struct falling_window *f, float fall, float tremble) {
	double x = 0, y = 0;
	float z = 0.f;
	int w = 0, h = 0;
	if (!api->window_position(f->window, &x, &y, &z) ||
			!api->window_size(f->window, &w, &h) || w <= 0 || h <= 0)
		return;

	float hx = hole.logical_w * 0.5f, hy = hole.logical_h * 0.5f;
	float rel_x = (float)x + w * 0.5f - hx;
	float rel_y = -((float)y + h * 0.5f - hy);
	float r0 = sqrtf(rel_x * rel_x + rel_y * rel_y);
	float a0 = atan2f(rel_y, rel_x);

	float orbit = ORBIT_SPIN * fall * fall * (hole.mode == MODE_EMIT ? -1.f : 1.f);
	float r = r0 * (1.f - fall);
	float px = r * cosf(a0 + orbit), py = r * sinf(a0 + orbit);
	float spin = orbit * 0.85f;

	/* Direction from the window's current centre to the hole, expressed in
	 * the window's own (pre-spin) frame so the stretch follows the hole. */
	float tx = -px, ty = -py;
	float len = sqrtf(tx * tx + ty * ty);
	if (len < 1e-3f) {
		tx = -cosf(a0 + orbit);
		ty = -sinf(a0 + orbit);
		len = 1.f;
	}
	tx /= len;
	ty /= len;
	float cs = cosf(-spin), sn = sinf(-spin);
	float dir_x = tx * cs - ty * sn, dir_y = tx * sn + ty * cs;

	float depth_local = (HOLE_Z - z) * fall * hole.logical_h / WINDOW_DEPTH_PX;

	const float params[SHADY_WINDOW_SHADER_PARAMS * 4] = {
		px - rel_x, py - rel_y, spin, fall,
		dir_x, dir_y, depth_local, hole.mode == MODE_EMIT ? 1.f : 0.f,
		(float)w, (float)h, tremble, 0.f,
		0.f, 0.f, 0.f, 0.f,
	};
	api->window_set_shader_params(host, f->window, params);
}

static void attach_shader(struct falling_window *f, float fall) {
	if (api->window_set_shader(host, f->window, window_program))
		apply_params(f, fall, 0.f);
}

/* ---- swallow / emit --------------------------------------------------- */

static void open_hole(enum mode mode) {
	hole.mode = mode;
	hole.phase = PHASE_OPENING;
	hole.phase_time = 0.f;
	if (mode == MODE_SWALLOW) kick(1.0f, 1.0f, 0.9f, 1.0f);
	else kick(1.4f, 1.3f, 1.2f, 1.0f);
	api->schedule_render(host);
}

static int compare_delay(const void *a, const void *b) {
	float da = ((const struct falling_window *)a)->delay;
	float db = ((const struct falling_window *)b)->delay;
	return (da > db) - (da < db);
}

/* Nearest windows go first; each one lands as its own impact. */
static void stagger(void) {
	float hx = hole.logical_w * 0.5f, hy = hole.logical_h * 0.5f;
	for (size_t i = 0; i < hole.count; i++) {
		double x = 0, y = 0;
		float z = 0.f;
		int w = 0, h = 0;
		api->window_position(hole.windows[i].window, &x, &y, &z);
		api->window_size(hole.windows[i].window, &w, &h);
		float dx = (float)x + w * 0.5f - hx, dy = (float)y + h * 0.5f - hy;
		hole.windows[i].delay = sqrtf(dx * dx + dy * dy);
	}
	qsort(hole.windows, hole.count, sizeof(hole.windows[0]), compare_delay);
	for (size_t i = 0; i < hole.count; i++)
		hole.windows[i].delay = (float)i * STAGGER_SECONDS;
}

static void begin_swallow(void) {
	hole.count = 0;
	for (size_t i = 0; i < api->window_count(host) && hole.count < MAX_WINDOWS; i++) {
		shady_window window = api->window_at(host, i);
		if (!window_alive(window) || !api->window_visible(window)) continue;
		hole.windows[hole.count++] = (struct falling_window){ .window = window };
	}
	stagger();
	for (size_t i = 0; i < hole.count; i++) attach_shader(&hole.windows[i], 0.f);
	open_hole(MODE_SWALLOW);
	char message[96];
	snprintf(message, sizeof(message), "black-hole: swallowing %zu window(s)", hole.count);
	log_info(message);
}

static void begin_emit(void) {
	const char *current = api->current_workspace(host);
	char target[128];
	snprintf(target, sizeof(target), "%s", current ? current : "1");
	for (size_t i = 0; i < hole.count;) {
		struct falling_window *f = &hole.windows[i];
		if (!window_alive(f->window)) {
			forget(i);
			continue;
		}
		api->window_move_to_workspace(host, f->window, target);
		*f = (struct falling_window){ .window = f->window, .progress = 1.f };
		attach_shader(f, 1.f);
		i++;
	}
	stagger();
	open_hole(MODE_EMIT);
	char message[96];
	snprintf(message, sizeof(message), "black-hole: white hole releasing %zu window(s)",
		hole.count);
	log_info(message);
}

/* Put everything back right now: used on unload, reload and shutdown. */
static void release_all(void) {
	const char *current = api->current_workspace(host);
	char target[128];
	snprintf(target, sizeof(target), "%s", current ? current : "1");
	for (size_t i = 0; i < hole.count; i++) {
		shady_window window = hole.windows[i].window;
		if (!window_alive(window)) continue;
		if (hole.windows[i].hidden) api->window_move_to_workspace(host, window, target);
		api->window_reset_shader(host, window);
	}
	reset_state();
}

/* ---- animation -------------------------------------------------------- */

/* Advance every falling window; returns true once all have finished. */
static bool step_windows(void) {
	bool all_done = true;
	for (size_t i = 0; i < hole.count;) {
		struct falling_window *f = &hole.windows[i];
		if (!window_alive(f->window)) {
			forget(i);
			continue;
		}
		if (!f->done) {
			float t = clamp01((hole.phase_time - f->delay) / FALL_SECONDS);
			float fall, tremble;
			if (hole.mode == MODE_SWALLOW) {
				/* Hold, shudder, then whip in with accelerating speed. */
				fall = powf(t, 2.2f);
				tremble = t <= 0.f ? 2.5f : 7.f * (1.f - smooth(t / 0.45f));
			} else {
				/* Shot out of the white hole, decelerating to a stop. */
				float u = 1.f - t;
				fall = u * u * u;
				tremble = 5.f * u * u;
				if (t > 0.f && !f->started) kick(0.45f, 0.45f, 0.35f, 0.8f);
			}
			if (t > 0.f) f->started = true;
			f->progress = hole.mode == MODE_SWALLOW ? t : 1.f - t;
			apply_params(f, fall, tremble);
			if (t >= 1.f) {
				f->done = true;
				api->window_reset_shader(host, f->window);
				if (hole.mode == MODE_SWALLOW) {
					api->window_move_to_workspace(host, f->window, HIDDEN_WORKSPACE);
					f->hidden = true;
					/* Crossing the horizon: one impact per window. */
					kick(0.6f, 0.7f, 0.5f, 1.0f);
				}
			} else {
				all_done = false;
			}
		}
		i++;
	}
	return all_done;
}

static void tick(struct shady_server *server, float dt, float logical_w, float logical_h) {
	(void)server;
	if (logical_w > 0.f && logical_h > 0.f) {
		hole.logical_w = logical_w;
		hole.logical_h = logical_h;
	}
	if (hole.phase == PHASE_IDLE && !effects_alive()) return;
	hole.phase_time += dt;
	hole.clock += dt;

	switch (hole.phase) {
	case PHASE_OPENING:
		hole.size = ease_out_back(hole.phase_time / OPEN_SECONDS);
		hole.rumble = smooth(hole.phase_time / OPEN_SECONDS);
		if (hole.phase_time >= OPEN_SECONDS) {
			hole.phase = PHASE_ACTIVE;
			hole.phase_time = 0.f;
		}
		break;
	case PHASE_ACTIVE:
		/* The horizon throbs while it feeds. */
		hole.size = 1.f + 0.04f * sinf(hole.clock * 9.f) * hole.rumble;
		if (step_windows()) {
			hole.phase = PHASE_CLOSING;
			hole.phase_time = 0.f;
			if (hole.mode == MODE_EMIT) hole.count = 0;
		}
		break;
	case PHASE_CLOSING: {
		/* Implode: slow at first, then snap shut with a final blast. */
		float t = clamp01(hole.phase_time / CLOSE_SECONDS);
		hole.size = 1.f - t * t * t;
		hole.rumble = 1.f - t;
		if (hole.phase_time >= CLOSE_SECONDS) {
			hole.phase = PHASE_IDLE;
			hole.size = 0.f;
			hole.rumble = 0.f;
			kick(1.3f, 1.2f, 1.0f, 0.f);
			log_info(hole.mode == MODE_SWALLOW
				? "black-hole: closed (Super+H releases the windows)"
				: "black-hole: closed");
		}
		break;
	}
	case PHASE_IDLE:
		break;
	}
	decay_effects(dt);
	api->schedule_render(host);
}

static bool key(struct shady_server *server, const xkb_keysym_t *syms, int nsyms,
		uint32_t state, uint32_t modifiers) {
	(void)server;
	if (state != SHADY_KEY_PRESSED || !(modifiers & SHADY_MODIFIER_LOGO)) return false;
	bool pressed = false;
	for (int i = 0; i < nsyms; i++)
		pressed |= syms[i] == XKB_KEY_h || syms[i] == XKB_KEY_H;
	if (!pressed) return false;

	if (hole.phase != PHASE_IDLE) {
		log_info("black-hole: busy, try again when the hole has closed");
		return true;
	}
	if (hole.count > 0) begin_emit();
	else begin_swallow();
	return true;
}

static void on_event(shady_host h, const struct shady_event *event, void *user) {
	(void)h;
	(void)user;
	if (event->type != SHADY_EVENT_WINDOW_DESTROYED) return;
	for (size_t i = 0; i < hole.count; i++) {
		if (hole.windows[i].window == event->object.window) {
			forget(i);
			return;
		}
	}
}

/* ---- the hole itself -------------------------------------------------- */

static void shock_uniform(shady_host h, const char *name, const struct shock *s) {
	api->shader_uniform_vec4(h, hole_program, name, s->age, s->strength, 0.f, 0.f);
}

static void draw_hole(shady_host h, const struct shady_render_context *ctx, void *user) {
	(void)user;
	if (!hole_program || !ctx || !effects_alive() || !SHADY_RENDER_CONTEXT_HAS(ctx, aspect))
		return;
	api->shader_uniform_vec2(h, hole_program, "u_resolution", (float)ctx->width, (float)ctx->height);
	api->shader_uniform_float(h, hole_program, "u_time", hole.clock);
	api->shader_uniform_vec4(h, hole_program, "u_cam_pos", ctx->camera_position[0],
		ctx->camera_position[1], ctx->camera_position[2], 0.f);
	api->shader_uniform_vec4(h, hole_program, "u_cam_fwd", ctx->camera_forward[0],
		ctx->camera_forward[1], ctx->camera_forward[2], 0.f);
	api->shader_uniform_vec4(h, hole_program, "u_cam_right", ctx->camera_right[0],
		ctx->camera_right[1], ctx->camera_right[2], 0.f);
	api->shader_uniform_vec4(h, hole_program, "u_cam_up", ctx->camera_up[0],
		ctx->camera_up[1], ctx->camera_up[2], 0.f);
	api->shader_uniform_vec2(h, hole_program, "u_lens", ctx->tan_half_fov_y, ctx->aspect);
	api->shader_uniform_vec4(h, hole_program, "u_hole", 0.f, 0.f, HOLE_Z, HOLE_RADIUS);
	api->shader_uniform_vec4(h, hole_program, "u_state", hole.size,
		hole.mode == MODE_EMIT ? 1.f : 0.f, 1.f, 1.f + 0.35f * hole.flare);
	api->shader_uniform_vec4(h, hole_program, "u_fx", hole.shake, hole.flash, hole.flare,
		hole.rumble);
	shock_uniform(h, "u_shock0", &hole.shocks[0]);
	shock_uniform(h, "u_shock1", &hole.shocks[1]);
	shock_uniform(h, "u_shock2", &hole.shocks[2]);
	static bool reported;
	bool drawn = api->shader_draw_fullscreen_scene(h, hole_program);
	if (!reported) {
		reported = true;
		api->log(drawn ? SHADY_PLUGIN_LOG_INFO : SHADY_PLUGIN_LOG_ERROR, drawn
			? "black-hole: lensing the scene"
			: "black-hole: scene capture failed; the hole cannot be drawn");
	}
}

/* ---- lifecycle -------------------------------------------------------- */

static shady_shader_program load_program(const char *vertex, const char *fragment) {
	const char *root = getenv("SHADY_ROOT");
	if (!root || !*root) root = ".";
	char vert[1024], frag[1024];
	snprintf(vert, sizeof(vert), "%s/examples/plugins/shaders/%s", root, vertex);
	snprintf(frag, sizeof(frag), "%s/examples/plugins/shaders/%s", root, fragment);
	return api->shader_program_create(host, vert, frag);
}

static void destroy(struct shady_server *server);

static bool init(struct shady_server *server) {
	window_program = load_program("black_hole_window.vert", "black_hole_window.frag");
	hole_program = load_program("overlay.vert", "black_hole.frag");
	/* After windows so the post-process lenses them too. */
	hook = api->render_hook_add(host, SHADY_RENDER_STAGE_AFTER_WINDOWS, draw_hole, NULL);
	if (!window_program || !hole_program || !hook) {
		api->log(SHADY_PLUGIN_LOG_ERROR, "black-hole: failed to build shaders or hooks");
		destroy(server);
		return false;
	}
	return true;
}

static void start(struct shady_server *server) {
	(void)server;
	log_info("black-hole: Super+H swallows the workspace; press again to release");
}

static void stop(struct shady_server *server) {
	(void)server;
	release_all();
}

static void destroy(struct shady_server *server) {
	(void)server;
	release_all();
	if (hook) api->render_hook_remove(host, hook);
	if (window_program) api->shader_program_destroy(host, window_program);
	if (hole_program) api->shader_program_destroy(host, hole_program);
	hook = 0;
	window_program = hole_program = 0;
}

static const char *const provides[] = { "spatial.black-hole", NULL };
static const char *const requires[] = { "spatial", NULL };

static const struct shady_module module = {
	.name = "black-hole",
	.provides = provides,
	.requires = requires,
	.init = init,
	.start = start,
	.stop = stop,
	.destroy = destroy,
	.key = key,
	.tick = tick,
};

const struct shady_module *shady_plugin_entry_v1(uint32_t host_abi,
		const struct shady_plugin_api_v1 *host_api, shady_host host_handle) {
	if (host_abi != SHADY_PLUGIN_ABI_V1 || !host_api ||
			host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
			!SHADY_API_HAS(host_api, window_set_shader_params) ||
			!SHADY_API_HAS(host_api, shader_draw_fullscreen_scene) ||
			!SHADY_API_HAS(host_api, render_hook_add) ||
			!SHADY_API_HAS(host_api, window_move_to_workspace))
		return NULL;
	api = host_api;
	host = host_handle;
	reset_state();
	api->subscribe_event(host, SHADY_EVENT_WINDOW_DESTROYED, on_event, NULL);
	return &module;
}
