/* Adapted from wlroots 0.20.2 TinyWL (CC0). See LICENSES/tinywl-CC0.txt. */
#include <linux/input-event-codes.h>
#include <libinput.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/backend/libinput.h>
#include <wlr/backend/session.h>
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
#include <wlr/xcursor.h>
#include <xkbcommon/xkbcommon.h>

#include "shady.h"
#include "ipc/ipc.h"
#include "module/module.h"
#include "modules/desktop/ime.h"
#include "modules/desktop/state.h"
#include <wlr/types/wlr_layer_shell_v1.h>
#include "render/render.h"
#if SHADY_HAS_SPATIAL
#include "render/pick3d.h"
#endif

void reset_cursor_mode(struct shady_server *server) {
	if (server->cursor_mode == SHADY_CURSOR_RESIZE && server->grabbed_toplevel) {
		struct shady_toplevel *toplevel = server->grabbed_toplevel;
		toplevel->resize_serial = wlr_xdg_toplevel_set_resizing(toplevel->xdg_toplevel, false);
	}
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

/* The smallest window a resize makes, unless the client asks for more. */
#define RESIZE_MIN_WIDTH 120
#define RESIZE_MIN_HEIGHT 60

static void process_cursor_resize(struct shady_server *server) {
	struct shady_toplevel *toplevel = server->grabbed_toplevel;
	if (!toplevel) return;
	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;
	int left = server->grab_geobox.x;
	int right = server->grab_geobox.x + server->grab_geobox.width;
	int top = server->grab_geobox.y;
	int bottom = server->grab_geobox.y + server->grab_geobox.height;
	int width = server->grab_geobox.width, height = server->grab_geobox.height;
	if (server->resize_edges & WLR_EDGE_LEFT) width = right - (int)border_x;
	else if (server->resize_edges & WLR_EDGE_RIGHT) width = (int)border_x - left;
	if (server->resize_edges & WLR_EDGE_TOP) height = bottom - (int)border_y;
	else if (server->resize_edges & WLR_EDGE_BOTTOM) height = (int)border_y - top;

	struct wlr_xdg_toplevel_state *current = &toplevel->xdg_toplevel->current;
	int min_w = current->min_width > RESIZE_MIN_WIDTH ? current->min_width : RESIZE_MIN_WIDTH;
	int min_h = current->min_height > RESIZE_MIN_HEIGHT ? current->min_height : RESIZE_MIN_HEIGHT;
	if (width < min_w) width = min_w;
	if (height < min_h) height = min_h;
	if (current->max_width > 0 && width > current->max_width) width = current->max_width;
	if (current->max_height > 0 && height > current->max_height) height = current->max_height;

	/* Moving the window here, before the client has drawn the new size,
	 * would make the opposite edge jitter: the commit handler keeps that
	 * edge where it was grabbed once the new size arrives. */
	toplevel->resize_serial = wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);
}

/* The window edge under the pointer when nothing else is (see
 * shady_resize_edge_at_3d); in the flat desktop the frames are where the
 * scene graph has them. */
