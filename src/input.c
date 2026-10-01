/* Adapted from wlroots 0.20.2 TinyWL (CC0). See LICENSES/tinywl-CC0.txt. */
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <xkbcommon/xkbcommon.h>

#include "shady.h"
#include "render/math3d.h"
#include "render/pick3d.h"
#include "render/render.h"
#include "modules/physics/physics.h"
#include "modules/fps/fps.h"
#include "modules/close_animation/close_animation.h"
#include "modules/window_motion/window_motion.h"
#include "modules/lua/lua.h"

#define CAMERA_ORBIT_SENS 0.005f
#define CAMERA_PAN_SENS 0.0025f
#define CAMERA_KEY_PAN 0.05f
#define CAMERA_KEY_ORBIT 0.08f
#define CAMERA_ZOOM_STEP 0.15f
#define CAMERA_PITCH_MAX 1.4f
#define CAMERA_DIST_MIN 0.4f
#define CAMERA_DIST_MAX 12.0f
#define WINDOW_Z_STEP 0.055f
#define WINDOW_Z_MIN -1.5f
#define WINDOW_Z_MAX 0.75f

void reset_cursor_mode(struct shady_server *server) {
	server->cursor_mode = SHADY_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

static void clamp_camera(struct shady_camera *cam) {
	if (cam->pitch > CAMERA_PITCH_MAX) {
		cam->pitch = CAMERA_PITCH_MAX;
	}
	if (cam->pitch < -CAMERA_PITCH_MAX) {
		cam->pitch = -CAMERA_PITCH_MAX;
	}
	if (cam->distance < CAMERA_DIST_MIN) {
		cam->distance = CAMERA_DIST_MIN;
	}
	if (cam->distance > CAMERA_DIST_MAX) {
		cam->distance = CAMERA_DIST_MAX;
	}
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

	/*
	 * Window displacement since the previous pointer event.
	 *
	 * A fast mouse movement therefore injects a larger impulse into
	 * the wobble spring.
	 */
	shady_window_motion_drag(server, toplevel, new_x, new_y);

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

static void process_cursor_camera_orbit(struct shady_server *server) {
	double dx = server->cursor->x - server->cam_grab_x;
	double dy = server->cursor->y - server->cam_grab_y;
	server->experimental.camera.yaw = server->cam_grab_yaw - (float)dx * CAMERA_ORBIT_SENS;
	server->experimental.camera.pitch = server->cam_grab_pitch - (float)dy * CAMERA_ORBIT_SENS;
	clamp_camera(&server->experimental.camera);
	shady_render_schedule_all_outputs(server);
}

static void process_cursor_camera_pan(struct shady_server *server) {
	double dx = server->cursor->x - server->cam_grab_x;
	double dy = server->cursor->y - server->cam_grab_y;
	struct shady_vec3 right, up;
	shady_camera_basis(&server->experimental.camera, &right, &up, NULL);
	/* Drag right → pan world left (camera moves with grab). */
	float scale = server->experimental.camera.distance * CAMERA_PAN_SENS;
	server->experimental.camera.target_x = server->cam_grab_target_x
		- right.x * (float)dx * scale + up.x * (float)dy * scale;
	server->experimental.camera.target_y = server->cam_grab_target_y
		- right.y * (float)dx * scale + up.y * (float)dy * scale;
	server->experimental.camera.target_z = server->cam_grab_target_z
		- right.z * (float)dx * scale + up.z * (float)dy * scale;
	shady_render_schedule_all_outputs(server);
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

static struct shady_toplevel *toplevel_at_cursor(struct shady_server *server,
		double lx, double ly, struct wlr_surface **surface, double *sx, double *sy) {
	if (!server->config.spatial_mode) {
		return desktop_toplevel_at(server, lx, ly, surface, sx, sy);
	}
	return shady_toplevel_at_3d(server, lx, ly, surface, sx, sy);
}

static void process_cursor_motion(struct shady_server *server, uint32_t time) {
	if (server->cursor_mode == SHADY_CURSOR_MOVE) {
		process_cursor_move(server);
		return;
	} else if (server->cursor_mode == SHADY_CURSOR_RESIZE) {
		process_cursor_resize(server);
		return;
	} else if (server->cursor_mode == SHADY_CURSOR_CAMERA_ORBIT) {
		process_cursor_camera_orbit(server);
		return;
	} else if (server->cursor_mode == SHADY_CURSOR_CAMERA_PAN) {
		process_cursor_camera_pan(server);
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
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->wlr_keyboard->modifiers);
}

static struct shady_toplevel *focused_toplevel(
	struct shady_server *server
) {
	struct wlr_surface *surface =
		server->seat->keyboard_state.focused_surface;

	if (!surface) {
		return NULL;
	}

	struct wlr_xdg_toplevel *xdg_toplevel =
		wlr_xdg_toplevel_try_from_wlr_surface(
			surface
		);

	if (!xdg_toplevel) {
		return NULL;
	}

	struct shady_toplevel *toplevel;

	wl_list_for_each(
		toplevel,
		&server->toplevels,
		link
	) {
		if (toplevel->xdg_toplevel == xdg_toplevel) {
			return toplevel;
		}
	}

	return NULL;
}

static bool bind_matches(const struct shady_keybind *bind,
		xkb_keysym_t sym, uint32_t modifiers) {
	const uint32_t mask = WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT |
		WLR_MODIFIER_CTRL | WLR_MODIFIER_LOGO;
	return bind->sym == sym && bind->modifiers == (modifiers & mask);
}

static bool handle_keybinding(struct shady_server *server,
		xkb_keysym_t sym, uint32_t modifiers) {
	struct shady_config *c = &server->config;
	if (shady_lua_handle_key(server, sym, modifiers)) return true;
	if (server->config.spatial_mode && bind_matches(&c->bind_debug_ray, sym, modifiers)) {
		server->experimental.debug_ray = !server->experimental.debug_ray;
		shady_render_schedule_all_outputs(server);
		return true;
	}
	if (server->config.spatial_mode && bind_matches(&c->bind_gravity_toggle, sym, modifiers)) {
		shady_physics_toggle_gravity(server);
		return true;
	}
	if (server->config.spatial_mode && bind_matches(&c->bind_fps_capture, sym, modifiers))
		return shady_fps_toggle_capture(server);
	if (server->config.spatial_mode && bind_matches(&c->bind_fps_toggle, sym, modifiers))
		return shady_fps_toggle(server);
	if (bind_matches(&c->bind_quit,sym,modifiers)) { wl_display_terminate(server->wl_display); return true; }
	if (bind_matches(&c->bind_cycle_windows,sym,modifiers)) {
		if (wl_list_length(&server->toplevels)>=2) { struct shady_toplevel *next=wl_container_of(server->toplevels.prev,next,link); focus_toplevel(next); }
		return true;
	}
	if (bind_matches(&c->bind_close_window,sym,modifiers)) { shady_close_animation_begin(server); return true; }
	if (!server->config.spatial_mode) return false;
	bool changed=false; struct shady_vec3 right,up,forward;
	if (bind_matches(&c->bind_camera_left,sym,modifiers) || bind_matches(&c->bind_camera_right,sym,modifiers) ||
		bind_matches(&c->bind_camera_up,sym,modifiers) || bind_matches(&c->bind_camera_down,sym,modifiers)) {
		shady_camera_basis(&server->experimental.camera,&right,&up,&forward);
		float sign = (bind_matches(&c->bind_camera_left,sym,modifiers)||bind_matches(&c->bind_camera_down,sym,modifiers)) ? -1.f : 1.f;
		struct shady_vec3 v = (bind_matches(&c->bind_camera_left,sym,modifiers)||bind_matches(&c->bind_camera_right,sym,modifiers)) ? right : up;
		server->experimental.camera.target_x += v.x*CAMERA_KEY_PAN*sign; server->experimental.camera.target_y += v.y*CAMERA_KEY_PAN*sign; server->experimental.camera.target_z += v.z*CAMERA_KEY_PAN*sign; changed=true;
	} else if (bind_matches(&c->bind_camera_yaw_left,sym,modifiers)) { server->experimental.camera.yaw += CAMERA_KEY_ORBIT; changed=true;
	} else if (bind_matches(&c->bind_camera_yaw_right,sym,modifiers)) { server->experimental.camera.yaw -= CAMERA_KEY_ORBIT; changed=true;
	} else if (bind_matches(&c->bind_camera_zoom_in,sym,modifiers)) { server->experimental.camera.distance -= CAMERA_ZOOM_STEP; changed=true;
	} else if (bind_matches(&c->bind_camera_zoom_out,sym,modifiers)) { server->experimental.camera.distance += CAMERA_ZOOM_STEP; changed=true;
	} else if (bind_matches(&c->bind_camera_reset,sym,modifiers)) { shady_camera_reset(&server->experimental.camera); changed=true;
	} else return false;
	if (changed) { clamp_camera(&server->experimental.camera); shady_render_schedule_all_outputs(server); }
	return true;
}

static void keyboard_handle_key(
		struct wl_listener *listener, void *data) {
	struct shady_keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
	struct shady_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int j = 0; j < nsyms && !handled; j++)
			handled = handle_keybinding(server, syms[j], modifiers);
	}

	if (shady_fps_handle_key(server, syms, nsyms, event->state))
		handled = true;
	if (!handled) {
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat,event->time_msec,event->keycode,event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void server_new_keyboard(struct shady_server *server,
		struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct shady_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
		XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct shady_server *server,
		struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);
}

void server_new_input(struct wl_listener *listener, void *data) {
	struct shady_server *server =
		wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
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

void server_cursor_motion(struct wl_listener *listener, void *data) {
	struct shady_server *server =
		wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	if (server->config.spatial_mode && shady_fps_handle_motion(server, event->delta_x, event->delta_y))
		return;
	wlr_cursor_move(server->cursor, &event->pointer->base,
			event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

void server_cursor_motion_absolute(
		struct wl_listener *listener, void *data) {
	struct shady_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	if (server->config.spatial_mode && server->experimental.camera.first_person && server->experimental.fps.input_capture) {
		shady_fps_handle_motion(server, 0.0, 0.0);
		return;
	}
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
	uint32_t mods = seat_modifiers(server);

	if (server->config.spatial_mode && shady_fps_handle_button(server, event->button, event->state))
		return;

	/* Right-drag orbits. Alt+middle-drag pans (plain middle goes to clients). */
	if (server->config.spatial_mode && (event->button == BTN_RIGHT
			|| (event->button == BTN_MIDDLE && (mods & WLR_MODIFIER_ALT)))) {
		if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
			server->cursor_mode = (event->button == BTN_RIGHT)
				? SHADY_CURSOR_CAMERA_ORBIT : SHADY_CURSOR_CAMERA_PAN;
			server->cam_grab_x = server->cursor->x;
			server->cam_grab_y = server->cursor->y;
			server->cam_grab_yaw = server->experimental.camera.yaw;
			server->cam_grab_pitch = server->experimental.camera.pitch;
			server->cam_grab_target_x = server->experimental.camera.target_x;
			server->cam_grab_target_y = server->experimental.camera.target_y;
			server->cam_grab_target_z = server->experimental.camera.target_z;
			wlr_seat_pointer_clear_focus(server->seat);
		} else if (server->cursor_mode == SHADY_CURSOR_CAMERA_ORBIT
				|| server->cursor_mode == SHADY_CURSOR_CAMERA_PAN) {
			reset_cursor_mode(server);
		}
		return;
	}

	if (event->button == BTN_MIDDLE
			&& event->state == WL_POINTER_BUTTON_STATE_RELEASED
			&& server->cursor_mode == SHADY_CURSOR_CAMERA_PAN) {
		reset_cursor_mode(server);
		return;
	}

	wlr_seat_pointer_notify_button(server->seat,
			event->time_msec, event->button, event->state);
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		if (server->cursor_mode != SHADY_CURSOR_CAMERA_ORBIT
				&& server->cursor_mode != SHADY_CURSOR_CAMERA_PAN) {
			reset_cursor_mode(server);
		}
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

	if (server->config.spatial_mode && shady_fps_handle_axis(server, event))
		return;

	uint32_t mods = seat_modifiers(server);

	/*
	 * Alt+Shift+scroll moves the focused window through world Z.
	 * Scroll up pulls it toward the camera; scroll down pushes it away.
	 * Alt+scroll remains camera zoom.
	 */
	if (server->config.spatial_mode && (mods & WLR_MODIFIER_ALT)
			&& (mods & WLR_MODIFIER_SHIFT)
			&& event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		struct shady_toplevel *toplevel = focused_toplevel(server);
		if (toplevel) {
			float direction = event->delta < 0.0 ? 1.0f : -1.0f;
			toplevel->experimental.z += direction * WINDOW_Z_STEP;
			if (toplevel->experimental.z < WINDOW_Z_MIN) toplevel->experimental.z = WINDOW_Z_MIN;
			if (toplevel->experimental.z > WINDOW_Z_MAX) toplevel->experimental.z = WINDOW_Z_MAX;
			shady_render_schedule_all_outputs(server);
		}
		return;
	}

	/* Alt+scroll zooms the camera; plain scroll goes to the client. */
	if (server->config.spatial_mode && (mods & WLR_MODIFIER_ALT)
			&& event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		server->experimental.camera.distance += (float)(event->delta * 0.01);
		clamp_camera(&server->experimental.camera);
		shady_render_schedule_all_outputs(server);
		return;
	}

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
