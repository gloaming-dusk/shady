#ifndef SHADY_MODULE_CLOSE_ANIMATION_H
#define SHADY_MODULE_CLOSE_ANIMATION_H
#include "../../shady.h"
#include <wlr/types/wlr_xdg_shell.h>
struct wl_list;
#if SHADY_HAS_CLOSE_ANIMATION
void shady_close_animation_begin(struct shady_server *server);
void shady_close_animation_update_toplevel(struct shady_toplevel *toplevel,float dt);
void shady_close_animation_update_snapshots(struct wl_list *snapshots,float dt);
#else
static inline void shady_close_animation_begin(struct shady_server*s){struct shady_toplevel*t=NULL;if(!wl_list_empty(&s->toplevels))t=wl_container_of(s->toplevels.next,t,link);if(t)wlr_xdg_toplevel_send_close(t->xdg_toplevel);}
static inline void shady_close_animation_update_toplevel(struct shady_toplevel*t,float dt){(void)t;(void)dt;}
static inline void shady_close_animation_update_snapshots(struct wl_list*l,float dt){(void)l;(void)dt;}
#endif
#endif
