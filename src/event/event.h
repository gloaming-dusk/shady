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
	uint32_t event_type;
	shady_event_callback callback;
	void *user_data;
};

struct shady_event_bus {
	struct wl_list subscriptions;
	uint64_t next_serial;
};

void shady_events_init(struct shady_server *server);
void shady_events_finish(struct shady_server *server);
bool shady_event_subscribe(struct shady_server *server, uint32_t event_type,
	shady_event_callback callback, void *user_data);
void shady_event_emit_window(struct shady_server *server,
	enum shady_event_type type, void *window);
void shady_event_emit_output(struct shady_server *server,
	enum shady_event_type type, void *output);
void shady_event_emit_module(struct shady_server *server,
	enum shady_event_type type, const void *module);

#endif
