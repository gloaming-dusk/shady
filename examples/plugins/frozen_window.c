/*
 * frozen-window: Super+Z freezes the focused window. Frost crystallises in
 * from the frame edges, feathering into needle-like ice while the window
 * shivers once and its colours cool; the centre stays clear enough to keep
 * reading. Super+Z on a frozen window thaws it: the frost recedes back to
 * the edges and meltwater runs down the glass. Super+Shift+Z thaws every
 * frozen window, or freezes the whole workspace when none are frozen.
 *
 * Everything is drawn by frozen_window.* through window_set_shader_params;
 * the window itself is never moved, resized or blocked. A fully frozen
 * window is static, so it costs no extra frames once the ice has set.
 *
 * Frozen windows own the per-window shader slot, so another shader plugin
 * (water-windows, window-portal) that re-attaches its shader takes over the
 * window. Unloading or reloading the plugin thaws every window immediately.
 *
 * Requires the spatial module. Shaders are read from
 * $SHADY_ROOT/examples/plugins/shaders (default: the working directory).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <shady/event.h>
#include <shady/plugin.h>

#define MAX_WINDOWS 64
#define FREEZE_SECONDS 1.6f
#define THAW_SECONDS 1.3f
/* The window shudders once as the cold hits, then holds still. */
#define SHIVER_SECONDS 0.45f
#define SHIVER_PX 2.2f

struct frozen_window {
	shady_window window;
	float frost; /* 0 = clear, 1 = fully frozen */
	float shiver; /* seconds of shiver left */
	float seed; /* gives every window its own frost pattern */
	bool freezing; /* false = thawing */
};

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static shady_shader_program ice_program;
static struct frozen_window frozen[MAX_WINDOWS];
static size_t frozen_count;
static uint32_t next_seed;

static void log_info(const char *message) {
	api->log(SHADY_PLUGIN_LOG_INFO, message);
}

static float clamp01(float v) {
	return v < 0.f ? 0.f : v > 1.f ? 1.f : v;
}

static bool eligible(shady_window window) {
	return window &&
		api->window_valid(host, window) &&
		api->window_mapped(window) &&
		api->window_visible(window);
}

static struct frozen_window *find(shady_window window) {
	for (size_t i = 0; i < frozen_count; i++)
		if (frozen[i].window == window) return &frozen[i];
	return NULL;
}

static void forget(size_t index) {
	frozen[index] = frozen[--frozen_count];
}

static bool apply_params(const struct frozen_window *f) {
	int w = 0, h = 0;
	if (!api->window_size(f->window, &w, &h) || w <= 0 || h <= 0) {
		w = 1;
		h = 1;
	}
	float shiver = SHIVER_PX * clamp01(f->shiver / SHIVER_SECONDS);
	const float params[SHADY_WINDOW_SHADER_PARAMS * 4] = {
		f->frost, f->freezing ? 0.f : 1.f, f->seed, shiver,
		(float)w, (float)h, 0.f, 0.f,
		0.f, 0.f, 0.f, 0.f,
		0.f, 0.f, 0.f, 0.f,
	};
	return api->window_set_shader_params(host, f->window, params);
}

static void freeze(shady_window window) {
	struct frozen_window *f = find(window);
	if (f) {
		/* Re-freezing a thawing window picks up from its current frost. */
		f->freezing = true;
		f->shiver = SHIVER_SECONDS;
		/* Take the window back if another shader plugin claimed it. */
		if (!apply_params(f) && api->window_set_shader(host, window, ice_program))
			apply_params(f);
		return;
	}
	if (frozen_count == MAX_WINDOWS || !api->window_set_shader(host, window, ice_program))
		return;
	f = &frozen[frozen_count++];
	*f = (struct frozen_window){
		.window = window,
		.frost = 0.f,
		.shiver = SHIVER_SECONDS,
		/* Small offsets keep the noise lattice in a precise float range. */
		.seed = (float)(next_seed++ % 97u) * 3.71f,
		.freezing = true,
	};
	apply_params(f);
}

static void thaw(struct frozen_window *f) {
	f->freezing = false;
	f->shiver = 0.f;
	apply_params(f);
}

static void release_all(void) {
	for (size_t i = 0; i < frozen_count; i++)
		if (api->window_valid(host, frozen[i].window))
			api->window_reset_shader(host, frozen[i].window);
	frozen_count = 0;
}

static bool animating(void) {
	for (size_t i = 0; i < frozen_count; i++) {
		const struct frozen_window *f = &frozen[i];
		if (!f->freezing || f->frost < 1.f || f->shiver > 0.f) return true;
	}
	return false;
}

