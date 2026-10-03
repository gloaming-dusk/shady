#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;

struct overview_state {
	bool active;
	bool animating;
	size_t selected_rank;
};

struct overview_window_state {
	bool saved;
	double home_x;
	double home_y;
	float home_z;
};

static struct overview_state *state(void) {
	return api->module_state(host, "spatial-overview");
}

static struct overview_window_state *window_state(shady_window window) {
	return api->window_state(window, "spatial-overview");
}

static bool managed(shady_window window) {
	return window &&
		api->window_valid(host, window) &&
		api->window_mapped(window) &&
		api->window_visible(window) &&
		!api->window_maximized(window) &&
		!api->window_fullscreen(window);
}

static size_t managed_count(void) {
	size_t count = 0;
	for (size_t i = 0; i < api->window_count(host); i++) {
		if (managed(api->window_at(host, i)))
			count++;
	}
	return count;
}

static shady_window managed_at(size_t wanted) {
	size_t rank = 0;
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (!managed(window))
			continue;
		if (rank++ == wanted)
			return window;
	}
	return NULL;
}

static size_t managed_rank(shady_window needle) {
	size_t rank = 0;
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (!managed(window))
			continue;
		if (window == needle)
			return rank;
		rank++;
	}
	return rank;
}

static void output_size(int *width, int *height) {
	*width = 1280;
	*height = 720;
	if (api->output_count(host) == 0)
		return;
	shady_output output = api->output_at(host, 0);
	api->output_size(output, width, height);
	if (*width <= 0) *width = 1280;
	if (*height <= 0) *height = 720;
}

static void overview_target(shady_window window, double *x, double *y, float *z) {
	size_t count = managed_count();
	size_t rank = managed_rank(window);
	int width, height;
	output_size(&width, &height);

	size_t columns = count <= 2 ? count : (count <= 4 ? 2 : 3);
	if (columns == 0) columns = 1;
	size_t rows = (count + columns - 1) / columns;
	size_t col = rank % columns;
	size_t row = rank / columns;

	int window_width = 600, window_height = 380;
	api->window_size(window, &window_width, &window_height);
	if (window_width <= 0) window_width = 600;
	if (window_height <= 0) window_height = 380;

	const double span_x = width * 0.62;
	const double span_y = height * 0.45;
	double center_x = width * 0.5;
	double center_y = height * 0.52;

	double nx = columns <= 1 ? 0.0 :
		((double)col / (double)(columns - 1) - 0.5);
	double ny = rows <= 1 ? 0.0 :
		((double)row / (double)(rows - 1) - 0.5);

	*x = center_x + nx * span_x - window_width * 0.5;
	*y = center_y + ny * span_y - window_height * 0.5;
	struct overview_state *s = state();
	*z = -0.34f - 0.055f * (float)rank;
	if (s && s->active && rank == s->selected_rank)
		*z += 0.16f;
}

static void set_active(bool enabled) {
	struct overview_state *s = state();
	if (!s) return;

	if (enabled) {
		shady_window focused = api->focused_window(host);
		s->selected_rank = managed(focused) ? managed_rank(focused) : 0;
		for (size_t i = 0; i < api->window_count(host); i++) {
			shady_window window = api->window_at(host, i);
			if (!managed(window))
				continue;
			struct overview_window_state *ws = window_state(window);
			if (!ws) continue;
			if (api->window_position(window,
					&ws->home_x, &ws->home_y, &ws->home_z))
				ws->saved = true;
		}
	}

	s->active = enabled;
	s->animating = true;
	api->schedule_render(host);
}

static void move_selection(int dx, int dy) {
	struct overview_state *s = state();
	size_t count = managed_count();
	if (!s || !s->active || count == 0) return;

	size_t columns = count <= 2 ? count : (count <= 4 ? 2 : 3);
	if (columns == 0) columns = 1;
	size_t rank = s->selected_rank < count ? s->selected_rank : 0;
	size_t row = rank / columns;
	size_t col = rank % columns;
	size_t rows = (count + columns - 1) / columns;

	long next_row = (long)row + dy;
	long next_col = (long)col + dx;
	if (next_row < 0) next_row = 0;
	if (next_col < 0) next_col = 0;
	if ((size_t)next_row >= rows) next_row = (long)rows - 1;
	if ((size_t)next_col >= columns) next_col = (long)columns - 1;

	size_t next = (size_t)next_row * columns + (size_t)next_col;
	if (next >= count) next = count - 1;
	if (next == s->selected_rank) return;
	s->selected_rank = next;
	s->animating = true;
	api->schedule_render(host);
}

