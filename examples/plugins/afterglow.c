/*
 * Afterglow: a low sun over a mirror-still sea.
 *
 * Draws a camera-aware procedural sky and sea behind the spatial desktop and
 * cycles it through four times of day (Super+T / Super+Shift+T). The current
 * phase also drives the compositor's accent colours, so focus borders and the
 * floor grid always belong to the sky they sit under. Closing windows sink
 * into the sea.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shady/event.h>
#include <shady/plugin.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define PHASE_COUNT 4
#define TRANSITION_SECONDS 2.4f

struct rgb { float r, g, b; };

struct phase {
	const char *name;
	struct rgb zenith, mid, horizon, glow;
	float glow_strength;
	float sun_azimuth, sun_elevation, sun_disk;
	struct rgb sun;
	float halo;
	float stars, clouds, sea_brightness;
	struct rgb accent, grid;
};

static const struct phase phases[PHASE_COUNT] = {
	{
		.name = "golden hour",
		.zenith = { 0.10f, 0.16f, 0.36f }, .mid = { 0.44f, 0.38f, 0.62f },
		.horizon = { 1.00f, 0.74f, 0.50f }, .glow = { 1.00f, 0.62f, 0.30f },
		.glow_strength = 0.85f,
		.sun_azimuth = -0.58f, .sun_elevation = 0.110f, .sun_disk = 1.f,
		.sun = { 1.00f, 0.88f, 0.66f }, .halo = 0.9f,
		.stars = 0.f, .clouds = 0.9f, .sea_brightness = 1.0f,
		.accent = { 1.00f, 0.76f, 0.48f }, .grid = { 1.00f, 0.63f, 0.36f },
	},
	{
		.name = "afterglow",
		.zenith = { 0.055f, 0.060f, 0.200f }, .mid = { 0.34f, 0.16f, 0.42f },
		.horizon = { 1.00f, 0.50f, 0.38f }, .glow = { 1.00f, 0.45f, 0.22f },
		.glow_strength = 1.0f,
		.sun_azimuth = -0.58f, .sun_elevation = 0.022f, .sun_disk = 1.f,
		.sun = { 1.00f, 0.66f, 0.40f }, .halo = 1.0f,
		.stars = 0.18f, .clouds = 1.0f, .sea_brightness = 0.95f,
		.accent = { 1.00f, 0.69f, 0.44f }, .grid = { 1.00f, 0.48f, 0.36f },
	},
	{
		.name = "blue hour",
		.zenith = { 0.030f, 0.050f, 0.160f }, .mid = { 0.12f, 0.16f, 0.40f },
		.horizon = { 0.62f, 0.42f, 0.62f }, .glow = { 0.95f, 0.45f, 0.45f },
		.glow_strength = 0.55f,
		.sun_azimuth = -0.58f, .sun_elevation = -0.050f, .sun_disk = 0.f,
		.sun = { 1.00f, 0.55f, 0.45f }, .halo = 0.35f,
		.stars = 0.55f, .clouds = 0.7f, .sea_brightness = 0.85f,
		.accent = { 1.00f, 0.62f, 0.72f }, .grid = { 0.71f, 0.55f, 1.00f },
	},
	{
		/* The "sun" becomes a moon: higher, silver and on the other side. */
		.name = "night",
		.zenith = { 0.012f, 0.016f, 0.050f }, .mid = { 0.030f, 0.045f, 0.120f },
		.horizon = { 0.10f, 0.11f, 0.24f }, .glow = { 0.35f, 0.40f, 0.80f },
		.glow_strength = 0.25f,
		.sun_azimuth = 0.52f, .sun_elevation = 0.180f, .sun_disk = 1.f,
		.sun = { 0.80f, 0.86f, 1.00f }, .halo = 0.25f,
		.stars = 1.0f, .clouds = 0.35f, .sea_brightness = 0.80f,
		.accent = { 0.66f, 0.76f, 1.00f }, .grid = { 0.44f, 0.49f, 1.00f },
	},
};

