#ifndef SHADY_IPC_SHORTCUTS_H
#define SHADY_IPC_SHORTCUTS_H

/*
 * The shortcut list: every key combination the session answers to, with a
 * line saying what it does, for the shortcut overlay (Super+Shift+/ by
 * default) and the IPC `shortcuts` query.
 *
 * Three sources feed it:
 * - the compositor's own bindings, read from the config whenever the list
 *   is walked, so `bind.*` settings are always reflected;
 * - shady.bind(keys, fn, description) in Lua;
 * - native plugins, through the plugin API's declare_shortcut.
 *
 * A declared shortcut belongs to its owner (a plugin's load base, or the
 * Lua runtime) and goes away with it. Plugin shortcuts are listed under the
 * plugin's name and only while the plugin's module is active.
 */

#include <stdbool.h>
#include <stdint.h>

struct shady_server;

#define SHADY_SHORTCUT_KEYS_MAX 64
#define SHADY_SHORTCUT_DESCRIPTION_MAX 160
#define SHADY_SHORTCUT_GROUP_MAX 64

struct shady_shortcut {
	const char *keys;        /* "Super+Shift+Z", or a free label such as "W A S D" */
	const char *group;       /* "Shady", "First person", a plugin name, ... */
	const char *description;
};

/* Add, replace (same owner and keys) or, with description NULL, remove a
 * shortcut. `group` NULL means "the plugin whose load base is owner".
 * False for empty or overlong text. */
bool shady_shortcuts_declare(void *owner, const char *group, const char *keys,
	const char *description);
void shady_shortcuts_forget_owner(void *owner);
/* Every listed shortcut, the compositor's own first. */
void shady_shortcuts_for_each(struct shady_server *server,
	void (*fn)(const struct shady_shortcut *shortcut, void *data), void *data);
void shady_shortcuts_finish(void);

/* Canonical spelling of "super+shift+z" -> "Super+Shift+Z". Text that is
 * not a single key combination is copied as it is. */
void shady_shortcut_format(const char *keys, char *out, uint32_t size);

#endif
