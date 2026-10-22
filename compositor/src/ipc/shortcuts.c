#include "shortcuts.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-util.h>
#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>

#include "../shady.h"
#include "../module/module.h"

struct entry {
	struct wl_list link;
	void *owner;
	char *group; /* NULL: the owning plugin's name */
	char keys[SHADY_SHORTCUT_KEYS_MAX];
	char *description;
};

/* One compositor per process, so one list. Initialised on first use. */
static struct wl_list entries = { &entries, &entries };

static uint32_t modifier_from_name(const char *name) {
	if (!strcasecmp(name, "Super") || !strcasecmp(name, "Logo")) return WLR_MODIFIER_LOGO;
	if (!strcasecmp(name, "Ctrl") || !strcasecmp(name, "Control")) return WLR_MODIFIER_CTRL;
	if (!strcasecmp(name, "Alt")) return WLR_MODIFIER_ALT;
	if (!strcasecmp(name, "Shift")) return WLR_MODIFIER_SHIFT;
	return 0;
}

static void format_bind(xkb_keysym_t sym, uint32_t modifiers, char *out, uint32_t size) {
	char name[64];
	if (xkb_keysym_get_name(xkb_keysym_to_lower(sym), name, sizeof(name)) < 0) name[0] = '\0';
	/* Letters read better in capitals, as printed on the keys. */
	if (name[0] >= 'a' && name[0] <= 'z' && name[1] == '\0') name[0] -= 'a' - 'A';
	snprintf(out, size, "%s%s%s%s%s",
		modifiers & WLR_MODIFIER_LOGO ? "Super+" : "",
		modifiers & WLR_MODIFIER_CTRL ? "Ctrl+" : "",
		modifiers & WLR_MODIFIER_ALT ? "Alt+" : "",
		modifiers & WLR_MODIFIER_SHIFT ? "Shift+" : "",
		name);
}

void shady_shortcut_format(const char *keys, char *out, uint32_t size) {
	char buf[SHADY_SHORTCUT_KEYS_MAX];
	snprintf(out, size, "%s", keys);
	if (strlen(keys) >= sizeof(buf) || strchr(keys, ' ')) return;
	strcpy(buf, keys);
	uint32_t modifiers = 0;
	const char *key = NULL;
	char *save = NULL;
	for (char *token = strtok_r(buf, "+", &save); token; token = strtok_r(NULL, "+", &save)) {
		uint32_t m = modifier_from_name(token);
		if (m) modifiers |= m;
		else if (key) return;
		else key = token;
	}
	if (!key) return;
	xkb_keysym_t sym = xkb_keysym_from_name(key, XKB_KEYSYM_CASE_INSENSITIVE);
	if (sym == XKB_KEY_NoSymbol) return;
	format_bind(sym, modifiers, out, size);
}

static bool printable(const char *text, size_t max) {
	size_t len = text ? strlen(text) : 0;
	if (len == 0 || len >= max) return false;
	for (const unsigned char *p = (const unsigned char *)text; *p; p++)
		if (*p < 0x20 || *p == 0x7f) return false;
	return true;
}

static void entry_free(struct entry *e) {
	wl_list_remove(&e->link);
	free(e->group);
	free(e->description);
	free(e);
}

bool shady_shortcuts_declare(void *owner, const char *group, const char *keys,
		const char *description) {
	if (!printable(keys, SHADY_SHORTCUT_KEYS_MAX) ||
			(group && !printable(group, SHADY_SHORTCUT_GROUP_MAX)) ||
			(description && !printable(description, SHADY_SHORTCUT_DESCRIPTION_MAX)))
		return false;
	char canonical[SHADY_SHORTCUT_KEYS_MAX];
	shady_shortcut_format(keys, canonical, sizeof(canonical));
	struct entry *e, *found = NULL;
	wl_list_for_each(e, &entries, link)
		if (e->owner == owner && !strcmp(e->keys, canonical)) found = e;
	if (!description) {
		if (found) entry_free(found);
		return true;
	}
	char *copy = strdup(description);
	char *group_copy = group ? strdup(group) : NULL;
	if (!copy || (group && !group_copy)) {
		free(copy);
		free(group_copy);
		return false;
	}
	if (!found) {
		if (!(found = calloc(1, sizeof(*found)))) {
			free(copy);
			free(group_copy);
			return false;
		}
		found->owner = owner;
		snprintf(found->keys, sizeof(found->keys), "%s", canonical);
		wl_list_insert(entries.prev, &found->link);
	}
	free(found->description);
	free(found->group);
	found->description = copy;
	found->group = group_copy;
	return true;
}

void shady_shortcuts_forget_owner(void *owner) {
	struct entry *e, *tmp;
	wl_list_for_each_safe(e, tmp, &entries, link)
		if (e->owner == owner) entry_free(e);
}

