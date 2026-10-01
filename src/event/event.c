#include "event.h"

#include <stdlib.h>
#include <string.h>

#include "../shady.h"

const char *shady_event_name(enum shady_event_type type) {
	switch (type) {
	case SHADY_EVENT_WINDOW_CREATED: return "window.created";
	case SHADY_EVENT_WINDOW_MAPPED: return "window.mapped";
	case SHADY_EVENT_WINDOW_UNMAPPED: return "window.unmapped";
	case SHADY_EVENT_WINDOW_FOCUSED: return "window.focused";
	case SHADY_EVENT_WINDOW_DESTROYED: return "window.destroyed";
	case SHADY_EVENT_OUTPUT_ADDED: return "output.added";
	case SHADY_EVENT_OUTPUT_REMOVED: return "output.removed";
	case SHADY_EVENT_MODULE_STARTED: return "module.started";
	case SHADY_EVENT_MODULE_STOPPED: return "module.stopped";
	}
	return "unknown";
}

void shady_events_init(struct shady_server *server) {
	wl_list_init(&server->events.subscriptions);
	server->events.next_serial = 1;
}

void shady_events_finish(struct shady_server *server) {
	struct shady_event_subscription *sub, *tmp;
	wl_list_for_each_safe(sub, tmp, &server->events.subscriptions, link) {
		wl_list_remove(&sub->link);
		free(sub);
	}
}

bool shady_event_subscribe(struct shady_server *server, uint32_t event_type,
		shady_event_callback callback, void *user_data) {
	if (!callback || event_type > SHADY_EVENT_MODULE_STOPPED) return false;
	struct shady_event_subscription *sub = calloc(1, sizeof(*sub));
	if (!sub) return false;
	sub->event_type = event_type;
	sub->callback = callback;
	sub->user_data = user_data;
	wl_list_insert(server->events.subscriptions.prev, &sub->link);
	return true;
}

static void emit(struct shady_server *server, struct shady_event *event) {
	event->serial = server->events.next_serial++;
	struct shady_event_subscription *sub, *tmp;
	wl_list_for_each_safe(sub, tmp, &server->events.subscriptions, link) {
		if (sub->event_type == (uint32_t)event->type) {
			sub->callback((shady_host)server, event, sub->user_data);
		}
	}
}

void shady_event_emit_window(struct shady_server *server,
		enum shady_event_type type, void *window) {
	struct shady_event event = {.type = type};
	event.object.window = (shady_window)window;
	emit(server, &event);
}

void shady_event_emit_output(struct shady_server *server,
		enum shady_event_type type, void *output) {
	struct shady_event event = {.type = type};
	event.object.output = (shady_output)output;
	emit(server, &event);
}

void shady_event_emit_module(struct shady_server *server,
		enum shady_event_type type, const void *module) {
	struct shady_event event = {.type = type};
	event.object.module = (shady_module_handle)module;
	emit(server, &event);
}
