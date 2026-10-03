/* Adapted from wlroots 0.20.2 TinyWL (CC0). See LICENSES/tinywl-CC0.txt. */
#include <linux/input-event-codes.h>
#include <libinput.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "shady.h"
#include "module/module.h"
#include "modules/desktop/ime.h"
#include "modules/desktop/state.h"
#include "render/render.h"

void reset_cursor_mode(struct shady_server *server) {
	server->cursor_mode = SHADY_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

static void process_cursor_move(struct shady_server *server) {
	struct shady_toplevel *toplevel =
		server->grabbed_toplevel;

	if (!toplevel) {
		return;
	}

	double new_x =
		server->cursor->x -
		server->grab_x;

	double new_y =
		server->cursor->y -
		server->grab_y;

	shady_modules_toplevel_moved(toplevel, new_x, new_y);

	wlr_scene_node_set_position(
		&toplevel->scene_tree->node,
		(int)new_x,
		(int)new_y
	);

	shady_render_schedule_all_outputs(
		server
	);
}

static void process_cursor_resize(struct shady_server *server) {
	struct shady_toplevel *toplevel = server->grabbed_toplevel;
	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;
	int new_left = server->grab_geobox.x;
	int new_right = server->grab_geobox.x + server->grab_geobox.width;
	int new_top = server->grab_geobox.y;
	int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

	if (server->resize_edges & WLR_EDGE_TOP) {
		new_top = border_y;
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_left = border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_right = border_x;
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}

	struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
		new_left - geo_box->x, new_top - geo_box->y);

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

static struct shady_toplevel *desktop_titlebar_at(struct shady_server *server,
		double lx, double ly) {
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node,
		lx, ly, NULL, NULL);
	if (!node || node->type != WLR_SCENE_NODE_BUFFER) return NULL;
	struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->titlebar_scene_buffer == buffer &&
				toplevel->mapped && !toplevel->fullscreen) {
			return toplevel;
		}
	}
	return NULL;
}

static struct shady_toplevel *desktop_toplevel_at(struct shady_server *server,
		double lx, double ly, struct wlr_surface **surface, double *sx, double *sy) {
	*surface = NULL;
	*sx = *sy = 0.0;
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node,
		lx, ly, sx, sy);
	if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	if (!scene_surface) {
		return NULL;
	}
	*surface = scene_surface->surface;
	struct wlr_surface *root = wlr_surface_get_root_surface(*surface);
	struct wlr_xdg_toplevel *xdg = wlr_xdg_toplevel_try_from_wlr_surface(root);
	if (!xdg) {
		return NULL;
	}
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->xdg_toplevel == xdg) {
			return toplevel;
		}
	}
	return NULL;
}

static bool overlay_surface_at(struct shady_server *server,
		double lx, double ly, struct wlr_surface **surface, double *sx, double *sy) {
	if (!server->overlay_tree) return false;
	struct wlr_scene_node *node = wlr_scene_node_at(&server->overlay_tree->node,
		lx, ly, sx, sy);
	if (!node || node->type != WLR_SCENE_NODE_BUFFER) return false;
	struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	if (!scene_surface) return false;
	*surface = scene_surface->surface;
	return true;
}

static struct shady_toplevel *toplevel_at_cursor(struct shady_server *server,
		double lx, double ly, struct wlr_surface **surface, double *sx, double *sy) {
	*surface = NULL;
	*sx = *sy = 0.0;
	/* Critical 2D shell/session-lock UI always wins over spatial picking. */
	if (overlay_surface_at(server, lx, ly, surface, sx, sy)) return NULL;

	struct shady_toplevel *toplevel = NULL;
	if (!shady_desktop_state(server)->session_locked && shady_modules_pick_surface(server, lx, ly,
			surface, sx, sy, &toplevel)) {
		return toplevel;
	}
	return desktop_toplevel_at(server, lx, ly, surface, sx, sy);
}

static void process_cursor_motion(struct shady_server *server, uint32_t time) {
	if (server->cursor_mode == SHADY_CURSOR_MOVE) {
		process_cursor_move(server);
		return;
	} else if (server->cursor_mode == SHADY_CURSOR_RESIZE) {
		process_cursor_resize(server);
		return;
	}

	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct shady_toplevel *toplevel = toplevel_at_cursor(server,
			server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (!toplevel) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	if (surface) {
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(seat);
	}
}

static void keyboard_handle_modifiers(
		struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	if (keyboard->binding_state) {
		xkb_state_update_mask(keyboard->binding_state,
			0, 0, 0, 0, 0, keyboard->wlr_keyboard->modifiers.group);
	}
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	if (!shady_ime_handle_modifiers(keyboard->server,
			&keyboard->wlr_keyboard->modifiers)) {
		wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
			&keyboard->wlr_keyboard->modifiers);
	}
}

