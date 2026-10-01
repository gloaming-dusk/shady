#include "close_animation.h"
#include "../window_motion/window_motion.h"
#include <stdlib.h>
#include <GLES2/gl2.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "../../render/render.h"
#define CRUMPLE_SECONDS .42f
#define WAIT_SECONDS .25f
#define RESTORE_SECONDS .32f
/* Snapshot prefix shared with renderer; only fields needed for lifetime update. */
struct close_snapshot_lifetime { struct wl_list link; struct shady_toplevel *toplevel; struct shady_server *server; GLuint texture; int texture_width,texture_height; float x,y,width,height,tilt_x,tilt_y,z,wobble_x,wobble_y; bool has_alpha,dirty,animating; float progress; };
void shady_close_animation_begin(struct shady_server*s){struct shady_toplevel*t=NULL;if(!wl_list_empty(&s->toplevels))t=wl_container_of(s->toplevels.next,t,link);if(!t)return;if(!s->config.close_animation){wlr_xdg_toplevel_send_close(t->xdg_toplevel);return;}if(t->experimental.close.state!=SHADY_CLOSE_IDLE&&t->experimental.close.state!=SHADY_CLOSE_ARMED)return;t->experimental.close.state=SHADY_CLOSE_CRUMPLING;t->experimental.close.progress=0;t->experimental.close.wait_time=0;shady_window_motion_add_impulse(s,t,.10f,-.07f,0.f,0.f);shady_render_schedule_all_outputs(s);}
void shady_close_animation_update_toplevel(struct shady_toplevel*t,float dt){switch(t->experimental.close.state){case SHADY_CLOSE_CRUMPLING:t->experimental.close.progress+=dt/CRUMPLE_SECONDS;if(t->experimental.close.progress>=1){t->experimental.close.progress=1;t->experimental.close.wait_time=0;wlr_xdg_toplevel_send_close(t->xdg_toplevel);t->experimental.close.state=SHADY_CLOSE_WAITING;}break;case SHADY_CLOSE_WAITING:t->experimental.close.wait_time+=dt;if(t->experimental.close.wait_time>=WAIT_SECONDS)t->experimental.close.state=SHADY_CLOSE_RESTORING;break;case SHADY_CLOSE_RESTORING:t->experimental.close.progress-=dt/RESTORE_SECONDS;if(t->experimental.close.progress<=0){t->experimental.close.progress=0;t->experimental.close.wait_time=0;t->experimental.close.state=SHADY_CLOSE_ARMED;}break;default:break;}}
void shady_close_animation_update_snapshots(struct wl_list*l,float dt){struct close_snapshot_lifetime*s,*tmp;wl_list_for_each_safe(s,tmp,l,link){if(!s->animating)continue;s->progress+=dt/CRUMPLE_SECONDS;if(s->progress>=1){if(s->texture)glDeleteTextures(1,&s->texture);wl_list_remove(&s->link);free(s);}}}
