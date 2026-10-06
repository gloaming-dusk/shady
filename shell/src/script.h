#ifndef SHADY_SHELL_SCRIPT_H
#define SHADY_SHELL_SCRIPT_H

/*
 * The shell's Lua runtime: loads shell.lua, exposes the `shell` API, owns
 * the bars and popups it declares and repaints them by calling their view
 * functions. A config change on disk is reloaded in a fresh Lua state and
 * swapped in only if it loads cleanly. See docs/SHELL_LUA_API.md.
 */

#include <stdbool.h>

struct shell;
struct shell_output;

bool shell_lua_init(struct shell *shell);
void shell_lua_finish(struct shell *shell);
void shell_lua_output_added(struct shell *shell, struct shell_output *output);
void shell_lua_output_removed(struct shell *shell, struct shell_output *output);
/* The compositor model changed: repaint every view. */
void shell_lua_model_changed(struct shell *shell);
/* Run the handlers registered with shell.on(name, fn). */
void shell_lua_emit(struct shell *shell, const char *event);

#endif