static bool bind_matches(const struct shady_keybind *bind,
		xkb_keysym_t sym, uint32_t modifiers) {
	const uint32_t mask = WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT |
		WLR_MODIFIER_CTRL | WLR_MODIFIER_LOGO;
	return xkb_keysym_to_lower(bind->sym) == xkb_keysym_to_lower(sym) &&
		bind->modifiers == (modifiers & mask);
}

static bool handle_keybinding(struct shady_server *server,
		xkb_keysym_t sym, uint32_t modifiers) {
	struct shady_config *c = &server->config;
	if (bind_matches(&c->bind_quit, sym, modifiers)) {
		wl_display_terminate(server->wl_display);
		return true;
	}
	if (bind_matches(&c->bind_cycle_windows, sym, modifiers)) {
		struct shady_toplevel *candidate;
		wl_list_for_each_reverse(candidate, &server->toplevels, link) {
			if (candidate->scene_tree && candidate->scene_tree->node.enabled) {
				focus_toplevel(candidate);
				break;
			}
		}
		return true;
	}
	return false;
}

static void keyboard_handle_key(
		struct wl_listener *listener, void *data) {
	struct shady_keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
	struct shady_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;
	if (shady_desktop_state(server)->idle_notifier) {
		wlr_idle_notifier_v1_notify_activity(shady_desktop_state(server)->idle_notifier, server->seat);
	}

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	struct xkb_state *binding_state = keyboard->binding_state
		? keyboard->binding_state : keyboard->wlr_keyboard->xkb_state;
	int nsyms = xkb_state_key_get_syms(binding_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

	if (!shady_desktop_state(server)->session_locked) {
		handled = shady_modules_key(server, syms, nsyms,
			event->state, modifiers);
		if (!handled && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			for (int j = 0; j < nsyms && !handled; j++) {
				handled = handle_keybinding(server, syms[j], modifiers);
			}
		}
	}
	if (!handled) {
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		if (!shady_ime_handle_key(server, event->time_msec,
				event->keycode, event->state)) {
			wlr_seat_keyboard_notify_key(seat, event->time_msec,
				event->keycode, event->state);
		}
	}
}

void shady_input_update_seat_capabilities(struct shady_server *server) {
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	const char *backends = getenv("WLR_BACKENDS");
	bool headless = backends && strstr(backends, "headless");
	if (headless || !wl_list_empty(&server->keyboards))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(server->seat, caps);
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	struct shady_server *server = keyboard->server;
	bool was_current = wlr_seat_get_keyboard(server->seat) == keyboard->wlr_keyboard;
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	if (keyboard->binding_state) xkb_state_unref(keyboard->binding_state);
	free(keyboard);

	if (was_current) {
		if (!wl_list_empty(&server->keyboards)) {
			struct shady_keyboard *next = wl_container_of(
				server->keyboards.next, next, link);
			wlr_seat_set_keyboard(server->seat, next->wlr_keyboard);
		} else {
			wlr_seat_set_keyboard(server->seat, NULL);
		}
	}

	shady_input_update_seat_capabilities(server);
}

void shady_input_add_keyboard(struct shady_server *server,
		struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct shady_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	bool is_virtual = wlr_input_device_get_virtual_keyboard(device) != NULL;
	if (!is_virtual) {
		struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
			XKB_KEYMAP_COMPILE_NO_FLAGS);
		keyboard->binding_state = xkb_state_new(keymap);
		wlr_keyboard_set_keymap(wlr_keyboard, keymap);
		xkb_keymap_unref(keymap);
		xkb_context_unref(context);
		wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);
	}

	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	wl_list_insert(&server->keyboards, &keyboard->link);

	/* A headless compositor can gain its first keyboard after a window was
	 * already focused (for example via zwp_virtual_keyboard_v1). Restore
	 * keyboard focus to the topmost visible mapped toplevel in that case. */
	if (!server->seat->keyboard_state.focused_surface) {
		struct shady_toplevel *toplevel;
		wl_list_for_each(toplevel, &server->toplevels, link) {
			if (!toplevel->mapped || !toplevel->scene_tree ||
					!toplevel->scene_tree->node.enabled) continue;
			struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
			wlr_seat_keyboard_notify_enter(server->seat, surface,
				keyboard->wlr_keyboard->keycodes,
				keyboard->wlr_keyboard->num_keycodes,
				&keyboard->wlr_keyboard->modifiers);
			break;
		}
	}
}

