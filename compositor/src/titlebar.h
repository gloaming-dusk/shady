#ifndef SHADY_TITLEBAR_H
#define SHADY_TITLEBAR_H

#include <stdbool.h>

struct shady_toplevel;

bool shady_titlebar_init(struct shady_toplevel *toplevel);
void shady_titlebar_refresh(struct shady_toplevel *toplevel);
void shady_titlebar_fini(struct shady_toplevel *toplevel);
void shady_titlebar_global_fini(void);

#endif
