#ifndef SHADY_PUBLIC_TYPES_H
#define SHADY_PUBLIC_TYPES_H
#include <stdint.h>
/* Opaque host objects. Plugins must only inspect them through the API table. */
struct shady_host_handle;
struct shady_window_handle;
struct shady_output_handle;
struct shady_seat_handle;
struct shady_module_handle;

typedef struct shady_host_handle *shady_host;
typedef struct shady_window_handle *shady_window;
typedef struct shady_output_handle *shady_output;
typedef struct shady_seat_handle *shady_seat;
typedef struct shady_module_handle *shady_module_handle;
typedef uint64_t shady_subscription_id;
typedef uint64_t shady_shader_program;
typedef uint64_t shady_render_hook_id;
struct shady_event;
typedef void (*shady_event_callback)(
	shady_host host,
	const struct shady_event *event,
	void *user_data);

#endif
