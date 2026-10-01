#ifndef SHADY_EXPERIMENTAL_RUNTIME_H
#define SHADY_EXPERIMENTAL_RUNTIME_H

#include <stdbool.h>

struct shady_server;

/*
 * Advance spatial-desktop simulation at most once for a compositor tick.
 * Returns true when state was advanced and render matrices may need rebuild.
 */
bool shady_experimental_update(struct shady_server *server,
	float logical_w, float logical_h);

#endif