static struct shady_toplevel *resize_edge_at(struct shady_server *server,
		double lx, double ly, uint32_t *edges) {
	*edges = 0;
	if (shady_desktop_state(server)->session_locked) return NULL;
#if SHADY_HAS_SPATIAL
	if (server->config.spatial_mode)
		return shady_resize_edge_at_3d(server, lx, ly, edges);
#endif
	const double border = 10.0, corner = 24.0;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (!toplevel->mapped || !toplevel->scene_tree->node.enabled ||
				toplevel->maximized || toplevel->fullscreen) continue;
		struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
		double title = shady_toplevel_has_titlebar(toplevel) ? toplevel->titlebar_height : 0;
		double x0 = toplevel->scene_tree->node.x, y0 = toplevel->scene_tree->node.y - title;
		double x1 = x0 + geo->width, y1 = toplevel->scene_tree->node.y + geo->height;
		if (lx < x0 - border || lx > x1 + border || ly < y0 - border || ly > y1 + border)
			continue;
		uint32_t e = 0;
		if (lx < x0) e |= WLR_EDGE_LEFT; else if (lx > x1) e |= WLR_EDGE_RIGHT;
		if (ly < y0) e |= WLR_EDGE_TOP; else if (ly > y1) e |= WLR_EDGE_BOTTOM;
		if (!e) return NULL; /* on the window itself */
		if (e == WLR_EDGE_LEFT || e == WLR_EDGE_RIGHT) {
			if (ly < y0 + corner) e |= WLR_EDGE_TOP;
			else if (ly > y1 - corner) e |= WLR_EDGE_BOTTOM;
		} else if (e == WLR_EDGE_TOP || e == WLR_EDGE_BOTTOM) {
			if (lx < x0 + corner) e |= WLR_EDGE_LEFT;
			else if (lx > x1 - corner) e |= WLR_EDGE_RIGHT;
		}
		*edges = e;
		return toplevel;
	}
	return NULL;
}

static struct shady_toplevel *desktop_titlebar_at(struct shady_server *server,
		double lx, double ly) {
	/* Spatial titlebars are picked by the spatial module where they are
	 * rendered, rather than at their unprojected scene-graph coordinates. */
	if (server->config.spatial_mode && !shady_desktop_state(server)->session_locked)
		return NULL;
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
	/* A layer surface placed in 3D is not where the scene graph has it. */
	if (shady_render_layer_is_transformed(server, scene_surface->surface)) return NULL;
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
	struct wlr_surface *scene_hit = NULL;
	struct wlr_scene_node *node = wlr_scene_node_at(&server->overlay_tree->node,
		lx, ly, sx, sy);
	if (node && node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_surface *scene_surface =
			wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
		if (scene_surface) scene_hit = scene_surface->surface;
	}
	/* Popups and other non-layer overlay content are drawn above every
	 * layer surface, so they win outright. */
	if (scene_hit && !wlr_layer_surface_v1_try_from_wlr_surface(
			wlr_surface_get_root_surface(scene_hit))) {
		*surface = scene_hit;
		return true;
	}
	/* The spatial renderer may draw layer surfaces in 3D: hit-test them all
	 * in stacking order where they are drawn. */
	int picked = shady_render_layer_pick(server, lx, ly, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, surface, sx, sy);
	if (picked >= 0) return picked == 1;
	if (!scene_hit) return false;
	*surface = scene_hit;
	return true;
}

bool shady_input_pointer_on_overlay(struct shady_server *server) {
	struct wlr_surface *surface = NULL;
	double sx = 0, sy = 0;
	return overlay_surface_at(server, server->cursor->x, server->cursor->y,
		&surface, &sx, &sy);
}

/* The spatial renderer draws xdg popups flat, above everything but the
 * session lock, at their scene-graph place: hit-test them there, newest
 * (topmost) first. The spatial pick only knows the windows beneath. */
static bool popup_surface_at(struct shady_server *server,
		double lx, double ly, struct wlr_surface **surface, double *sx, double *sy) {
	if (!server->config.spatial_mode || shady_desktop_state(server)->session_locked)
		return false;
	struct shady_popup *popup;
	wl_list_for_each(popup, &server->popups, link) {
		if (!popup->xdg_popup->base->surface->mapped) continue;
		struct wlr_scene_node *node = wlr_scene_node_at(&popup->scene_tree->node,
			lx, ly, sx, sy);
		if (!node || node->type != WLR_SCENE_NODE_BUFFER) continue;
		struct wlr_scene_surface *scene_surface =
			wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
		if (!scene_surface) continue;
		*surface = scene_surface->surface;
		return true;
	}
	return false;
}

