#define _POSIX_C_SOURCE 200809L

#include "plugins.h"

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <shady/shell_plugin.h>

#include "script.h"
#include "shell.h"
#include "theme.h"
#include "ui.h"

/* The host handle a plugin sees is its own record. */
struct shady_shell_host_handle {
    struct wl_list link;
    char *name;
    char *path;
    void *handle;
    const struct shady_shell_plugin *module;
    bool initialized;
};
typedef struct shady_shell_host_handle plugin_t;

struct shady_shell_timer_handle {
    struct wl_list link;
    plugin_t *owner;
    struct shell_timer *timer;
    shady_shell_timer_fn fn;
    void *data;
};

struct shady_shell_watch_handle {
    struct wl_list link;
    plugin_t *owner;
    struct shell_watch *watch;
    shady_shell_watch_fn fn;
    void *data;
};

struct value {
    struct wl_list link;
    plugin_t *owner;
    char *key;
    char *value;
};

struct action {
    struct wl_list link;
    plugin_t *owner;
    char *name;
    shady_shell_action_fn fn;
    void *data;
};

struct widget_type {
    struct wl_list link;
    plugin_t *owner;
    char *name;
    void (*draw)(void *data, cairo_t *cr, double width, double height,
        const shady_shell_props *props);
    void *data;
};

static struct {
    struct shell *shell;
    struct wl_list plugins, timers, watches, values, actions, widgets;
    bool ready;
} host;

static void log_line(const char *who, const char *message) {
    fprintf(stderr, "shady-shell: %s: %s\n", who, message);
}

static void changed(void) {
    if (host.shell) shell_lua_model_changed(host.shell);
}

/* ---- the API given to plugins ----------------------------------------- */

static void api_log(shady_shell_host plugin, const char *message) {
    log_line(plugin && plugin->name ? plugin->name : "plugin", message ? message : "");
}

static struct value *find_value(const char *key) {
    struct value *v;
    wl_list_for_each(v, &host.values, link)
        if (!strcmp(v->key, key)) return v;
    return NULL;
}

static void value_free(struct value *v) {
    wl_list_remove(&v->link);
    free(v->key);
    free(v->value);
    free(v);
}

static bool api_set_value(shady_shell_host plugin, const char *key, const char *value) {
    if (!key || !*key) return false;
    struct value *v = find_value(key);
    if (!value) {
        if (v) {
            value_free(v);
            changed();
        }
        return true;
    }
    if (v && !strcmp(v->value, value)) return true;
    char *copy = strdup(value);
    if (!copy) return false;
    if (!v) {
        v = calloc(1, sizeof(*v));
        if (!v || !(v->key = strdup(key))) {
            free(v);
            free(copy);
            return false;
        }
        wl_list_insert(host.values.prev, &v->link);
    }
    v->owner = plugin;
    free(v->value);
    v->value = copy;
    changed();
    return true;
}

static const char *api_value(shady_shell_host plugin, const char *key) {
    (void)plugin;
    return shell_plugins_value(key);
}

static bool api_add_action(shady_shell_host plugin, const char *name,
        shady_shell_action_fn fn, void *data) {
    if (!name || !*name || !fn) return false;
    struct action *a;
    wl_list_for_each(a, &host.actions, link)
        if (!strcmp(a->name, name)) return false;
    a = calloc(1, sizeof(*a));
    if (!a || !(a->name = strdup(name))) {
        free(a);
        return false;
    }
    a->owner = plugin;
    a->fn = fn;
    a->data = data;
    wl_list_insert(host.actions.prev, &a->link);
    return true;
}

static bool api_add_widget(shady_shell_host plugin, const struct shady_shell_widget_type *type) {
    if (!type || !type->name || !*type->name || !type->draw) return false;
    struct widget_type *w;
    wl_list_for_each(w, &host.widgets, link)
        if (!strcmp(w->name, type->name)) return false;
    w = calloc(1, sizeof(*w));
    if (!w || !(w->name = strdup(type->name))) {
        free(w);
        return false;
    }
    w->owner = plugin;
    w->draw = type->draw;
    w->data = type->data;
    wl_list_insert(host.widgets.prev, &w->link);
    return true;
}

