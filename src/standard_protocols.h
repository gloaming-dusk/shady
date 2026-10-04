#ifndef SHADY_STANDARD_PROTOCOLS_H
#define SHADY_STANDARD_PROTOCOLS_H

/*
 * Standard window and workspace protocols for third-party shells and bars
 * (waybar, Quickshell, eww, ...), mirroring the compositor's own state:
 *
 *   wlr-foreign-toplevel-management-unstable-v1  task lists: titles, states,
 *                                                activate/close/maximize/fullscreen
 *   ext-foreign-toplevel-list-v1                 the window list, read-only
 *   ext-workspace-v1                             workspaces, activation
 *
 * shady-shell keeps using shady-shell-v1; these are for everyone else.
 */

#include <stdbool.h>

struct shady_server;

bool shady_standard_protocols_init(struct shady_server *server);
void shady_standard_protocols_finish(struct shady_server *server);

#endif