static const struct shady_close_effect sink_effect = {
	.style = SHADY_CLOSE_EFFECT_SLIDE_FADE,
	.duration = 0.70f,
	.strength = 1.0f,
	.direction_x = 0.0f,
	.direction_y = -0.85f,
};

struct afterglow_state {
	int from, to;
	float blend; /* 0..1 progress from `from` to `to` */
	float time;
	bool initialized;
};

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static shady_shader_program sky;
static shady_render_hook_id hook;
static shady_subscription_id map_subscription;
static bool warned_camera;
static bool logged_draw;

static struct afterglow_state *state(void) {
	return api->module_state(host, "afterglow");
}

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static struct rgb mix_rgb(struct rgb a, struct rgb b, float t) {
	return (struct rgb){ lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t) };
}

static float ease(float t) {
	t = fminf(fmaxf(t, 0.f), 1.f);
	return t * t * (3.f - 2.f * t);
}

/* Current interpolated phase. */
static struct phase current_phase(void) {
	struct afterglow_state *s = state();
	const struct phase *a = &phases[s ? s->from : 1];
	const struct phase *b = &phases[s ? s->to : 1];
	float t = ease(s ? s->blend : 1.f);
	return (struct phase){
		.name = b->name,
		.zenith = mix_rgb(a->zenith, b->zenith, t),
		.mid = mix_rgb(a->mid, b->mid, t),
		.horizon = mix_rgb(a->horizon, b->horizon, t),
		.glow = mix_rgb(a->glow, b->glow, t),
		.glow_strength = lerpf(a->glow_strength, b->glow_strength, t),
		.sun_azimuth = lerpf(a->sun_azimuth, b->sun_azimuth, t),
		.sun_elevation = lerpf(a->sun_elevation, b->sun_elevation, t),
		.sun_disk = lerpf(a->sun_disk, b->sun_disk, t),
		.sun = mix_rgb(a->sun, b->sun, t),
		.halo = lerpf(a->halo, b->halo, t),
		.stars = lerpf(a->stars, b->stars, t),
		.clouds = lerpf(a->clouds, b->clouds, t),
		.sea_brightness = lerpf(a->sea_brightness, b->sea_brightness, t),
		.accent = mix_rgb(a->accent, b->accent, t),
		.grid = mix_rgb(a->grid, b->grid, t),
	};
}

static void set_color(const char *key, struct rgb c) {
	char value[16];
	snprintf(value, sizeof(value), "#%02X%02X%02X",
		(unsigned)lrintf(fminf(fmaxf(c.r, 0.f), 1.f) * 255.f),
		(unsigned)lrintf(fminf(fmaxf(c.g, 0.f), 1.f) * 255.f),
		(unsigned)lrintf(fminf(fmaxf(c.b, 0.f), 1.f) * 255.f));
	api->config_set(host, key, value);
}

/* Keep compositor-drawn accents in the same light as the sky. */
static void sync_config(const struct phase *p) {
	set_color("window_border_focus_color", p->accent);
	set_color("floor_grid_color", p->grid);
	/* The gradient background is only visible if the sky hook fails, but keep
	 * it on palette so that fallback still looks intentional. */
	set_color("background_top", p->zenith);
	set_color("background_horizon", p->horizon);
	set_color("background_bottom", mix_rgb(p->zenith, (struct rgb){ 0, 0, 0 }, 0.5f));
}

