#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;

struct water_state {
	bool enabled;
};

static struct water_state *state(void) {
	return api->module_state(host, "water-windows");
}

static bool managed(shady_window window) {
	return window &&
		api->window_valid(host, window) &&
		api->window_mapped(window) &&
		api->window_visible(window) &&
		!api->window_maximized(window) &&
		!api->window_fullscreen(window);
}

static void apply_window(shady_window window, size_t rank) {
	struct water_state *s = state();
	if (!s || !managed(window))
		return;

	if (!s->enabled) {
		api->window_set_water_effect(host, window, 0.f, 8.f, 1.f, 0.f);
		return;
	}

	shady_window focused = api->focused_window(host);
	float amplitude = window == focused ? 0.050f : 0.034f;
	float frequency = 8.2f + (float)(rank % 3) * 0.65f;
	float speed = 1.35f + (float)(rank % 2) * 0.18f;
	float phase = (float)rank * 1.73f;
	api->window_set_water_effect(host, window,
		amplitude, frequency, speed, phase);
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

static void start(struct shady_server *server) {
	(void)server;
	struct water_state *s = state();
	if (!s) return;
	s->enabled = true;
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
		if (api->window_valid(host, window))
			api->window_set_water_effect(host, window, 0.f, 8.f, 1.f, 0.f);
	}
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
	.start = start,
	.stop = stop,
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
			!host_api->window_water_effect)
		return NULL;

	api = host_api;
	host = host_handle;

	api->subscribe_event(host, SHADY_EVENT_WINDOW_MAPPED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_FOCUSED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_STATE_CHANGED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WORKSPACE_CHANGED, on_event, NULL);

	return &module;
}
