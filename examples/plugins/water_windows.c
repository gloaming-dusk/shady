#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static shady_shader_program water_program;
static bool shader_logged;

struct water_state {
	bool enabled;
	float strength;
};

static struct water_state *state(void) {
	return api->module_state(host, "water-windows");
}

static bool managed(shady_window window) {
	return window &&
		api->window_valid(host, window) &&
		api->window_mapped(window) &&
		api->window_visible(window) &&
		!api->window_fullscreen(window);
}

static float surface_scale_for_window(shady_window window) {
	int width = 0, height = 0;
	if (!api->window_size(window, &width, &height) || width <= 0 || height <= 0)
		return 1.f;

	/* Specular/Fresnel that looks broad and natural on a large surface can
	 * become visually harsh on a small window. Fade only the surface-light
	 * layer with size; keep the underlying liquid geometry/refraction intact. */
	int min_dim = width < height ? width : height;
	if (min_dim <= 220) return 0.30f;
	if (min_dim >= 620) return 1.00f;
	float t = (float)(min_dim - 220) / 400.f;
	/* Smoothstep avoids a visible style jump while resizing. */
	return 0.30f + 0.70f * (t * t * (3.f - 2.f * t));
}

static void apply_window(shady_window window, size_t rank) {
	struct water_state *s = state();
	if (!s || !managed(window))
		return;

	if (!s->enabled) {
		api->window_reset_shader(host, window);
		api->window_set_water_effect(host, window, 0.f, 8.f, 1.f, 0.f);
		api->window_set_water_surface(host, window, 0.f, 0.f, 0.f, 0.f);
		return;
	}

	if (water_program && api->window_set_shader(host, window, water_program) && !shader_logged) {
		api->log(SHADY_PLUGIN_LOG_INFO,
			"water-windows: external window shader attached");
		shader_logged = true;
	}

	shady_window focused = api->focused_window(host);
	/* Strong enough to read as liquid on real application contents. The
	 * shader keeps the outer perimeter anchored so interaction still feels
	 * stable even when the interior visibly refracts. */
	float base = window == focused ? 0.120f : 0.086f;
	float amplitude = base * s->strength;
	float frequency = 7.4f + (float)(rank % 3) * 0.70f;
	float speed = 1.18f + (float)(rank % 2) * 0.16f;
	float phase = (float)rank * 1.73f;
	api->window_set_water_effect(host, window,
		amplitude, frequency, speed, phase);
	float surface_scale = surface_scale_for_window(window);
	float fresnel = (window == focused ? 1.28f : 1.00f) * s->strength * surface_scale;
	float specular = (window == focused ? 1.42f : 1.08f) * s->strength * surface_scale;
	float caustic = 1.10f * s->strength * (0.45f + 0.55f * surface_scale);
	float tint = 0.92f * s->strength * (0.65f + 0.35f * surface_scale);
	api->window_set_water_surface(host, window,
		fresnel, specular, caustic, tint);
}

static void apply_all(void) {
	size_t rank = 0;
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (!managed(window))
			continue;
		apply_window(window, rank++);
	}
	api->schedule_render(host);
}

static bool key(struct shady_server *server, const xkb_keysym_t *syms,
		int nsyms, uint32_t key_state, uint32_t modifiers) {
	(void)server;
	if (key_state != SHADY_KEY_PRESSED)
		return false;

	for (int i = 0; i < nsyms; i++) {
		if ((modifiers & SHADY_MODIFIER_LOGO) &&
				(syms[i] == XKB_KEY_w || syms[i] == XKB_KEY_W)) {
			struct water_state *s = state();
			if (!s) return false;
			s->enabled = !s->enabled;
			apply_all();
			api->log(SHADY_PLUGIN_LOG_INFO,
				s->enabled ? "water-windows: enabled" : "water-windows: disabled");
			return true;
		}
	}
	return false;
}

static void on_event(shady_host event_host,
		const struct shady_event *event, void *user_data) {
	(void)event_host;
	(void)user_data;
	switch (event->type) {
	case SHADY_EVENT_WINDOW_MAPPED:
	case SHADY_EVENT_WINDOW_FOCUSED:
	case SHADY_EVENT_WINDOW_STATE_CHANGED:
	case SHADY_EVENT_WORKSPACE_CHANGED:
		apply_all();
		break;
	default:
		break;
	}
}

static bool init(struct shady_server *server) {
	(void)server;
	const char *root = getenv("SHADY_ROOT");
	if (!root || !*root) root = ".";
	char vert[1024], frag[1024];
	snprintf(vert, sizeof(vert), "%s/examples/plugins/shaders/water_window.vert", root);
	snprintf(frag, sizeof(frag), "%s/examples/plugins/shaders/water_window.frag", root);
	water_program = api->shader_program_create(host, vert, frag);
	if (!water_program) {
		api->log(SHADY_PLUGIN_LOG_ERROR,
			"water-windows: failed to load plugin window shader");
		return false;
	}
	return true;
}

static void start(struct shady_server *server) {
	(void)server;
	struct water_state *s = state();
	if (!s) return;
	s->enabled = true;
	s->strength = 1.f;
	const char *strength_env = getenv("SHADY_WATER_STRENGTH");
	if (strength_env && *strength_env) {
		char *end = NULL;
		float parsed = strtof(strength_env, &end);
		if (end != strength_env && parsed >= 0.25f && parsed <= 1.5f)
			s->strength = parsed;
	}
	apply_all();
	api->log(SHADY_PLUGIN_LOG_INFO,
		"water-windows: Super+W toggles animated liquid window surfaces");
}

static void stop(struct shady_server *server) {
	(void)server;
	struct water_state *s = state();
	if (s) s->enabled = false;
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (api->window_valid(host, window)) {
			api->window_reset_shader(host, window);
			api->window_set_water_effect(host, window, 0.f, 8.f, 1.f, 0.f);
			api->window_set_water_surface(host, window, 0.f, 0.f, 0.f, 0.f);
		}
	}
}

static void destroy(struct shady_server *server) {
	(void)server;
	if (water_program) api->shader_program_destroy(host, water_program);
	water_program = 0;
	shader_logged = false;
}

static const char *const provides[] = {
	"spatial.water-windows",
	NULL,
};

static const char *const requires[] = {
	"spatial.window-state",
	NULL,
};

static const struct shady_module module = {
	.name = "water-windows",
	.provides = provides,
	.requires = requires,
	.state_size = sizeof(struct water_state),
	.init = init,
	.start = start,
	.stop = stop,
	.destroy = destroy,
	.key = key,
};

const struct shady_module *shady_plugin_entry_v1(
		uint32_t host_abi,
		const struct shady_plugin_api_v1 *host_api,
		shady_host host_handle) {
	if (host_abi != SHADY_PLUGIN_ABI_V1 ||
			!host_api ||
			host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
			host_api->struct_size < sizeof(*host_api) ||
			!host_api->window_set_water_effect ||
			!host_api->window_water_effect ||
			!host_api->window_set_water_surface ||
			!host_api->window_water_surface ||
			!host_api->shader_program_create ||
			!host_api->shader_program_destroy ||
			!host_api->window_set_shader ||
			!host_api->window_reset_shader)
		return NULL;

	api = host_api;
	host = host_handle;

	api->subscribe_event(host, SHADY_EVENT_WINDOW_MAPPED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_FOCUSED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_STATE_CHANGED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WORKSPACE_CHANGED, on_event, NULL);

	return &module;
}
