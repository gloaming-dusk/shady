#ifndef SHADY_SHELL_PROTOCOL_H
#define SHADY_SHELL_PROTOCOL_H

#include <stdbool.h>

struct shady_server;

bool shady_shell_protocol_init(struct shady_server *server);
void shady_shell_protocol_finish(struct shady_server *server);
/* False when no shell is there to hear it. */
bool shady_shell_protocol_toggle_launcher(struct shady_server *server);
/* Whether a shell (shady-shell, or any client of shady-shell-v1) is bound. */
bool shady_shell_protocol_connected(struct shady_server *server);

#endif
