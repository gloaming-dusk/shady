#include "ime.h"

#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/util/log.h>

#include "../../shady.h"
#include "state.h"

struct shady_text_input {
	struct wl_list link;
	struct shady_ime_state *ime;
	struct wlr_text_input_v3 *text_input;
	struct wl_listener enable;
	struct wl_listener commit;
	struct wl_listener disable;
	struct wl_listener destroy;
};

struct shady_ime_state {
	struct shady_server *server;
	struct wlr_text_input_manager_v3 *text_input_manager;
	struct wlr_input_method_manager_v2 *input_method_manager;
	struct wlr_input_method_v2 *input_method;
	struct wl_list text_inputs;
	struct wl_listener new_text_input;
	struct wl_listener new_input_method;
	struct wl_listener input_method_commit;
	struct wl_listener input_method_destroy;
	struct wlr_input_method_keyboard_grab_v2 *active_keyboard_grab;
	struct wl_listener grab_keyboard;
	struct wl_listener keyboard_grab_destroy;
	struct wl_listener keyboard_focus_change;
};

static struct shady_text_input *focused_text_input(struct shady_ime_state *ime) {
	struct wlr_surface *surface = ime->server->seat->keyboard_state.focused_surface;
	if (!surface) return NULL;
	struct wl_client *client = wl_resource_get_client(surface->resource);
	struct shady_text_input *entry;
	wl_list_for_each(entry, &ime->text_inputs, link) {
		if (entry->text_input->seat != ime->server->seat) continue;
		if (wl_resource_get_client(entry->text_input->resource) == client)
			return entry;
	}
	return NULL;
}

static void send_text_input_state(struct shady_ime_state *ime,
		struct wlr_text_input_v3 *text_input) {
	if (!ime->input_method || !text_input || !text_input->current_enabled) return;
	if (text_input->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) {
		wlr_input_method_v2_send_surrounding_text(ime->input_method,
			text_input->current.surrounding.text,
			text_input->current.surrounding.cursor,
			text_input->current.surrounding.anchor);
	}
	if (text_input->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) {
		wlr_input_method_v2_send_content_type(ime->input_method,
			text_input->current.content_type.hint,
			text_input->current.content_type.purpose);
	}
	wlr_input_method_v2_send_text_change_cause(ime->input_method,
		text_input->current.text_change_cause);
	wlr_input_method_v2_send_done(ime->input_method);
}

static void update_activation(struct shady_ime_state *ime) {
	if (!ime->input_method) return;
	struct shady_text_input *entry = focused_text_input(ime);
	if (entry && entry->text_input->current_enabled) {
		wlr_input_method_v2_send_activate(ime->input_method);
		send_text_input_state(ime, entry->text_input);
	} else {
		wlr_input_method_v2_send_deactivate(ime->input_method);
		wlr_input_method_v2_send_done(ime->input_method);
	}
}

static void text_input_enable(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_text_input *entry = wl_container_of(listener, entry, enable);
	update_activation(entry->ime);
}

static void text_input_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_text_input *entry = wl_container_of(listener, entry, commit);
	if (focused_text_input(entry->ime) == entry)
		send_text_input_state(entry->ime, entry->text_input);
}

static void text_input_disable(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_text_input *entry = wl_container_of(listener, entry, disable);
	if (entry->ime->input_method) {
		wlr_input_method_v2_send_deactivate(entry->ime->input_method);
		wlr_input_method_v2_send_done(entry->ime->input_method);
	}
}

static void text_input_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_text_input *entry = wl_container_of(listener, entry, destroy);
	wl_list_remove(&entry->enable.link);
	wl_list_remove(&entry->commit.link);
	wl_list_remove(&entry->disable.link);
	wl_list_remove(&entry->destroy.link);
	wl_list_remove(&entry->link);
	free(entry);
}