static bool key(struct shady_server *server, const xkb_keysym_t *syms,
		int nsyms, uint32_t key_state, uint32_t modifiers) {
	(void)server;
	if (key_state != SHADY_KEY_PRESSED)
		return false;

	struct overview_state *s = state();
	if (!s) return false;

	for (int i = 0; i < nsyms; i++) {
		xkb_keysym_t sym = syms[i];
		if ((modifiers & SHADY_MODIFIER_LOGO) &&
				(sym == XKB_KEY_o || sym == XKB_KEY_O)) {
			set_active(!s->active);
			return true;
		}
		if (!s->active)
			continue;
		switch (sym) {
		case XKB_KEY_Left:
			move_selection(-1, 0);
			return true;
		case XKB_KEY_Right:
			move_selection(1, 0);
			return true;
		case XKB_KEY_Up:
			move_selection(0, -1);
			return true;
		case XKB_KEY_Down:
			move_selection(0, 1);
			return true;
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter: {
			shady_window selected = managed_at(s->selected_rank);
			set_active(false);
			if (selected) api->window_focus(host, selected);
			return true;
		}
		case XKB_KEY_Escape:
			set_active(false);
			return true;
		default:
			break;
		}
	}
	return false;
}

static void tick(struct shady_server *server, float dt,
		float logical_w, float logical_h) {
	(void)server;
	(void)logical_w;
	(void)logical_h;

	struct overview_state *s = state();
	if (!s || !s->animating)
		return;

	float alpha = dt * 10.0f;
	if (alpha > 1.f) alpha = 1.f;
	bool moving = false;

	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (!managed(window))
			continue;

		struct overview_window_state *ws = window_state(window);
		if (!ws)
			continue;

		double current_x = 0.0, current_y = 0.0;
		float current_z = 0.f;
		if (!api->window_position(window, &current_x, &current_y, &current_z))
			continue;

		double target_x, target_y;
		float target_z;
		if (s->active) {
			if (!ws->saved) {
				ws->home_x = current_x;
				ws->home_y = current_y;
				ws->home_z = current_z;
				ws->saved = true;
			}
			overview_target(window, &target_x, &target_y, &target_z);
		} else {
			if (!ws->saved)
				continue;
			target_x = ws->home_x;
			target_y = ws->home_y;
			target_z = ws->home_z;
		}

		double next_x = current_x + (target_x - current_x) * alpha;
		double next_y = current_y + (target_y - current_y) * alpha;
		float next_z = current_z + (target_z - current_z) * alpha;

		/* x/y are committed through an integer scene-node API. Once the
		 * remaining distance is only a few pixels, interpolation can quantize
		 * to the same integer forever, so snap using the pre-step distance. */
		double dx = fabs(target_x - current_x);
		double dy = fabs(target_y - current_y);
		float dz = fabsf(target_z - current_z);
		if (dx <= 6.0 && dy <= 6.0 && dz < 0.005f) {
			next_x = target_x;
			next_y = target_y;
			next_z = target_z;
			if (!s->active)
				ws->saved = false;
		} else {
			moving = true;
		}

		api->window_set_position(host, window, next_x, next_y, next_z);
	}

	s->animating = moving;
	if (moving)
		api->schedule_render(host);
}

static void on_event(shady_host event_host,
		const struct shady_event *event, void *user_data) {
	(void)event_host;
	(void)user_data;
	struct overview_state *s = state();
	if (!s || !s->active)
		return;

	switch (event->type) {
	case SHADY_EVENT_WINDOW_MAPPED:
	case SHADY_EVENT_WINDOW_UNMAPPED:
	case SHADY_EVENT_WINDOW_DESTROYED:
	case SHADY_EVENT_WINDOW_RESIZED:
	case SHADY_EVENT_WINDOW_STATE_CHANGED:
		s->animating = true;
		api->schedule_render(host);
		break;
	default:
		break;
	}
}

static void start(struct shady_server *server) {
	(void)server;
	api->log(SHADY_PLUGIN_LOG_INFO,
		"spatial-overview: Super+O toggles a native 3D window overview");
}

static const char *const provides[] = {
	"spatial.overview",
	NULL,
};

static const char *const requires[] = {
	"spatial.window-state",
	NULL,
};

static const struct shady_module module = {
	.name = "spatial-overview",
	.provides = provides,
	.requires = requires,
	.state_size = sizeof(struct overview_state),
	.toplevel_state_size = sizeof(struct overview_window_state),
	.start = start,
	.key = key,
	.tick = tick,
};

const struct shady_module *shady_plugin_entry_v1(
		uint32_t host_abi,
		const struct shady_plugin_api_v1 *host_api,
		shady_host host_handle) {
	if (host_abi != SHADY_PLUGIN_ABI_V1 ||
			!host_api ||
			host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
			host_api->struct_size < sizeof(*host_api))
		return NULL;

	api = host_api;
	host = host_handle;
	api->subscribe_event(host, SHADY_EVENT_WINDOW_MAPPED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_UNMAPPED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_DESTROYED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_RESIZED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_STATE_CHANGED, on_event, NULL);
	return &module;
}