static void draw(shady_host h, const struct shady_render_context *ctx, void *user) {
	(void)user;
	if (!sky || !ctx) return;
	if (!SHADY_RENDER_CONTEXT_HAS(ctx, aspect)) {
		if (!warned_camera) {
			api->log(SHADY_PLUGIN_LOG_ERROR,
				"afterglow: host render context has no camera; sky disabled");
			warned_camera = true;
		}
		return;
	}
	struct phase p = current_phase();
	struct afterglow_state *s = state();
	float ce = cosf(p.sun_elevation);
	float sun_x = sinf(p.sun_azimuth) * ce;
	float sun_y = sinf(p.sun_elevation);
	float sun_z = -cosf(p.sun_azimuth) * ce;

	api->shader_uniform_vec2(h, sky, "u_resolution", (float)ctx->width, (float)ctx->height);
	api->shader_uniform_float(h, sky, "u_time", s ? s->time : 0.f);
	api->shader_uniform_vec4(h, sky, "u_cam_pos", ctx->camera_position[0],
		ctx->camera_position[1], ctx->camera_position[2], 0.f);
	api->shader_uniform_vec4(h, sky, "u_cam_fwd", ctx->camera_forward[0],
		ctx->camera_forward[1], ctx->camera_forward[2], 0.f);
	api->shader_uniform_vec4(h, sky, "u_cam_right", ctx->camera_right[0],
		ctx->camera_right[1], ctx->camera_right[2], 0.f);
	api->shader_uniform_vec4(h, sky, "u_cam_up", ctx->camera_up[0],
		ctx->camera_up[1], ctx->camera_up[2], 0.f);
	api->shader_uniform_vec2(h, sky, "u_lens", ctx->tan_half_fov_y, ctx->aspect);
	api->shader_uniform_vec4(h, sky, "u_zenith", p.zenith.r, p.zenith.g, p.zenith.b, 0.f);
	api->shader_uniform_vec4(h, sky, "u_mid", p.mid.r, p.mid.g, p.mid.b, 0.f);
	api->shader_uniform_vec4(h, sky, "u_horizon", p.horizon.r, p.horizon.g, p.horizon.b, 0.f);
	api->shader_uniform_vec4(h, sky, "u_glow", p.glow.r, p.glow.g, p.glow.b, p.glow_strength);
	api->shader_uniform_vec4(h, sky, "u_sun_dir", sun_x, sun_y, sun_z, p.sun_disk);
	api->shader_uniform_vec4(h, sky, "u_sun_color", p.sun.r, p.sun.g, p.sun.b, p.halo);
	/* Sea level sits on the world floor plane (see shady_world_floor). */
	api->shader_uniform_vec4(h, sky, "u_night", p.stars, p.clouds, -0.62f,
		p.sea_brightness);
	bool drawn = api->shader_draw_fullscreen(h, sky);
	if (!logged_draw) {
		api->log(drawn ? SHADY_PLUGIN_LOG_INFO : SHADY_PLUGIN_LOG_ERROR,
			drawn ? "afterglow: sky rendered" : "afterglow: sky draw failed");
		logged_draw = true;
	}
}

static void apply_close_effect(shady_window window) {
	if (window && api->window_valid(host, window) && api->window_mapped(window))
		api->window_set_close_effect(host, window, &sink_effect);
}

static void on_event(shady_host h, const struct shady_event *event, void *user) {
	(void)h; (void)user;
	if (event->type == SHADY_EVENT_WINDOW_MAPPED)
		for (size_t i = 0; i < api->window_count(host); i++)
			apply_close_effect(api->window_at(host, i));
}

static void go_to_phase(int next) {
	struct afterglow_state *s = state();
	if (!s) return;
	next = (next % PHASE_COUNT + PHASE_COUNT) % PHASE_COUNT;
	if (next == s->to) return;
	/* Start from wherever we are now, even mid-transition. */
	if (s->blend < 1.f) s->from = s->blend < 0.5f ? s->from : s->to;
	else s->from = s->to;
	s->to = next;
	s->blend = 0.f;
	char message[96];
	snprintf(message, sizeof(message), "afterglow: drifting to %s", phases[next].name);
	api->log(SHADY_PLUGIN_LOG_INFO, message);
	api->schedule_render(host);
}

static bool key(struct shady_server *server, const xkb_keysym_t *syms,
		int n, uint32_t pressed, uint32_t mods) {
	(void)server;
	if (pressed != SHADY_KEY_PRESSED || !(mods & SHADY_MODIFIER_LOGO) ||
			(mods & (SHADY_MODIFIER_CTRL | SHADY_MODIFIER_ALT)))
		return false;
	struct afterglow_state *s = state();
	if (!s) return false;
	for (int i = 0; i < n; i++) {
		if (syms[i] == XKB_KEY_t || syms[i] == XKB_KEY_T) {
			go_to_phase(s->to + ((mods & SHADY_MODIFIER_SHIFT) ? -1 : 1));
			return true;
		}
	}
	return false;
}

