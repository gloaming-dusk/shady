#ifndef SHADY_EVENT_BUS_H
#define SHADY_EVENT_BUS_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <shady/event.h>

struct shady_server;

const char *shady_event_name(enum shady_event_type type);
struct shady_event_subscription {
	struct wl_list link;
	shady_subscription_id id;
	uint32_t event_type;
	shady_event_callback callback;
	void *user_data;
	void *owner;
	bool removed;
};

struct shady_event_bus {
	struct wl_list subscriptions;
	uint64_t next_serial;
	shady_subscription_id next_subscription_id;
	unsigned dispatch_depth;
};

void shady_events_init(struct shady_server *server);
void shady_events_finish(struct shady_server *server);
bool shady_event_subscribe(struct shady_server *server, uint32_t event_type,
	shady_event_callback callback, void *user_data);
shady_subscription_id shady_event_subscribe_owned(struct shady_server *server,
	uint32_t event_type, shady_event_callback callback, void *user_data, void *owner);
bool shady_event_unsubscribe(struct shady_server *server,
	shady_subscription_id subscription);
void shady_event_unsubscribe_owner(struct shady_server *server, void *owner);
bool shady_events_dispatching(const struct shady_server *server);
void shady_event_emit_window(struct shady_server *server,
	enum shady_event_type type, void *window);
void shady_event_emit_output(struct shady_server *server,
	enum shady_event_type type, void *output);
void shady_event_emit_module(struct shady_server *server,
	enum shady_event_type type, const void *module);
void shady_event_emit_workspace(struct shady_server *server,
	enum shady_event_type type, const char *workspace);

#endif
