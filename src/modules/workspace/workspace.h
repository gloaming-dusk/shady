#ifndef SHADY_WORKSPACE_H
#define SHADY_WORKSPACE_H

#include <stdbool.h>
#include <stddef.h>

struct shady_server;
struct shady_toplevel;

bool shady_workspace_switch(struct shady_server *server, const char *name);
bool shady_workspace_move_toplevel(struct shady_toplevel *toplevel, const char *name);
const char *shady_workspace_current_name(struct shady_server *server);
const char *shady_workspace_toplevel_name(struct shady_toplevel *toplevel);
size_t shady_workspace_count(struct shady_server *server);
const char *shady_workspace_name_at(struct shady_server *server, size_t index);

#endif
