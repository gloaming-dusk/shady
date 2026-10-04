#ifndef SHADY_PUBLIC_SHELL_PLUGIN_H
#define SHADY_PUBLIC_SHELL_PLUGIN_H

/*
 * Native plugins for shady-shell.
 *
 * A shell plugin is a shared object loaded by the shell's Lua config with
 * shell.plugin("name"). It extends the shell with things Lua cannot reach
 * well: system services, long-lived sockets, custom-drawn widgets. It never
 * touches Lua directly; it talks to configs through
 *
 *   values   strings the plugin publishes (shell.value("sysinfo.cpu"));
 *            a change repaints the views that read them
 *   actions  named callbacks configs invoke (shell.action("name", arg))
 *   widgets  widget types drawn with cairo (shell.widget("name", {...}))
 *
 * and runs in the shell's event loop through timers and fd watches.
 * Everything a plugin registers is released by the shell when the plugin
 * is destroyed. See docs/SHELL_PLUGIN_API.md.
 *
 * Compatibility follows the compositor plugin API: fields of
 * struct shady_shell_api_v1 are append-only, so check optional entries with
 * SHADY_SHELL_API_HAS before calling them.
 */

#include <cairo.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHADY_SHELL_PLUGIN_ABI_V1 1u
#define SHADY_SHELL_PLUGIN_ENTRY_V1 "shady_shell_plugin_entry_v1"

#define SHADY_SHELL_API_HAS(api, member) \
    ((api) && (api)->struct_size >= offsetof(struct shady_shell_api_v1, member) + \
        sizeof((api)->member) && (api)->member)

/* One per loaded plugin; identifies the caller in every API call. */
typedef struct shady_shell_host_handle *shady_shell_host;
typedef struct shady_shell_timer_handle *shady_shell_timer;
typedef struct shady_shell_watch_handle *shady_shell_watch;
/* Read-only key/value table: plugin options or a widget's properties. */
typedef struct shady_shell_props_handle shady_shell_props;

typedef void (*shady_shell_timer_fn)(void *data);
/* `revents` are poll(2) bits. */
typedef void (*shady_shell_watch_fn)(int fd, short revents, void *data);
/* `argument` is the string passed from Lua, "" if none. */
typedef void (*shady_shell_action_fn)(const char *argument, void *data);

struct shady_shell_widget_type {
    /* Lua uses this name: shell.widget(name, props). Prefix it with the
     * plugin name, e.g. "sysinfo.graph". */
    const char *name;
    /* Draw into the widget's box: (0, 0) is its top-left corner, units are
     * logical pixels, and drawing is clipped to width x height. */
    void (*draw)(void *data, cairo_t *cr, double width, double height,
        const shady_shell_props *props);
    void *data;
};

struct shady_shell_api_v1 {
    uint32_t abi_version;
    uint32_t struct_size;

    void (*log)(shady_shell_host host, const char *message);

    /* Values. set_value copies both strings; NULL value removes the key. */
    bool (*set_value)(shady_shell_host host, const char *key, const char *value);
    const char *(*value)(shady_shell_host host, const char *key);

    bool (*add_action)(shady_shell_host host, const char *name,
        shady_shell_action_fn fn, void *data);
    bool (*add_widget)(shady_shell_host host, const struct shady_shell_widget_type *type);

    /* One-shot timer; re-add it from the callback to repeat. */
    shady_shell_timer (*timer_add)(shady_shell_host host, uint32_t ms,
        shady_shell_timer_fn fn, void *data);
    void (*timer_cancel)(shady_shell_host host, shady_shell_timer timer);
    shady_shell_watch (*watch_add)(shady_shell_host host, int fd, short events,
        shady_shell_watch_fn fn, void *data);
    void (*watch_remove)(shady_shell_host host, shady_shell_watch watch);

    /* Props: options passed to init, or a widget's properties in draw. */
    const char *(*prop_string)(const shady_shell_props *props, const char *key);
    double (*prop_number)(const shady_shell_props *props, const char *key, double fallback);
    /* Colours ("#RRGGBB[AA]") parsed to 0..1 RGBA; false if missing or bad. */
    bool (*prop_color)(const shady_shell_props *props, const char *key, double rgba[4]);

    /* Repaint every view, e.g. after a widget's data changed without a
     * value changing. */
    void (*redraw)(shady_shell_host host);
    /* A theme colour by name (accent, accent_2, accent_deep, surface,
     * text, text_dim, danger) as RGBA; false for unknown names. */
    bool (*theme_color)(shady_shell_host host, const char *name, double rgba[4]);
};

struct shady_shell_plugin {
    const char *name;
    /* `options` is the table given to shell.plugin(name, options). Return
     * false to refuse loading; the plugin is then destroyed. */
    bool (*init)(shady_shell_host host, const shady_shell_props *options);
    void (*destroy)(shady_shell_host host);
};

typedef const struct shady_shell_plugin *(*shady_shell_plugin_entry_v1_fn)(
    uint32_t host_abi, const struct shady_shell_api_v1 *api, shady_shell_host host);

#endif