static void new_text_input(struct wl_listener *listener, void *data) {
	struct shady_ime_state *ime = wl_container_of(listener, ime, new_text_input);
	struct wlr_text_input_v3 *text_input = data;
	struct shady_text_input *entry = calloc(1, sizeof(*entry));
	if (!entry) return;
	entry->ime = ime;
	entry->text_input = text_input;
	entry->enable.notify = text_input_enable;
	entry->commit.notify = text_input_commit;
	entry->disable.notify = text_input_disable;
	entry->destroy.notify = text_input_destroy;
	wl_signal_add(&text_input->events.enable, &entry->enable);
	wl_signal_add(&text_input->events.commit, &entry->commit);
	wl_signal_add(&text_input->events.disable, &entry->disable);
	wl_signal_add(&text_input->events.destroy, &entry->destroy);
	wl_list_insert(&ime->text_inputs, &entry->link);

	struct wlr_surface *surface = ime->server->seat->keyboard_state.focused_surface;
	if (surface && wl_resource_get_client(surface->resource) ==
			wl_resource_get_client(text_input->resource)) {
		wlr_text_input_v3_send_enter(text_input, surface);
	}
}

static void input_method_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_ime_state *ime = wl_container_of(listener, ime, input_method_commit);
	struct shady_text_input *entry = focused_text_input(ime);
	if (!entry || !entry->text_input->current_enabled) return;

	struct wlr_input_method_v2_state *state = &ime->input_method->current;
	if (state->preedit.text) {
		wlr_text_input_v3_send_preedit_string(entry->text_input,
			state->preedit.text,
			state->preedit.cursor_begin,
			state->preedit.cursor_end);
	}
	if (state->commit_text) {
		wlr_text_input_v3_send_commit_string(entry->text_input, state->commit_text);
	}
	if (state->delete.before_length || state->delete.after_length) {
		wlr_text_input_v3_send_delete_surrounding_text(entry->text_input,
			state->delete.before_length, state->delete.after_length);
	}
	wlr_text_input_v3_send_done(entry->text_input);
}

static void keyboard_grab_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_ime_state *ime = wl_container_of(listener, ime, keyboard_grab_destroy);
	wl_list_remove(&ime->keyboard_grab_destroy.link);
	ime->active_keyboard_grab = NULL;
}

static void keyboard_grab(struct wl_listener *listener, void *data) {
	struct shady_ime_state *ime = wl_container_of(listener, ime, grab_keyboard);
	struct wlr_input_method_keyboard_grab_v2 *grab = data;
	if (ime->active_keyboard_grab) {
		wlr_input_method_keyboard_grab_v2_destroy(grab);
		return;
	}
	ime->active_keyboard_grab = grab;
	ime->keyboard_grab_destroy.notify = keyboard_grab_destroy;
	wl_signal_add(&grab->events.destroy, &ime->keyboard_grab_destroy);
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(ime->server->seat);
	if (keyboard) wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard);
}

static void input_method_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_ime_state *ime = wl_container_of(listener, ime, input_method_destroy);
	wl_list_remove(&ime->input_method_commit.link);
	wl_list_remove(&ime->input_method_destroy.link);
	if (ime->grab_keyboard.link.prev) wl_list_remove(&ime->grab_keyboard.link);
	if (ime->keyboard_grab_destroy.link.prev)
		wl_list_remove(&ime->keyboard_grab_destroy.link);
	ime->active_keyboard_grab = NULL;
	ime->input_method = NULL;
}

static void new_input_method(struct wl_listener *listener, void *data) {
	struct shady_ime_state *ime = wl_container_of(listener, ime, new_input_method);
	struct wlr_input_method_v2 *input_method = data;
	if (input_method->seat != ime->server->seat || ime->input_method) {
		wlr_log(WLR_ERROR, "IME: rejecting input method for unavailable seat");
		wlr_input_method_v2_send_unavailable(input_method);
		return;
	}
	ime->input_method = input_method;
	ime->input_method_commit.notify = input_method_commit;
	ime->input_method_destroy.notify = input_method_destroy;
	wl_signal_add(&input_method->events.commit, &ime->input_method_commit);
	wl_signal_add(&input_method->events.destroy, &ime->input_method_destroy);
	ime->grab_keyboard.notify = keyboard_grab;
	wl_signal_add(&input_method->events.grab_keyboard, &ime->grab_keyboard);
	update_activation(ime);
}

