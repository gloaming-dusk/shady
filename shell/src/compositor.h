#ifndef SHADY_SHELL_COMPOSITOR_H
#define SHADY_SHELL_COMPOSITOR_H

/*
 * The shell's link to the compositor's IPC socket (docs/IPC_API.md). It
 * subscribes to `value.changed` and keeps every published value, which
 * configs read with shell.compositor_value(key); a change repaints the
 * views. The socket is $SHADY_SHELL_IPC_SOCKET, $SHADY_SOCKET (set by the
 * compositor for programs it starts) or the default path; if it is missing
 * or goes away, the shell keeps retrying.
 */

#include <stdbool.h>
#include <stddef.h>

struct shell;

void shell_compositor_init(struct shell *shell);
void shell_compositor_finish(void);
const char *shell_compositor_value(const char *key);
/* Press a shortcut through the compositor (the IPC `key` command), e.g.
 * "Super+t". False when not connected or the keys look malformed. */
bool shell_compositor_key(const char *keys);

/* The compositor's shortcut list, as of its last `shortcuts.toggle` event
 * (Super+Shift+/ by default). Each one arriving in full emits the shell's
 * `shortcuts` event. */
struct shell_shortcut {
    char *keys;
    char *group;
    char *description;
};
size_t shell_compositor_shortcut_count(void);
const struct shell_shortcut *shell_compositor_shortcut_at(size_t index);
/* Ask the compositor to toggle the overlay: it answers with the event. */
bool shell_compositor_toggle_shortcuts(void);

#endif
