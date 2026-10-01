#ifndef SHADY_PUBLIC_EVENT_H
#define SHADY_PUBLIC_EVENT_H

#include <stdint.h>
#include <shady/plugin.h>

enum shady_event_type {
	SHADY_EVENT_WINDOW_CREATED = 0,
	SHADY_EVENT_WINDOW_MAPPED,
	SHADY_EVENT_WINDOW_UNMAPPED,
	SHADY_EVENT_WINDOW_FOCUSED,
	SHADY_EVENT_WINDOW_DESTROYED,
	SHADY_EVENT_OUTPUT_ADDED,
	SHADY_EVENT_OUTPUT_REMOVED,
	SHADY_EVENT_MODULE_STARTED,
	SHADY_EVENT_MODULE_STOPPED,
};

struct shady_event {
	enum shady_event_type type;
	uint64_t serial;
	union {
		shady_window window;
		shady_output output;
		shady_module_handle module;
	} object;
};

#endif