static void configure_libinput_pointer(struct wlr_input_device *device) {
	if (!wlr_input_device_is_libinput(device)) return;
	struct libinput_device *libinput_device =
		wlr_libinput_get_device_handle(device);
	if (!libinput_device ||
			!libinput_device_config_dwt_is_available(libinput_device)) return;

	enum libinput_config_status status =
		libinput_device_config_dwt_set_enabled(libinput_device,
			LIBINPUT_CONFIG_DWT_DISABLED);
	if (status == LIBINPUT_CONFIG_STATUS_SUCCESS) {
		wlr_log(WLR_INFO,
			"input: disabled touchpad disable-while-typing for %s",
			device->name ? device->name : "pointer");
	} else {
		wlr_log(WLR_ERROR,
			"input: failed to disable touchpad disable-while-typing for %s",
			device->name ? device->name : "pointer");
	}
}

static void server_new_pointer(struct shady_server *server,
		struct wlr_input_device *device) {
	configure_libinput_pointer(device);
	wlr_cursor_attach_input_device(server->cursor, device);
}

void server_new_input(struct wl_listener *listener, void *data) {
	struct shady_server *server =
		wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		shady_input_add_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}
	shady_input_update_seat_capabilities(server);
}

void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(
			listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
		server->seat->pointer_state.focused_client;
	if (focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
	}
}

void seat_pointer_focus_change(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(
			listener, server, pointer_focus_change);
	struct wlr_seat_pointer_focus_change_event *event = data;

	if (shady_desktop_state(server)->active_pointer_constraint) {
		wlr_pointer_constraint_v1_send_deactivated(
			shady_desktop_state(server)->active_pointer_constraint);
		shady_desktop_state(server)->active_pointer_constraint = NULL;
	}

	if (event->new_surface && shady_desktop_state(server)->pointer_constraints) {
		struct wlr_pointer_constraint_v1 *constraint =
			wlr_pointer_constraints_v1_constraint_for_surface(
				shady_desktop_state(server)->pointer_constraints, event->new_surface, server->seat);
		if (constraint) {
			shady_desktop_state(server)->active_pointer_constraint = constraint;
			wlr_pointer_constraint_v1_send_activated(constraint);
		}
	}

	if (event->new_surface == NULL) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
}