static void timer_fired(void *data) {
    struct shady_shell_timer_handle *t = data;
    shady_shell_timer_fn fn = t->fn;
    void *user = t->data;
    wl_list_remove(&t->link);
    free(t);
    fn(user);
}

static shady_shell_timer api_timer_add(shady_shell_host plugin, uint32_t ms,
        shady_shell_timer_fn fn, void *data) {
    if (!fn) return NULL;
    struct shady_shell_timer_handle *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->owner = plugin;
    t->fn = fn;
    t->data = data;
    t->timer = shell_timer_add(&host.shell->core, ms, timer_fired, t);
    if (!t->timer) {
        free(t);
        return NULL;
    }
    wl_list_insert(&host.timers, &t->link);
    return t;
}

static void timer_free(struct shady_shell_timer_handle *t) {
    shell_timer_cancel(t->timer);
    wl_list_remove(&t->link);
    free(t);
}

/* Only timers that have not fired are in the list; anything else is a
 * stale handle and is ignored. */
static void api_timer_cancel(shady_shell_host plugin, shady_shell_timer timer) {
    (void)plugin;
    struct shady_shell_timer_handle *t;
    wl_list_for_each(t, &host.timers, link) {
        if (t == timer) {
            timer_free(t);
            return;
        }
    }
}

static void watch_ready(int fd, short revents, void *data) {
    struct shady_shell_watch_handle *w = data;
    w->fn(fd, revents, w->data);
}

static shady_shell_watch api_watch_add(shady_shell_host plugin, int fd, short events,
        shady_shell_watch_fn fn, void *data) {
    if (fd < 0 || !fn) return NULL;
    struct shady_shell_watch_handle *w = calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->owner = plugin;
    w->fn = fn;
    w->data = data;
    w->watch = shell_watch_add(&host.shell->core, fd, events, watch_ready, w);
    if (!w->watch) {
        free(w);
        return NULL;
    }
    wl_list_insert(&host.watches, &w->link);
    return w;
}

/* The core only marks a removed watch and never calls it again, so the
 * wrapper can go at once, even from inside its own callback. */
static void watch_free(struct shady_shell_watch_handle *w) {
    shell_watch_remove(w->watch);
    wl_list_remove(&w->link);
    free(w);
}

static void api_watch_remove(shady_shell_host plugin, shady_shell_watch watch) {
    (void)plugin;
    struct shady_shell_watch_handle *w;
    wl_list_for_each(w, &host.watches, link) {
        if (w == watch) {
            watch_free(w);
            return;
        }
    }
}

static const char *api_prop_string(const shady_shell_props *props, const char *key) {
    const struct ui_prop *p = ui_find_prop(props, key);
    return p ? p->string : NULL;
}

static double api_prop_number(const shady_shell_props *props, const char *key, double fallback) {
    const struct ui_prop *p = ui_find_prop(props, key);
    if (!p) return fallback;
    if (p->is_number) return p->number;
    char *end = NULL;
    double v = strtod(p->string, &end);
    return end && end != p->string && *end == '\0' ? v : fallback;
}

static bool api_prop_color(const shady_shell_props *props, const char *key, double rgba[4]) {
    const struct ui_prop *p = ui_find_prop(props, key);
    return p && ui_parse_hex(p->string, rgba);
}

static void api_redraw(shady_shell_host plugin) {
    (void)plugin;
    changed();
}

static bool api_theme_color(shady_shell_host plugin, const char *name, double rgba[4]) {
    (void)plugin;
    static const struct { const char *name; const struct ui_rgb *rgb; } colours[] = {
        { "accent", &UI_ACCENT }, { "accent_2", &UI_ACCENT_2 },
        { "accent_deep", &UI_ACCENT_DEEP }, { "surface", &UI_SURFACE },
        { "text", &UI_TEXT }, { "text_dim", &UI_TEXT_DIM }, { "danger", &UI_DANGER },
    };
    for (size_t i = 0; name && i < sizeof(colours) / sizeof(colours[0]); i++) {
        if (strcmp(colours[i].name, name) != 0) continue;
        rgba[0] = colours[i].rgb->r;
        rgba[1] = colours[i].rgb->g;
        rgba[2] = colours[i].rgb->b;
        rgba[3] = 1.0;
        return true;
    }
    return false;
}