static struct shady_toplevel *toplevel_at_cursor(struct shady_server *server,
		double lx, double ly, struct wlr_surface **surface, double *sx, double *sy) {
	*surface = NULL;
	*sx = *sy = 0.0;
	/* Critical 2D shell/session-lock UI always wins over spatial picking. */
	if (overlay_surface_at(server, lx, ly, surface, sx, sy)) return NULL;
	if (popup_surface_at(server, lx, ly, surface, sx, sy)) return NULL;

	struct shady_toplevel *toplevel = NULL;
	bool locked = shady_desktop_state(server)->session_locked;
	/* A spatial pick module answers for the whole world, also with "nothing
	 * here" (claimed, but no window and no surface). */
	bool claimed = !locked && shady_modules_pick_surface(server, lx, ly,
		surface, sx, sy, &toplevel);
	if (claimed && (toplevel || *surface)) return toplevel;
	/* Background and bottom layers lie behind the windows: they take the
	 * pointer where no window is, transformed ones where they are drawn. */
	if (!locked && shady_render_layer_pick(server, lx, ly, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
			ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, surface, sx, sy) == 1) {
		return NULL;
	}
	if (claimed) {
		*surface = NULL;
		return NULL;
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
	if (!toplevel && !surface) {
		uint32_t edges = 0;
		resize_edge_at(server, server->cursor->x, server->cursor->y, &edges);
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
			edges ? wlr_xcursor_get_resize_name(edges) : "default");
	} else if (!toplevel) {
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
	if (bind_matches(&c->bind_show_shortcuts, sym, modifiers)) {
		shady_ipc_shortcuts_toggle(server);
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

/* Ctrl+Alt+Fn: the keymap turns it into XF86Switch_VT_n. Handled before
 * anything else so a stuck client, module or lock never traps the seat. */
static bool handle_vt_switch(struct shady_server *server,
		struct wlr_keyboard *keyboard, uint32_t keycode) {
	if (!server->session) return false;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->xkb_state, keycode, &syms);
	for (int i = 0; i < nsyms; i++) {
		if (syms[i] >= XKB_KEY_XF86Switch_VT_1 &&
				syms[i] <= XKB_KEY_XF86Switch_VT_12) {
			wlr_session_change_vt(server->session,
				syms[i] - XKB_KEY_XF86Switch_VT_1 + 1);
			return true;
		}
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
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED &&
			handle_vt_switch(server, keyboard->wlr_keyboard, keycode))
		return;
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

static void configure_libinput_pointer(struct shady_server *server,
		struct wlr_input_device *device) {
	if (!wlr_input_device_is_libinput(device)) return;
	struct libinput_device *libinput_device =
		wlr_libinput_get_device_handle(device);
	if (!libinput_device) return;
	const char *name = device->name ? device->name : "pointer";

	/* Touchpads tap to click and scroll the way the config says, as on
	 * other desktops; libinput leaves tapping off by default. */
	if (libinput_device_config_tap_get_finger_count(libinput_device) > 0) {
		libinput_device_config_tap_set_enabled(libinput_device,
			server->config.touchpad_tap ? LIBINPUT_CONFIG_TAP_ENABLED
				: LIBINPUT_CONFIG_TAP_DISABLED);
		if (libinput_device_config_scroll_has_natural_scroll(libinput_device))
			libinput_device_config_scroll_set_natural_scroll_enabled(libinput_device,
				server->config.touchpad_natural_scroll);
		wlr_log(WLR_INFO, "input: %s: tap %s, natural scroll %s", name,
			server->config.touchpad_tap ? "on" : "off",
			server->config.touchpad_natural_scroll ? "on" : "off");
	}

	if (!libinput_device_config_dwt_is_available(libinput_device)) return;
	enum libinput_config_status status =
		libinput_device_config_dwt_set_enabled(libinput_device,
			LIBINPUT_CONFIG_DWT_DISABLED);
	if (status == LIBINPUT_CONFIG_STATUS_SUCCESS) {
		wlr_log(WLR_INFO,
			"input: disabled touchpad disable-while-typing for %s", name);
	} else {
		wlr_log(WLR_ERROR,
			"input: failed to disable touchpad disable-while-typing for %s", name);
	}
}

static void server_new_pointer(struct shady_server *server,
		struct wlr_input_device *device) {
	configure_libinput_pointer(server, device);
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

/* Super+left drag moves the window under the pointer, Super+right drag
 * resizes it from the nearest corner, and a left press just outside a
 * window's frame resizes it from that edge. */
static bool begin_pointer_grab(struct shady_server *server, uint32_t button,
		uint32_t mods) {
	if (shady_desktop_state(server)->session_locked ||
			shady_input_pointer_on_overlay(server)) return false;
	double lx = server->cursor->x, ly = server->cursor->y;
	const uint32_t mod_mask = WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT |
		WLR_MODIFIER_CTRL | WLR_MODIFIER_LOGO;
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct shady_toplevel *toplevel = toplevel_at_cursor(server, lx, ly, &surface, &sx, &sy);
	if ((mods & mod_mask) == WLR_MODIFIER_LOGO &&
			(button == BTN_LEFT || button == BTN_RIGHT)) {
		if (!toplevel || toplevel->fullscreen) return false;
		focus_toplevel(toplevel);
		if (button == BTN_LEFT) {
			shady_toplevel_begin_interactive(toplevel, SHADY_CURSOR_MOVE, 0);
		} else {
			if (toplevel->maximized) shady_toplevel_set_maximized(toplevel, false);
			struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
			uint32_t edges =
				(lx < toplevel->scene_tree->node.x + geo->width / 2.0
					? WLR_EDGE_LEFT : WLR_EDGE_RIGHT) |
				(ly < toplevel->scene_tree->node.y + geo->height / 2.0
					? WLR_EDGE_TOP : WLR_EDGE_BOTTOM);
			shady_toplevel_begin_interactive(toplevel, SHADY_CURSOR_RESIZE, edges);
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
				wlr_xcursor_get_resize_name(edges));
		}
		wlr_seat_pointer_clear_focus(server->seat);
		return server->grabbed_toplevel == toplevel;
	}
	if (button != BTN_LEFT || toplevel || surface) return false;
	uint32_t edges = 0;
	struct shady_toplevel *edge = resize_edge_at(server, lx, ly, &edges);
	if (!edge || !edges) return false;
	focus_toplevel(edge);
	shady_toplevel_begin_interactive(edge, SHADY_CURSOR_RESIZE, edges);
	wlr_seat_pointer_clear_focus(server->seat);
	return server->grabbed_toplevel == edge;
}

/* A button from a pointer or from automation. */
static void handle_pointer_button(struct shady_server *server,
		struct wlr_pointer_button_event *event) {
	uint32_t mods = seat_modifiers(server);

	/* A move or resize the compositor runs ends with the button; the
	 * client never saw it pressed. */
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED &&
			(server->cursor_mode == SHADY_CURSOR_MOVE ||
			 server->cursor_mode == SHADY_CURSOR_RESIZE) && server->grabbed_toplevel) {
		reset_cursor_mode(server);
		process_cursor_motion(server, event->time_msec);
		return;
	}

	if (shady_modules_pointer_button(server, event, mods))
		return;

	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED &&
			begin_pointer_grab(server, event->button, mods))
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

	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED)
		process_cursor_motion(server, event->time_msec);
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

void server_cursor_button(struct wl_listener *listener, void *data) {
	struct shady_server *server =
		wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	if (shady_desktop_state(server)->idle_notifier) {
		wlr_idle_notifier_v1_notify_activity(shady_desktop_state(server)->idle_notifier, server->seat);
	}
	handle_pointer_button(server, event);
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

	handle_pointer_button(server, &event);
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
