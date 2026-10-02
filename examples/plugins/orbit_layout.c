#include <stddef.h>
#include <stdio.h>
#include <shady/plugin.h>
#include <shady/event.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static shady_window focused_window;

struct slot {
	float x;
	float y;
	float z;
};

/*
 * Slot zero is the primary/focused workspace.
 * The rest are deliberately asymmetric satellites.
 */
static const struct slot slots[] = {
	{ 0.00f,  0.01f, -0.06f },
	{-0.27f, -0.03f, -0.56f },
	{ 0.27f,  0.04f, -0.60f },
	{-0.36f,  0.20f, -0.68f },
	{ 0.36f, -0.18f, -0.72f },
	{-0.11f,  0.29f, -0.76f },
	{ 0.13f, -0.28f, -0.80f },
	{ 0.41f,  0.22f, -0.84f },
};

static shady_window first_mapped_except(shady_window excluded) {
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (window && window != excluded && api->window_mapped(window) &&
				api->window_visible(window))
			return window;
	}
	return NULL;
}

static bool mapped(shady_window window) {
	return window && api->window_valid(host, window) &&
		api->window_mapped(window);
}

static bool layout_managed(shady_window window) {
	return mapped(window) && api->window_visible(window) &&
		!api->window_maximized(window) && !api->window_fullscreen(window);
}

static void place_one(shady_window window, const struct slot *slot,
		int output_width, int output_height) {
	int window_width = 600, window_height = 380;
	api->window_size(window, &window_width, &window_height);

	double x = output_width * (0.5 + slot->x) - window_width * 0.5;
	double y = output_height * (0.5 + slot->y) - window_height * 0.5;

	api->window_set_position(host, window, x, y, slot->z);
}

static void place_windows(void) {
	if (!api->has_capability(host, "spatial.window-state"))
		return;

	size_t visible = 0;
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (layout_managed(window))
			visible++;
	}
	if (visible == 0) {
		focused_window = NULL;
		return;
	}

	if (!mapped(focused_window))
		focused_window = first_mapped_except(NULL);

	int width = 1280, height = 720;
	if (api->output_count(host) > 0) {
		shady_output output = api->output_at(host, 0);
		api->output_size(output, &width, &height);
	}
	if (width <= 0) width = 1280;
	if (height <= 0) height = 720;

	/* The focused window owns the readable, central primary position. */
	if (layout_managed(focused_window))
		place_one(focused_window, &slots[0], width, height);

	size_t satellite = 0;
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (!window || window == focused_window || !layout_managed(window))
			continue;

		size_t slot_index = 1 +
			(satellite % (sizeof(slots) / sizeof(slots[0]) - 1));
		struct slot slot = slots[slot_index];

		if (satellite >= sizeof(slots) / sizeof(slots[0]) - 1) {
			float drift = 0.035f * (float)(satellite - 6);
			slot.x += drift;
			slot.y -= drift * 0.5f;
			slot.z -= drift * 0.4f;
		}

		place_one(window, &slot, width, height);
		satellite++;
	}
}

static void on_event(shady_host event_host,
		const struct shady_event *event, void *user_data) {
	(void)event_host;
	(void)user_data;

	switch (event->type) {
	case SHADY_EVENT_WINDOW_MAPPED:
		/*
		 * XDG map is immediately followed by focus in the core. Treat the
		 * new window as primary here too, so there is no one-frame jump.
		 */
		focused_window = event->object.window;
		place_windows();
		break;
	case SHADY_EVENT_WINDOW_FOCUSED:
		focused_window = event->object.window;
		place_windows();
		break;
	case SHADY_EVENT_WINDOW_RESIZED:
	case SHADY_EVENT_WINDOW_STATE_CHANGED:
		place_windows();
		break;
	case SHADY_EVENT_WINDOW_UNMAPPED:
		if (focused_window == event->object.window)
			focused_window = first_mapped_except(event->object.window);
		place_windows();
		break;
	case SHADY_EVENT_WINDOW_DESTROYED:
		if (focused_window == event->object.window)
			focused_window = first_mapped_except(event->object.window);
		place_windows();
		break;
	default:
		break;
	}
}

static void start(struct shady_server *server) {
	(void)server;
	focused_window = first_mapped_except(NULL);
	api->log(SHADY_PLUGIN_LOG_INFO,
		"orbit-layout: focused window is primary; others orbit as satellites");
	place_windows();
}

static const char *const provides[] = {
	"rice.orbit-layout",
	NULL,
};

static const char *const requires[] = {
	"spatial.window-state",
	NULL,
};

static const struct shady_module module = {
	.name = "orbit-layout",
	.provides = provides,
	.requires = requires,
	.start = start,
};

const struct shady_module *shady_plugin_entry_v1(
		uint32_t host_abi,
		const struct shady_plugin_api_v1 *host_api,
		shady_host host_handle) {
	if (host_abi != SHADY_PLUGIN_ABI_V1 ||
			!host_api ||
			host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
			host_api->struct_size < sizeof(*host_api)) {
		return NULL;
	}

	api = host_api;
	host = host_handle;
	api->subscribe_event(host, SHADY_EVENT_WINDOW_MAPPED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_FOCUSED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_RESIZED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_STATE_CHANGED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_UNMAPPED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_DESTROYED, on_event, NULL);
	return &module;
}