static void keyboard_focus_change(struct wl_listener *listener, void *data) {
	struct shady_ime_state *ime = wl_container_of(listener, ime, keyboard_focus_change);
	struct wlr_seat_keyboard_focus_change_event *event = data;
	struct shady_text_input *entry;
	wl_list_for_each(entry, &ime->text_inputs, link) {
		if (entry->text_input->focused_surface &&
				entry->text_input->focused_surface == event->old_surface)
			wlr_text_input_v3_send_leave(entry->text_input);
		if (event->new_surface && !entry->text_input->focused_surface &&
				wl_resource_get_client(entry->text_input->resource) ==
				wl_resource_get_client(event->new_surface->resource)) {
			wlr_text_input_v3_send_enter(entry->text_input, event->new_surface);
		}
	}
	update_activation(ime);
}

bool shady_ime_init(struct shady_server *server) {
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	struct shady_ime_state *ime = calloc(1, sizeof(*ime));
	if (!ime) return false;
	ime->server = server;
	wl_list_init(&ime->text_inputs);
	ime->text_input_manager = wlr_text_input_manager_v3_create(server->wl_display);
	ime->input_method_manager = wlr_input_method_manager_v2_create(server->wl_display);
	if (!ime->text_input_manager || !ime->input_method_manager) {
		free(ime);
		return false;
	}
	ime->new_text_input.notify = new_text_input;
	wl_signal_add(&ime->text_input_manager->events.new_text_input,
		&ime->new_text_input);
	ime->new_input_method.notify = new_input_method;
	wl_signal_add(&ime->input_method_manager->events.new_input_method,
		&ime->new_input_method);
	ime->keyboard_focus_change.notify = keyboard_focus_change;
	wl_signal_add(&server->seat->keyboard_state.events.focus_change,
		&ime->keyboard_focus_change);
	desktop->ime = ime;
	return true;
}

bool shady_ime_handle_key(struct shady_server *server,
		uint32_t time_msec, uint32_t keycode, uint32_t state) {
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	struct shady_ime_state *ime = desktop ? desktop->ime : NULL;
	if (!ime || !ime->active_keyboard_grab) return false;
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard)
		wlr_input_method_keyboard_grab_v2_set_keyboard(ime->active_keyboard_grab, keyboard);
	wlr_input_method_keyboard_grab_v2_send_key(ime->active_keyboard_grab,
		time_msec, keycode, state);
	return true;
}

bool shady_ime_handle_modifiers(struct shady_server *server,
		struct wlr_keyboard_modifiers *modifiers) {
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	struct shady_ime_state *ime = desktop ? desktop->ime : NULL;
	if (!ime || !ime->active_keyboard_grab) return false;
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard)
		wlr_input_method_keyboard_grab_v2_set_keyboard(ime->active_keyboard_grab, keyboard);
	wlr_input_method_keyboard_grab_v2_send_modifiers(ime->active_keyboard_grab, modifiers);
	return true;
}

void shady_ime_finish(struct shady_server *server) {
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	struct shady_ime_state *ime = desktop ? desktop->ime : NULL;
	if (!ime) return;
	if (ime->new_text_input.link.prev) wl_list_remove(&ime->new_text_input.link);
	if (ime->new_input_method.link.prev) wl_list_remove(&ime->new_input_method.link);
	if (ime->keyboard_focus_change.link.prev)
		wl_list_remove(&ime->keyboard_focus_change.link);
	if (ime->input_method) {
		if (ime->input_method_commit.link.prev)
			wl_list_remove(&ime->input_method_commit.link);
		if (ime->input_method_destroy.link.prev)
			wl_list_remove(&ime->input_method_destroy.link);
		if (ime->grab_keyboard.link.prev)
			wl_list_remove(&ime->grab_keyboard.link);
		if (ime->keyboard_grab_destroy.link.prev)
			wl_list_remove(&ime->keyboard_grab_destroy.link);
	}
	struct shady_text_input *entry, *tmp;
	wl_list_for_each_safe(entry, tmp, &ime->text_inputs, link) {
		wl_list_remove(&entry->enable.link);
		wl_list_remove(&entry->commit.link);
		wl_list_remove(&entry->disable.link);
		wl_list_remove(&entry->destroy.link);
		wl_list_remove(&entry->link);
		free(entry);
	}
	free(ime);
	desktop->ime = NULL;
}
