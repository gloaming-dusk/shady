#ifndef SHADY_MODULE_SCENE_EFFECTS_H
#define SHADY_MODULE_SCENE_EFFECTS_H
#include "../../shady.h"
struct shady_gl_pipeline;
#if SHADY_HAS_SCENE_EFFECTS
void shady_scene_effects_draw_floor(struct shady_server*,struct shady_gl_pipeline*,const float vp[16]);
void shady_scene_effects_draw_shadows(struct shady_server*,struct shady_gl_pipeline*,const float vp[16],float logical_w,float logical_h,double ox,double oy);
void shady_scene_effects_draw_sides(struct shady_server*,struct shady_gl_pipeline*,const float mvp[16],const float model[16],float wobble_x,float wobble_y,float close_progress);
#else
static inline void shady_scene_effects_draw_floor(struct shady_server*s,struct shady_gl_pipeline*p,const float v[16]){(void)s;(void)p;(void)v;}
static inline void shady_scene_effects_draw_shadows(struct shady_server*s,struct shady_gl_pipeline*p,const float v[16],float w,float h,double x,double y){(void)s;(void)p;(void)v;(void)w;(void)h;(void)x;(void)y;}
static inline void shady_scene_effects_draw_sides(struct shady_server*s,struct shady_gl_pipeline*p,const float a[16],const float b[16],float x,float y,float c){(void)s;(void)p;(void)a;(void)b;(void)x;(void)y;(void)c;}
#endif
#endif