static const struct shady_shell_api_v1 api = {
    .abi_version = SHADY_SHELL_PLUGIN_ABI_V1,
    .struct_size = sizeof(struct shady_shell_api_v1),
    .log = api_log,
    .set_value = api_set_value,
    .value = api_value,
    .add_action = api_add_action,
    .add_widget = api_add_widget,
    .timer_add = api_timer_add,
    .timer_cancel = api_timer_cancel,
    .watch_add = api_watch_add,
    .watch_remove = api_watch_remove,
    .prop_string = api_prop_string,
    .prop_number = api_prop_number,
    .prop_color = api_prop_color,
    .redraw = api_redraw,
    .theme_color = api_theme_color,
};

/* ---- loading ---------------------------------------------------------- */

static void release_owned(plugin_t *plugin) {
    struct shady_shell_timer_handle *t, *ttmp;
    wl_list_for_each_safe(t, ttmp, &host.timers, link)
        if (t->owner == plugin) timer_free(t);
    struct shady_shell_watch_handle *w, *wtmp;
    wl_list_for_each_safe(w, wtmp, &host.watches, link)
        if (w->owner == plugin) watch_free(w);
    struct value *v, *vtmp;
    wl_list_for_each_safe(v, vtmp, &host.values, link)
        if (v->owner == plugin) value_free(v);
    struct action *a, *atmp;
    wl_list_for_each_safe(a, atmp, &host.actions, link) {
        if (a->owner != plugin) continue;
        wl_list_remove(&a->link);
        free(a->name);
        free(a);
    }
    struct widget_type *wt, *wttmp;
    wl_list_for_each_safe(wt, wttmp, &host.widgets, link) {
        if (wt->owner != plugin) continue;
        wl_list_remove(&wt->link);
        free(wt->name);
        free(wt);
    }
}

static void plugin_destroy(plugin_t *plugin) {
    if (plugin->initialized && plugin->module->destroy) plugin->module->destroy(plugin);
    release_owned(plugin);
    wl_list_remove(&plugin->link);
    if (plugin->handle) dlclose(plugin->handle);
    free(plugin->name);
    free(plugin->path);
    free(plugin);
}

static bool is_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool try_dir(const char *dir, const char *file, char *out, size_t size) {
    if (!dir || !*dir) return false;
    int n = snprintf(out, size, "%s/%s", dir, file);
    return n > 0 && (size_t)n < size && is_file(out);
}

static bool resolve(const char *spec, const char *config_dir, char *out, size_t size) {
    if (strchr(spec, '/')) {
        snprintf(out, size, "%s", spec);
        return is_file(out);
    }
    char file[NAME_MAX + 1];
    size_t len = strlen(spec);
    if (len > 3 && !strcmp(spec + len - 3, ".so")) snprintf(file, sizeof(file), "%s", spec);
    else snprintf(file, sizeof(file), "libshady-shell-plugin-%s.so", spec);

    const char *env = getenv("SHADY_SHELL_PLUGIN_PATH");
    if (env && *env) {
        char *copy = strdup(env);
        char *save = NULL;
        for (char *dir = copy ? strtok_r(copy, ":", &save) : NULL; dir;
                dir = strtok_r(NULL, ":", &save)) {
            if (try_dir(dir, file, out, size)) {
                free(copy);
                return true;
            }
        }
        free(copy);
    }
    char plugins_dir[PATH_MAX];
    snprintf(plugins_dir, sizeof(plugins_dir), "%s/plugins", config_dir ? config_dir : ".");
    return try_dir(config_dir, file, out, size) || try_dir(plugins_dir, file, out, size) ||
        try_dir(SHADY_SHELL_PLUGIN_DIR, file, out, size) ||
        try_dir(SHADY_SHELL_PLUGIN_INSTALL_DIR, file, out, size);
}