void seat_request_set_selection(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(
			listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void seat_request_set_primary_selection(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(
			listener, server, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

void server_cursor_motion(struct wl_listener *listener, void *data) {
	struct shady_server *server =
		wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	if (shady_desktop_state(server)->idle_notifier) {
		wlr_idle_notifier_v1_notify_activity(shady_desktop_state(server)->idle_notifier, server->seat);
	}
	if (shady_desktop_state(server)->relative_pointer_manager) {
		wlr_relative_pointer_manager_v1_send_relative_motion(
			shady_desktop_state(server)->relative_pointer_manager, server->seat,
			(uint64_t)event->time_msec * 1000,
			event->delta_x, event->delta_y,
			event->unaccel_dx, event->unaccel_dy);
	}
	if (shady_modules_pointer_motion(server, event))
		return;
	if (shady_desktop_state(server)->active_pointer_constraint &&
			shady_desktop_state(server)->active_pointer_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
		return;
	}
	wlr_cursor_move(server->cursor, &event->pointer->base,
			event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

void server_cursor_motion_absolute(
		struct wl_listener *listener, void *data) {
	struct shady_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	if (shady_desktop_state(server)->idle_notifier) {
		wlr_idle_notifier_v1_notify_activity(shady_desktop_state(server)->idle_notifier, server->seat);
	}
	if (shady_modules_pointer_motion_absolute(server, event))
		return;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
		event->y);
	process_cursor_motion(server, event->time_msec);
}

static uint32_t seat_modifiers(struct shady_server *server) {
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (!keyboard) {
		return 0;
	}
	return wlr_keyboard_get_modifiers(keyboard);
}

void server_cursor_button(struct wl_listener *listener, void *data) {
	struct shady_server *server =
		wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	if (shady_desktop_state(server)->idle_notifier) {
		wlr_idle_notifier_v1_notify_activity(shady_desktop_state(server)->idle_notifier, server->seat);
	}
	uint32_t mods = seat_modifiers(server);

	if (shady_modules_pointer_button(server, event, mods))
		return;

	if (event->button == BTN_LEFT) {
		struct shady_toplevel *titlebar = desktop_titlebar_at(server,
			server->cursor->x, server->cursor->y);
		if (titlebar) {
			if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
				focus_toplevel(titlebar);
				shady_toplevel_begin_interactive(titlebar, SHADY_CURSOR_MOVE, 0);
			} else if (server->cursor_mode == SHADY_CURSOR_MOVE &&
					server->grabbed_toplevel == titlebar) {
				reset_cursor_mode(server);
			}
			wlr_seat_pointer_clear_focus(server->seat);
			return;
		}
	}

	wlr_seat_pointer_notify_button(server->seat,
			event->time_msec, event->button, event->state);
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		reset_cursor_mode(server);
	} else {
		double sx, sy;
		struct wlr_surface *surface = NULL;
		struct shady_toplevel *toplevel = toplevel_at_cursor(server,
				server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		focus_toplevel(toplevel);
	}
}

void server_cursor_axis(struct wl_listener *listener, void *data) {
	struct shady_server *server =
		wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	if (shady_desktop_state(server)->idle_notifier) {
		wlr_idle_notifier_v1_notify_activity(shady_desktop_state(server)->idle_notifier, server->seat);
	}

	uint32_t mods = seat_modifiers(server);
	if (shady_modules_pointer_axis(server, event, mods))
		return;

	wlr_seat_pointer_notify_axis(server->seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

void server_cursor_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_server *server =
		wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

static uint32_t automation_time_msec(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

bool shady_input_automation_key(struct shady_server *server, xkb_keysym_t sym,
		uint32_t modifiers, bool pressed) {
	uint32_t state = pressed ? WL_KEYBOARD_KEY_STATE_PRESSED :
		WL_KEYBOARD_KEY_STATE_RELEASED;
	bool handled = false;
	if (!shady_desktop_state(server)->session_locked) {
		handled = shady_modules_key(server, &sym, 1, state, modifiers);
		if (!handled && pressed) handled = handle_keybinding(server, sym, modifiers);
	}
	return handled;
}

void shady_input_automation_pointer_move(struct shady_server *server,
		double x, double y) {
	if (!wlr_cursor_warp(server->cursor, NULL, x, y)) {
		wlr_cursor_warp_closest(server->cursor, NULL, x, y);
	}
	process_cursor_motion(server, automation_time_msec());
}

void shady_input_automation_pointer_button(struct shady_server *server,
		uint32_t button, bool pressed) {
	struct wlr_pointer_button_event event = {0};
	event.time_msec = automation_time_msec();
	event.button = button;
	event.state = pressed ? WL_POINTER_BUTTON_STATE_PRESSED :
		WL_POINTER_BUTTON_STATE_RELEASED;

	uint32_t mods = seat_modifiers(server);
	if (!shady_modules_pointer_button(server, &event, mods)) {
		if (button == BTN_LEFT) {
			struct shady_toplevel *titlebar = desktop_titlebar_at(server,
				server->cursor->x, server->cursor->y);
			if (titlebar) {
				if (pressed) {
					focus_toplevel(titlebar);
					shady_toplevel_begin_interactive(titlebar, SHADY_CURSOR_MOVE, 0);
				} else if (server->cursor_mode == SHADY_CURSOR_MOVE &&
						server->grabbed_toplevel == titlebar) {
					reset_cursor_mode(server);
				}
				wlr_seat_pointer_clear_focus(server->seat);
				wlr_seat_pointer_notify_frame(server->seat);
				return;
			}
		}
		wlr_seat_pointer_notify_button(server->seat,
			event.time_msec, event.button, event.state);
		if (!pressed) {
			reset_cursor_mode(server);
		} else {
			double sx, sy;
			struct wlr_surface *surface = NULL;
			struct shady_toplevel *toplevel = toplevel_at_cursor(server,
				server->cursor->x, server->cursor->y, &surface, &sx, &sy);
			focus_toplevel(toplevel);
		}
	}
	wlr_seat_pointer_notify_frame(server->seat);
}

void shady_input_automation_pointer_axis(struct shady_server *server,
		enum wl_pointer_axis orientation, double delta, int32_t discrete) {
	struct wlr_pointer_axis_event event = {0};
	event.time_msec = automation_time_msec();
	event.orientation = orientation;
	event.delta = delta;
	event.delta_discrete = discrete;
	event.source = WL_POINTER_AXIS_SOURCE_WHEEL;
	event.relative_direction = WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL;

	uint32_t mods = seat_modifiers(server);
	if (!shady_modules_pointer_axis(server, &event, mods)) {
		wlr_seat_pointer_notify_axis(server->seat,
			event.time_msec, event.orientation, event.delta,
			event.delta_discrete, event.source, event.relative_direction);
		wlr_seat_pointer_notify_frame(server->seat);
	}
}
