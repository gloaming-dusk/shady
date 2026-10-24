/* Touchpad gestures: the layout takes swipes with its number of fingers,
 * everything else goes to the client under the pointer through
 * pointer-gestures-unstable-v1 (pinch to zoom, swipe to go back). */
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>

#include "shady.h"
#include "idle.h"
#include "modules/desktop/state.h"
#include "modules/layout/layout.h"

static void notify_activity(struct shady_server *server) {
	shady_idle_activity(server);
}

static void swipe_begin(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(listener, server, swipe_begin);
	struct wlr_pointer_swipe_begin_event *event = data;
	notify_activity(server);
	server->swipe_ours = !shady_desktop_state(server)->session_locked &&
		shady_layout_swipe_begin(server, event->fingers);
	if (!server->swipe_ours)
		wlr_pointer_gestures_v1_send_swipe_begin(server->pointer_gestures,
			server->seat, event->time_msec, event->fingers);
}

static void swipe_update(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(listener, server, swipe_update);
	struct wlr_pointer_swipe_update_event *event = data;
	notify_activity(server);
	if (server->swipe_ours) shady_layout_swipe_update(server, event->dx, event->dy);
	else wlr_pointer_gestures_v1_send_swipe_update(server->pointer_gestures,
		server->seat, event->time_msec, event->dx, event->dy);
}

static void swipe_end(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(listener, server, swipe_end);
	struct wlr_pointer_swipe_end_event *event = data;
	if (server->swipe_ours) shady_layout_swipe_end(server, event->cancelled);
	else wlr_pointer_gestures_v1_send_swipe_end(server->pointer_gestures,
		server->seat, event->time_msec, event->cancelled);
	server->swipe_ours = false;
}

static void pinch_begin(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(listener, server, pinch_begin);
	struct wlr_pointer_pinch_begin_event *event = data;
	notify_activity(server);
	wlr_pointer_gestures_v1_send_pinch_begin(server->pointer_gestures,
		server->seat, event->time_msec, event->fingers);
}

static void pinch_update(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(listener, server, pinch_update);
	struct wlr_pointer_pinch_update_event *event = data;
	wlr_pointer_gestures_v1_send_pinch_update(server->pointer_gestures, server->seat,
		event->time_msec, event->dx, event->dy, event->scale, event->rotation);
}

static void pinch_end(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(listener, server, pinch_end);
	struct wlr_pointer_pinch_end_event *event = data;
	wlr_pointer_gestures_v1_send_pinch_end(server->pointer_gestures,
		server->seat, event->time_msec, event->cancelled);
}

static void hold_begin(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(listener, server, hold_begin);
	struct wlr_pointer_hold_begin_event *event = data;
	wlr_pointer_gestures_v1_send_hold_begin(server->pointer_gestures,
		server->seat, event->time_msec, event->fingers);
}

static void hold_end(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(listener, server, hold_end);
	struct wlr_pointer_hold_end_event *event = data;
	wlr_pointer_gestures_v1_send_hold_end(server->pointer_gestures,
		server->seat, event->time_msec, event->cancelled);
}

bool shady_gestures_init(struct shady_server *server) {
	server->pointer_gestures = wlr_pointer_gestures_v1_create(server->wl_display);
	if (!server->pointer_gestures) return false;
	struct wlr_cursor *cursor = server->cursor;
	server->swipe_begin.notify = swipe_begin;
	wl_signal_add(&cursor->events.swipe_begin, &server->swipe_begin);
	server->swipe_update.notify = swipe_update;
	wl_signal_add(&cursor->events.swipe_update, &server->swipe_update);
	server->swipe_end.notify = swipe_end;
	wl_signal_add(&cursor->events.swipe_end, &server->swipe_end);
	server->pinch_begin.notify = pinch_begin;
	wl_signal_add(&cursor->events.pinch_begin, &server->pinch_begin);
	server->pinch_update.notify = pinch_update;
	wl_signal_add(&cursor->events.pinch_update, &server->pinch_update);
	server->pinch_end.notify = pinch_end;
	wl_signal_add(&cursor->events.pinch_end, &server->pinch_end);
	server->hold_begin.notify = hold_begin;
	wl_signal_add(&cursor->events.hold_begin, &server->hold_begin);
	server->hold_end.notify = hold_end;
	wl_signal_add(&cursor->events.hold_end, &server->hold_end);
	return true;
}

void shady_gestures_finish(struct shady_server *server) {
	if (!server->pointer_gestures) return;
	wl_list_remove(&server->swipe_begin.link);
	wl_list_remove(&server->swipe_update.link);
	wl_list_remove(&server->swipe_end.link);
	wl_list_remove(&server->pinch_begin.link);
	wl_list_remove(&server->pinch_update.link);
	wl_list_remove(&server->pinch_end.link);
	wl_list_remove(&server->hold_begin.link);
	wl_list_remove(&server->hold_end.link);
	server->pointer_gestures = NULL;
}
