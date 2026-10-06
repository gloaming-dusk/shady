#ifndef SHADY_IPC_H
#define SHADY_IPC_H

/*
 * Compositor IPC: a Unix socket speaking newline-delimited JSON, for shells,
 * bars and scripts. Clients query state, send commands and subscribe to the
 * event stream that Lua and plugins see. See docs/IPC_API.md.
 *
 * The socket lives at $XDG_RUNTIME_DIR/shady-<wayland socket>.sock and its
 * path is exported as SHADY_SOCKET so spawned programs find it.
 */

#include <stdbool.h>

struct shady_server;

#define SHADY_IPC_PROTOCOL_VERSION 1

bool shady_ipc_init(struct shady_server *server, const char *wayland_socket);
void shady_ipc_finish(struct shady_server *server);
/* Tell subscribers a published value changed (value NULL: removed). */
void shady_ipc_value_changed(struct shady_server *server, const char *key, const char *value);

#endif