void shady_shortcuts_finish(void) {
	while (!wl_list_empty(&entries)) {
		struct entry *e = wl_container_of(entries.next, e, link);
		entry_free(e);
	}
}

static bool module_active(struct shady_server *server, const char *name) {
	ssize_t i = shady_module_index_by_name(server, name);
	return i >= 0 && server->modules.active[i];
}

/* The active plugin whose load base is owner, or NULL. */
static const char *plugin_name(struct shady_server *server, void *owner) {
	for (size_t i = 0; i < server->modules.count; i++)
		if (owner && server->modules.plugin_base[i] == owner && server->modules.active[i])
			return server->modules.plugin_name[i];
	return NULL;
}

struct core_bind {
	const char *group;
	const char *module; /* NULL: always */
	const char *description;
	size_t binds[4];    /* offsets into struct shady_config, 0-terminated */
};

#define B(field) offsetof(struct shady_config, field)
static const struct core_bind core_binds[] = {
	{ "Shady", NULL, "Show these shortcuts", { B(bind_show_shortcuts) } },
	{ "Shady", NULL, "Focus the next window", { B(bind_cycle_windows) } },
	{ "Shady", "close-animation", "Close the focused window", { B(bind_close_window) } },
	{ "Shady", NULL, "Quit Shady", { B(bind_quit) } },
	{ "Camera", "spatial", "Pan the camera",
		{ B(bind_camera_left), B(bind_camera_right), B(bind_camera_up), B(bind_camera_down) } },
	{ "Camera", "spatial", "Turn the camera",
		{ B(bind_camera_yaw_left), B(bind_camera_yaw_right) } },
	{ "Camera", "spatial", "Zoom in and out", { B(bind_camera_zoom_in), B(bind_camera_zoom_out) } },
	{ "Camera", "spatial", "Reset the camera", { B(bind_camera_reset) } },
	{ "Camera", "spatial", "Show collision and picking", { B(bind_debug_ray) } },
	{ "First person", "fps", "Enter or leave first-person mode", { B(bind_fps_toggle) } },
	{ "First person", "fps", "Capture the pointer", { B(bind_fps_capture) } },
	{ "First person", "physics", "Turn window gravity on or off", { B(bind_gravity_toggle) } },
};
#undef B

/* Keys first-person mode reads directly, not configurable. */
static const struct shady_shortcut fps_keys[] = {
	{ "W A S D", "First person", "Walk" },
	{ "Space", "First person", "Jump" },
	{ "Click", "First person", "Pick up or drop a window" },
	{ "Right-click", "First person", "Throw the window you hold" },
};

static void for_each_core(struct shady_server *server,
		void (*fn)(const struct shady_shortcut *shortcut, void *data), void *data) {
	for (size_t i = 0; i < sizeof(core_binds) / sizeof(core_binds[0]); i++) {
		const struct core_bind *c = &core_binds[i];
		if (c->module && !module_active(server, c->module)) continue;
		const struct shady_keybind *binds[4];
		size_t count = 0;
		bool same_modifiers = true;
		for (size_t b = 0; b < 4 && c->binds[b]; b++) {
			const struct shady_keybind *bind =
				(const struct shady_keybind *)((const char *)&server->config + c->binds[b]);
			if (bind->sym == XKB_KEY_NoSymbol) continue;
			same_modifiers &= !count || bind->modifiers == binds[0]->modifiers;
			binds[count++] = bind;
		}
		/* "Alt+Left/Right/Up/Down" when the modifiers agree, else
		 * "Alt+Q Super+E". */
		char keys[4 * SHADY_SHORTCUT_KEYS_MAX] = "";
		for (size_t b = 0; b < count; b++) {
			char one[SHADY_SHORTCUT_KEYS_MAX];
			format_bind(binds[b]->sym, b && same_modifiers ? 0 : binds[b]->modifiers,
				one, sizeof(one));
			size_t len = strlen(keys);
			snprintf(keys + len, sizeof(keys) - len, "%s%s",
				!b ? "" : same_modifiers ? "/" : " ", one);
		}
		if (!keys[0]) continue;
		fn(&(struct shady_shortcut){ keys, c->group, c->description }, data);
	}
	if (module_active(server, "fps"))
		for (size_t i = 0; i < sizeof(fps_keys) / sizeof(fps_keys[0]); i++)
			fn(&fps_keys[i], data);
}

void shady_shortcuts_for_each(struct shady_server *server,
		void (*fn)(const struct shady_shortcut *shortcut, void *data), void *data) {
	for_each_core(server, fn, data);
	struct entry *e;
	wl_list_for_each(e, &entries, link) {
		const char *group = e->group ? e->group : plugin_name(server, e->owner);
		if (!group) continue;
		fn(&(struct shady_shortcut){ e->keys, group, e->description }, data);
	}
}
