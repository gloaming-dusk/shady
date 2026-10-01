#include "../../module/module.h"
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include "../../shady.h"
#include "fps.h"
#include "state.h"

static bool bind_matches(const struct shady_keybind *bind,
		xkb_keysym_t sym, uint32_t modifiers) {
	const uint32_t mask = WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT |
		WLR_MODIFIER_CTRL | WLR_MODIFIER_LOGO;
	return bind->sym == sym && bind->modifiers == (modifiers & mask);
}

static bool fps_key(struct shady_server *server, const xkb_keysym_t *syms,
		int nsyms, uint32_t state, uint32_t modifiers) {
	if (shady_fps_handle_key(server, syms, nsyms, state)) return true;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return false;
	for (int i = 0; i < nsyms; i++) {
		if (bind_matches(&server->config.bind_fps_capture, syms[i], modifiers))
			return shady_fps_toggle_capture(server);
		if (bind_matches(&server->config.bind_fps_toggle, syms[i], modifiers))
			return shady_fps_toggle(server);
	}
	return false;
}

static bool fps_motion(struct shady_server *server,
		struct wlr_pointer_motion_event *event) {
	return shady_fps_handle_motion(server, event->delta_x, event->delta_y);
}

static bool fps_absolute(struct shady_server *server,
		struct wlr_pointer_motion_absolute_event *event) {
	(void)event;
	return shady_fps_handle_motion(server, 0.0, 0.0);
}

static bool fps_button(struct shady_server *server,
		struct wlr_pointer_button_event *event, uint32_t modifiers) {
	(void)modifiers;
	return shady_fps_handle_button(server, event->button, event->state);
}

static bool fps_axis(struct shady_server *server,
		struct wlr_pointer_axis_event *event, uint32_t modifiers) {
	(void)modifiers;
	return shady_fps_handle_axis(server, event);
}

static void fps_tick(struct shady_server *server, float dt,
		float logical_w, float logical_h) {
	shady_fps_update(server, dt);
	shady_fps_update_held_window(server, logical_w, logical_h);
}

static void fps_gone(struct shady_toplevel *toplevel) {
	shady_fps_toplevel_gone(toplevel->server, toplevel);
}

static bool module_enabled(struct shady_server *server) {
	return server->config.spatial_mode;
}

static const char *const provides[] = { "spatial.fps", NULL };
static const char *const requires[] = { "spatial.window-state", NULL };
static const char *const optional_requires[] = {
	"spatial.physics", "spatial.window-motion", NULL
};

static const struct shady_module module = {
	.name = "fps",
	.provides = provides,
	.enabled = module_enabled,
	.requires = requires,
	.optional_requires = optional_requires,
	.state_size = sizeof(struct shady_fps_state),
	.toplevel_state_size = sizeof(struct shady_fps_toplevel_state),
	.key = fps_key,
	.pointer_motion = fps_motion,
	.pointer_motion_absolute = fps_absolute,
	.pointer_button = fps_button,
	.pointer_axis = fps_axis,
	.tick = fps_tick,
	.toplevel_unmap = fps_gone,
	.toplevel_destroy = fps_gone,
};

const struct shady_module *shady_fps_module(void) { return &module; }
