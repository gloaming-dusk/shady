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

struct shell;

void shell_compositor_init(struct shell *shell);
void shell_compositor_finish(void);
const char *shell_compositor_value(const char *key);

#endif
