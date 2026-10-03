#ifndef SHADY_MODULE_CLOSE_ANIMATION_H
#define SHADY_MODULE_CLOSE_ANIMATION_H
#include "../../shady.h"
#include "state.h"
#include <wlr/types/wlr_xdg_shell.h>
struct wl_list;
#if SHADY_HAS_CLOSE_ANIMATION
void shady_close_animation_begin(struct shady_server *server);
void shady_close_animation_begin_window(struct shady_server *server,
	struct shady_toplevel *toplevel);
void shady_close_animation_get_effect(const struct shady_toplevel *toplevel,
	uint32_t *style, float *duration, float *strength,
	float *direction_x, float *direction_y);
void shady_close_animation_update_toplevel(struct shady_toplevel *toplevel,float dt);
void shady_close_animation_update_snapshots(struct wl_list *snapshots,float dt);
float shady_close_animation_progress(const struct shady_toplevel *toplevel);
enum shady_close_state shady_close_animation_state(const struct shady_toplevel *toplevel);
#else
static inline void shady_close_animation_begin_window(struct shady_server*s,struct shady_toplevel*t){(void)s;if(t)wlr_xdg_toplevel_send_close(t->xdg_toplevel);}
static inline void shady_close_animation_begin(struct shady_server*s){struct shady_toplevel*t=NULL;if(!wl_list_empty(&s->toplevels))t=wl_container_of(s->toplevels.next,t,link);shady_close_animation_begin_window(s,t);}
static inline void shady_close_animation_get_effect(const struct shady_toplevel*t,uint32_t*style,float*duration,float*strength,float*dx,float*dy){(void)t;if(style)*style=0;if(duration)*duration=.42f;if(strength)*strength=1.f;if(dx)*dx=0.f;if(dy)*dy=0.f;}
static inline void shady_close_animation_update_toplevel(struct shady_toplevel*t,float dt){(void)t;(void)dt;}
static inline void shady_close_animation_update_snapshots(struct wl_list*l,float dt){(void)l;(void)dt;}
static inline float shady_close_animation_progress(const struct shady_toplevel*t){(void)t;return 0.f;}
static inline enum shady_close_state shady_close_animation_state(const struct shady_toplevel*t){(void)t;return SHADY_CLOSE_IDLE;}
#endif
#endif
