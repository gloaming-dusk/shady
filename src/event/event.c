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
	case SHADY_EVENT_WINDOW_RESIZED: return "window.resized";
	case SHADY_EVENT_WINDOW_STATE_CHANGED: return "window.state_changed";
	case SHADY_EVENT_WINDOW_TITLE_CHANGED: return "window.title_changed";
	case SHADY_EVENT_WINDOW_DESTROYED: return "window.destroyed";
	case SHADY_EVENT_OUTPUT_ADDED: return "output.added";
	case SHADY_EVENT_OUTPUT_REMOVED: return "output.removed";
	case SHADY_EVENT_WORKSPACE_CHANGED: return "workspace.changed";
	case SHADY_EVENT_MODULE_STARTED: return "module.started";
	case SHADY_EVENT_MODULE_STOPPED: return "module.stopped";
	case SHADY_EVENT_COUNT: break;
	}
	return "unknown";
}

void shady_events_init(struct shady_server *server) {
	wl_list_init(&server->events.subscriptions);
	server->events.next_serial = 1;
	server->events.next_subscription_id = 1;
	server->events.dispatch_depth = 0;
}

void shady_events_finish(struct shady_server *server) {
	struct shady_event_subscription *sub, *tmp;
	wl_list_for_each_safe(sub, tmp, &server->events.subscriptions, link) {
		wl_list_remove(&sub->link);
		free(sub);
	}
}

shady_subscription_id shady_event_subscribe_owned(struct shady_server *server,
		uint32_t event_type, shady_event_callback callback, void *user_data,
		void *owner) {
	if (!callback || event_type >= SHADY_EVENT_COUNT) return 0;
	struct shady_event_subscription *sub = calloc(1, sizeof(*sub));
	if (!sub) return 0;
	sub->id = server->events.next_subscription_id++;
	if (sub->id == 0) sub->id = server->events.next_subscription_id++;
	sub->event_type = event_type;
	sub->callback = callback;
	sub->user_data = user_data;
	sub->owner = owner;
	wl_list_insert(server->events.subscriptions.prev, &sub->link);
	return sub->id;
}

bool shady_event_subscribe(struct shady_server *server, uint32_t event_type,
		shady_event_callback callback, void *user_data) {
	return shady_event_subscribe_owned(server, event_type, callback, user_data, NULL) != 0;
}

static void sweep_removed(struct shady_server *server) {
	if (server->events.dispatch_depth != 0) return;
	struct shady_event_subscription *sub, *tmp;
	wl_list_for_each_safe(sub, tmp, &server->events.subscriptions, link) {
		if (!sub->removed) continue;
		wl_list_remove(&sub->link);
		free(sub);
	}
}

bool shady_event_unsubscribe(struct shady_server *server,
		shady_subscription_id subscription) {
	if (!subscription) return false;
	struct shady_event_subscription *sub;
	wl_list_for_each(sub, &server->events.subscriptions, link) {
		if (sub->id != subscription || sub->removed) continue;
		sub->removed = true;
		sweep_removed(server);
		return true;
	}
	return false;
}

void shady_event_unsubscribe_owner(struct shady_server *server, void *owner) {
	if (!owner) return;
	struct shady_event_subscription *sub;
	wl_list_for_each(sub, &server->events.subscriptions, link) {
		if (sub->owner == owner) sub->removed = true;
	}
	sweep_removed(server);
}

bool shady_events_dispatching(const struct shady_server *server) {
	return server->events.dispatch_depth != 0;
}

static void emit(struct shady_server *server, struct shady_event *event) {
	event->serial = server->events.next_serial++;
	server->events.dispatch_depth++;
	struct shady_event_subscription *sub;
	wl_list_for_each(sub, &server->events.subscriptions, link) {
		if (!sub->removed && sub->event_type == (uint32_t)event->type) {
			sub->callback((shady_host)server, event, sub->user_data);
		}
	}
	server->events.dispatch_depth--;
	sweep_removed(server);
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

void shady_event_emit_workspace(struct shady_server *server,
		enum shady_event_type type, const char *workspace) {
	struct shady_event event = {.type = type};
	event.object.workspace = workspace;
	emit(server, &event);
}
