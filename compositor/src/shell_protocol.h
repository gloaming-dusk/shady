#ifndef SHADY_SHELL_PROTOCOL_H
#define SHADY_SHELL_PROTOCOL_H

#include <stdbool.h>

struct shady_server;

bool shady_shell_protocol_init(struct shady_server *server);
void shady_shell_protocol_finish(struct shady_server *server);
void shady_shell_protocol_toggle_launcher(struct shady_server *server);

#endif
