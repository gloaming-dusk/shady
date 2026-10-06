#include "values.h"

#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>

#include "ipc.h"

struct value {
	struct wl_list link;
	void *owner;
	char *key;
	char *value;
};

/* One compositor per process, so one store. Initialised on first use. */
static struct wl_list values = { &values, &values };

static bool valid_key(const char *key) {
	size_t len = key ? strlen(key) : 0;
	if (len == 0 || len > SHADY_VALUE_KEY_MAX) return false;
	for (const char *p = key; *p; p++) {
		char c = *p;
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
				c == '.' || c == '_' || c == '-'))
			return false;
	}
	return true;
}

static struct value *find(const char *key) {
	struct value *v;
	wl_list_for_each(v, &values, link)
		if (!strcmp(v->key, key)) return v;
	return NULL;
}

static void value_free(struct value *v) {
	wl_list_remove(&v->link);
	free(v->key);
	free(v->value);
	free(v);
}

bool shady_values_set(struct shady_server *server, void *owner, const char *key,
		const char *value) {
	if (!valid_key(key) || (value && strlen(value) > SHADY_VALUE_MAX)) return false;
	struct value *v = find(key);
	if (!value) {
		if (!v) return true;
		value_free(v);
		shady_ipc_value_changed(server, key, NULL);
		return true;
	}
	if (v && !strcmp(v->value, value)) {
		v->owner = owner;
		return true;
	}
	char *copy = strdup(value);
	if (!copy) return false;
	if (!v) {
		v = calloc(1, sizeof(*v));
		if (!v || !(v->key = strdup(key))) {
			free(v);
			free(copy);
			return false;
		}
		wl_list_insert(values.prev, &v->link);
	}
	free(v->value);
	v->value = copy;
	v->owner = owner;
	shady_ipc_value_changed(server, key, value);
	return true;
}

const char *shady_values_get(const char *key) {
	struct value *v = key ? find(key) : NULL;
	return v ? v->value : NULL;
}

void shady_values_forget_owner(struct shady_server *server, void *owner) {
	struct value *v, *tmp;
	wl_list_for_each_safe(v, tmp, &values, link) {
		if (v->owner != owner) continue;
		char *key = v->key;
		v->key = NULL;
		value_free(v);
		shady_ipc_value_changed(server, key, NULL);
		free(key);
	}
}

void shady_values_for_each(void (*fn)(const char *key, const char *value, void *data),
		void *data) {
	struct value *v;
	wl_list_for_each(v, &values, link) fn(v->key, v->value, data);
}

void shady_values_finish(void) {
	while (!wl_list_empty(&values)) {
		struct value *v = wl_container_of(values.next, v, link);
		value_free(v);
	}
}
