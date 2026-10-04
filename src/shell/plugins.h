#ifndef SHADY_SHELL_PLUGINS_H
#define SHADY_SHELL_PLUGINS_H

/*
 * Native shell plugins (include/shady/shell_plugin.h): loading, and the
 * values, actions and widget types they register. Plugins live for the
 * whole shell process; a Lua config reload keeps them and their state.
 */

#include <cairo.h>
#include <stdbool.h>
#include <stddef.h>

struct shell;
struct shady_shell_props_handle;

void shell_plugins_init(struct shell *shell);
/* Load by name (libshady-shell-plugin-<name>.so on the search path) or by
 * path. `config_dir` and its plugins/ subdirectory are searched after
 * $SHADY_SHELL_PLUGIN_PATH, then the build and install directories.
 * Loading a plugin that is already loaded succeeds and does nothing. */
bool shell_plugins_load(const char *spec, const struct shady_shell_props_handle *options,
    const char *config_dir, char *error, size_t error_size);
/* Destroy every plugin, releasing everything they registered. */
void shell_plugins_finish(void);

const char *shell_plugins_value(const char *key);
/* Run an action; false if no plugin registered `name`. */
bool shell_plugins_action(const char *name, const char *argument);
void shell_plugins_draw_widget(const char *type, cairo_t *cr, double width, double height,
    const struct shady_shell_props_handle *props);

#endif
