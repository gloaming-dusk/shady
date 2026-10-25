#ifndef SHADY_IDLE_H
#define SHADY_IDLE_H

#include <stdbool.h>
#include <stdint.h>

struct shady_server;

/* Idle timeouts for the session (lock the screen, turn it off), held back
 * while a client inhibits idling (a video playing) through
 * idle-inhibit-unstable-v1. Every input event counts as activity. */
typedef void (*shady_idle_fn)(void *data);

bool shady_idle_init(struct shady_server *server);
void shady_idle_finish(struct shady_server *server);
void shady_idle_activity(struct shady_server *server);
bool shady_idle_inhibited(struct shady_server *server);
/* Returns an id > 0, or 0 when there is no room. */
int shady_idle_add(struct shady_server *server, uint32_t timeout_ms,
	shady_idle_fn on_idle, shady_idle_fn on_resume, void *data);
/* Calls release(data) for each removed timeout's data. */
void shady_idle_clear(struct shady_server *server, shady_idle_fn release);
/* Keep the session awake as if a client inhibited idling. */
void shady_idle_set_inhibit(struct shady_server *server, bool inhibit);
/* Turn every output's panel off or back on (DPMS); input turns it on. */
void shady_idle_output_power(struct shady_server *server, bool on);

#endif