static void tick(struct shady_server *server, float dt, float w, float h) {
	(void)server; (void)w; (void)h;
	struct afterglow_state *s = state();
	if (!s) return;
	dt = fminf(fmaxf(dt, 0.f), 0.05f);
	/* Time only advances while frames are being produced anyway, so the sky
	 * never forces continuous rendering on an idle desktop. */
	s->time = fmodf(s->time + dt, 3600.f);
	if (s->blend < 1.f) {
		s->blend = fminf(1.f, s->blend + dt / TRANSITION_SECONDS);
		struct phase p = current_phase();
		sync_config(&p);
		api->schedule_render(host);
	}
}

static bool init(struct shady_server *server) {
	(void)server;
	const char *root = getenv("SHADY_ROOT");
	if (!root) root = ".";
	char vert[1024], frag[1024];
	snprintf(vert, sizeof(vert), "%s/examples/plugins/shaders/overlay.vert", root);
	snprintf(frag, sizeof(frag), "%s/examples/plugins/shaders/afterglow_sky.frag", root);
	sky = api->shader_program_create(host, vert, frag);
	if (!sky) {
		api->log(SHADY_PLUGIN_LOG_ERROR, "afterglow: sky shader failed to build");
		return false;
	}
	hook = api->render_hook_add(host, SHADY_RENDER_STAGE_AFTER_BACKGROUND, draw, NULL);
	if (!hook) {
		api->shader_program_destroy(host, sky);
		sky = 0;
		return false;
	}
	return true;
}

static void start(struct shady_server *server) {
	(void)server;
	struct afterglow_state *s = state();
	if (s && !s->initialized) {
		const char *wanted = getenv("SHADY_AFTERGLOW_PHASE");
		int phase = 1;
		for (int i = 0; wanted && i < PHASE_COUNT; i++)
			if (!strcmp(wanted, phases[i].name)) phase = i;
		*s = (struct afterglow_state){ .from = phase, .to = phase, .blend = 1.f,
			.initialized = true };
	}
	struct phase p = current_phase();
	sync_config(&p);
	map_subscription = api->subscribe_event_handle(host, SHADY_EVENT_WINDOW_MAPPED,
		on_event, NULL);
	for (size_t i = 0; i < api->window_count(host); i++)
		apply_close_effect(api->window_at(host, i));
	api->log(SHADY_PLUGIN_LOG_INFO,
		"afterglow: sun is setting; Super+T / Super+Shift+T change the hour");
	api->schedule_render(host);
}

static void stop(struct shady_server *server) {
	(void)server;
	if (map_subscription) api->unsubscribe_event(host, map_subscription);
	map_subscription = 0;
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (api->window_valid(host, window))
			api->window_reset_close_effect(host, window);
	}
}

static void destroy(struct shady_server *server) {
	(void)server;
	if (hook) api->render_hook_remove(host, hook);
	if (sky) api->shader_program_destroy(host, sky);
	hook = 0;
	sky = 0;
}

static const char *const provides[] = { "spatial.afterglow", NULL };
static const char *const requires[] = {
	"spatial.window-state", "spatial.close-animation", NULL,
};
static const struct shady_module module = {
	.name = "afterglow", .provides = provides, .requires = requires,
	.state_size = sizeof(struct afterglow_state),
	.init = init, .start = start, .stop = stop, .destroy = destroy,
	.key = key, .tick = tick,
};

const struct shady_module *shady_plugin_entry_v1(uint32_t abi,
		const struct shady_plugin_api_v1 *table, shady_host h) {
	if (abi != SHADY_PLUGIN_ABI_V1 || !table ||
			!SHADY_API_HAS(table, render_hook_add) ||
			!SHADY_API_HAS(table, window_set_close_effect) ||
			!SHADY_API_HAS(table, subscribe_event_handle))
		return NULL;
	api = table;
	host = h;
	return &module;
}