void shell_plugins_init(struct shell *shell) {
    host.shell = shell;
    wl_list_init(&host.plugins);
    wl_list_init(&host.timers);
    wl_list_init(&host.watches);
    wl_list_init(&host.values);
    wl_list_init(&host.actions);
    wl_list_init(&host.widgets);
    host.ready = true;
}

bool shell_plugins_load(const char *spec, const struct shady_shell_props_handle *options,
        const char *config_dir, char *error, size_t error_size) {
    if (!host.ready || !spec || !*spec) {
        snprintf(error, error_size, "no plugin given");
        return false;
    }
    plugin_t *plugin;
    wl_list_for_each(plugin, &host.plugins, link)
        if (!strcmp(plugin->name, spec) || (plugin->path && !strcmp(plugin->path, spec)))
            return true;

    char path[PATH_MAX];
    if (!resolve(spec, config_dir, path, sizeof(path))) {
        snprintf(error, error_size, "plugin %s not found", spec);
        return false;
    }
    wl_list_for_each(plugin, &host.plugins, link)
        if (!strcmp(plugin->path, path)) return true;

    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        snprintf(error, error_size, "%s", dlerror());
        return false;
    }
    shady_shell_plugin_entry_v1_fn entry =
        (shady_shell_plugin_entry_v1_fn)dlsym(handle, SHADY_SHELL_PLUGIN_ENTRY_V1);
    plugin = calloc(1, sizeof(*plugin));
    if (!entry || !plugin) {
        snprintf(error, error_size, "%s has no %s", path, SHADY_SHELL_PLUGIN_ENTRY_V1);
        free(plugin);
        dlclose(handle);
        return false;
    }
    plugin->handle = handle;
    plugin->path = strdup(path);
    wl_list_init(&plugin->link);
    plugin->module = entry(SHADY_SHELL_PLUGIN_ABI_V1, &api, plugin);
    if (!plugin->module || !plugin->module->name || !plugin->module->init) {
        snprintf(error, error_size, "%s refused this shell (ABI %u)", path,
            SHADY_SHELL_PLUGIN_ABI_V1);
        free(plugin->path);
        free(plugin);
        dlclose(handle);
        return false;
    }
    plugin->name = strdup(plugin->module->name);
    wl_list_insert(host.plugins.prev, &plugin->link);
    static const struct shady_shell_props_handle empty;
    if (!plugin->module->init(plugin, options ? options : &empty)) {
        snprintf(error, error_size, "plugin %s failed to initialise", plugin->name);
        plugin_destroy(plugin);
        return false;
    }
    plugin->initialized = true;
    fprintf(stderr, "shady-shell: loaded plugin %s from %s\n", plugin->name, path);
    return true;
}

void shell_plugins_finish(void) {
    if (!host.ready) return;
    /* Newest first, so plugins can depend on older ones. */
    while (!wl_list_empty(&host.plugins)) {
        plugin_t *plugin = wl_container_of(host.plugins.prev, plugin, link);
        plugin_destroy(plugin);
    }
    struct value *v, *vtmp;
    wl_list_for_each_safe(v, vtmp, &host.values, link) value_free(v);
    host.ready = false;
}

const char *shell_plugins_value(const char *key) {
    if (!host.ready || !key) return NULL;
    struct value *v = find_value(key);
    return v ? v->value : NULL;
}

bool shell_plugins_action(const char *name, const char *argument) {
    if (!host.ready || !name) return false;
    struct action *a;
    wl_list_for_each(a, &host.actions, link) {
        if (strcmp(a->name, name) != 0) continue;
        a->fn(argument ? argument : "", a->data);
        return true;
    }
    return false;
}

void shell_plugins_draw_widget(const char *type, cairo_t *cr, double width, double height,
        const struct shady_shell_props_handle *props) {
    if (!host.ready || !type) return;
    struct widget_type *w;
    wl_list_for_each(w, &host.widgets, link) {
        if (strcmp(w->name, type) != 0) continue;
        w->draw(w->data, cr, width, height, props);
        return;
    }
    static char warned[64];
    if (strncmp(warned, type, sizeof(warned) - 1) != 0) {
        snprintf(warned, sizeof(warned), "%s", type);
        fprintf(stderr, "shady-shell: no plugin provides widget %s\n", type);
    }
}
