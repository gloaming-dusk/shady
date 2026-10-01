#ifndef SHADY_MODULE_CLOSE_ANIMATION_H
#define SHADY_MODULE_CLOSE_ANIMATION_H
#include "../../shady.h"
#include "state.h"
#include <wlr/types/wlr_xdg_shell.h>
struct wl_list;
#if SHADY_HAS_CLOSE_ANIMATION
void shady_close_animation_begin(struct shady_server *server);
void shady_close_animation_update_toplevel(struct shady_toplevel *toplevel,float dt);
void shady_close_animation_update_snapshots(struct wl_list *snapshots,float dt);
float shady_close_animation_progress(const struct shady_toplevel *toplevel);
enum shady_close_state shady_close_animation_state(const struct shady_toplevel *toplevel);
#else
static inline void shady_close_animation_begin(struct shady_server*s){struct shady_toplevel*t=NULL;if(!wl_list_empty(&s->toplevels))t=wl_container_of(s->toplevels.next,t,link);if(t)wlr_xdg_toplevel_send_close(t->xdg_toplevel);}
static inline void shady_close_animation_update_toplevel(struct shady_toplevel*t,float dt){(void)t;(void)dt;}
static inline void shady_close_animation_update_snapshots(struct wl_list*l,float dt){(void)l;(void)dt;}
static inline float shady_close_animation_progress(const struct shady_toplevel*t){(void)t;return 0.f;}
static inline enum shady_close_state shady_close_animation_state(const struct shady_toplevel*t){(void)t;return SHADY_CLOSE_IDLE;}
#endif
#endif
