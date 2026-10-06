#ifndef SHADY_IPC_VALUES_H
#define SHADY_IPC_VALUES_H

/*
 * Published values: small named strings that plugins (publish_value) and
 * Lua (shady.publish) share with programs outside the compositor. IPC
 * clients read them with `values`/`value` and follow them through the
 * `value.changed` event; shady-shell exposes them to its config as
 * shell.compositor_value(key). Rices use them for state such as the current
 * hour or palette, so bars and panels can follow the compositor's look.
 *
 * Keys are 1-128 characters of [A-Za-z0-9._-], values at most 4096 bytes.
 * A value belongs to whoever set it last; when a plugin unloads, the values
 * it owns are removed.
 */

#include <stdbool.h>

struct shady_server;

#define SHADY_VALUE_KEY_MAX 128
#define SHADY_VALUE_MAX 4096

/* Set or (with value NULL) remove a value. False for an invalid key or value. */
bool shady_values_set(struct shady_server *server, void *owner, const char *key,
	const char *value);
const char *shady_values_get(const char *key);
void shady_values_forget_owner(struct shady_server *server, void *owner);
void shady_values_for_each(void (*fn)(const char *key, const char *value, void *data),
	void *data);
void shady_values_finish(void);

#endif