static void tick(struct shady_server *server, float dt, float logical_w, float logical_h) {
	(void)server;
	(void)logical_w;
	(void)logical_h;
	if (!animating()) return;

	size_t i = 0;
	while (i < frozen_count) {
		struct frozen_window *f = &frozen[i];
		if (!api->window_valid(host, f->window)) {
			forget(i);
			continue;
		}
		if (f->freezing) {
			f->frost = clamp01(f->frost + dt / FREEZE_SECONDS);
		} else {
			f->frost = clamp01(f->frost - dt / THAW_SECONDS);
			if (f->frost <= 0.f) {
				api->window_reset_shader(host, f->window);
				forget(i);
				continue;
			}
		}
		f->shiver = f->shiver > dt ? f->shiver - dt : 0.f;
		/* Another shader plugin took the window: let it go. */
		if (!apply_params(f)) {
			forget(i);
			continue;
		}
		i++;
	}
	api->schedule_render(host);
}

static void toggle_focused(void) {
	shady_window window = api->focused_window(host);
	if (!eligible(window)) {
		log_info("frozen-window: no focused window to freeze");
		return;
	}
	struct frozen_window *f = find(window);
	if (f && f->freezing) {
		thaw(f);
		log_info("frozen-window: thawing");
	} else {
		freeze(window);
		log_info("frozen-window: freezing");
	}
	api->schedule_render(host);
}

static void toggle_all(void) {
	bool any_frozen = false;
	for (size_t i = 0; i < frozen_count; i++)
		any_frozen |= frozen[i].freezing;

	if (any_frozen) {
		for (size_t i = 0; i < frozen_count; i++)
			if (frozen[i].freezing) thaw(&frozen[i]);
		log_info("frozen-window: thawing every window");
	} else {
		for (size_t i = 0; i < api->window_count(host); i++) {
			shady_window window = api->window_at(host, i);
			if (eligible(window)) freeze(window);
		}
		log_info("frozen-window: freezing the workspace");
	}
	api->schedule_render(host);
}

static bool key(struct shady_server *server, const xkb_keysym_t *syms, int nsyms,
		uint32_t state, uint32_t modifiers) {
	(void)server;
	if (state != SHADY_KEY_PRESSED || !(modifiers & SHADY_MODIFIER_LOGO)) return false;
	bool pressed = false;
	for (int i = 0; i < nsyms; i++)
		pressed |= syms[i] == XKB_KEY_z || syms[i] == XKB_KEY_Z;
	if (!pressed) return false;

	if (modifiers & SHADY_MODIFIER_SHIFT) toggle_all();
	else toggle_focused();
	return true;
}

static void on_event(shady_host h, const struct shady_event *event, void *user) {
	(void)h;
	(void)user;
	if (event->type != SHADY_EVENT_WINDOW_DESTROYED) return;
	for (size_t i = 0; i < frozen_count; i++) {
		if (frozen[i].window == event->object.window) {
			forget(i);
			return;
		}
	}
}

static bool init(struct shady_server *server) {
	(void)server;
	const char *root = getenv("SHADY_ROOT");
	if (!root || !*root) root = ".";
	char vert[1024], frag[1024];
	snprintf(vert, sizeof(vert), "%s/examples/plugins/shaders/frozen_window.vert", root);
	snprintf(frag, sizeof(frag), "%s/examples/plugins/shaders/frozen_window.frag", root);
	ice_program = api->shader_program_create(host, vert, frag);
	if (!ice_program) {
		api->log(SHADY_PLUGIN_LOG_ERROR, "frozen-window: failed to load plugin window shader");
		return false;
	}
	return true;
}

static void start(struct shady_server *server) {
	(void)server;
	log_info("frozen-window: Super+Z freezes/thaws the focused window, "
		"Super+Shift+Z the workspace");
}

static void stop(struct shady_server *server) {
	(void)server;
	release_all();
}

static void destroy(struct shady_server *server) {
	(void)server;
	release_all();
	if (ice_program) api->shader_program_destroy(host, ice_program);
	ice_program = 0;
}

static const char *const provides[] = { "spatial.frozen-window", NULL };
static const char *const requires[] = { "spatial", NULL };

static const struct shady_module module = {
	.name = "frozen-window",
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
			!host_api->shader_program_create ||
			!host_api->shader_program_destroy ||
			!host_api->window_set_shader ||
			!host_api->window_reset_shader ||
			!host_api->focused_window)
		return NULL;
	api = host_api;
	host = host_handle;
	frozen_count = 0;
	next_seed = 0;
	api->subscribe_event(host, SHADY_EVENT_WINDOW_DESTROYED, on_event, NULL);
	return &module;
}
