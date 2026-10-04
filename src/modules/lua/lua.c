#include "lua.h"
#include "state.h"
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/input-event-codes.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <wlr/util/log.h>
#include "../../shady.h"
#include "../../render/render.h"
#include "../../event/event.h"
#include "../../plugin/plugin.h"
#include "../../plugin/manager.h"
#include "../../plugin/manager_lua.h"
#include "../../shell_protocol.h"
#include "../physics/physics.h"
#include "../fps/fps.h"
#include "../fps/state.h"
#include "../spatial/state.h"
#include "../close_animation/close_animation.h"
#include "../workspace/workspace.h"
#include "../workspace/state.h"
#include <string.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>

static struct shady_server *lua_server;
static uint64_t lua_next_handler_id = 1;

struct lua_timer {
	struct wl_list link;
	lua_State *L;
	int callback_ref;
	struct wl_event_source *source;
};
static struct wl_list lua_timers;

struct lua_child_watch {
	struct wl_list link;
	lua_State *L;
	pid_t pid;
	int fd;
	int callback_ref;
	char path[4096];
	struct wl_event_source *source;
};
static struct wl_list lua_child_watches;

#define SHADY_LUA_WINDOW_MT "shady.window"
#define SHADY_LUA_OUTPUT_MT "shady.output"
#define SHADY_LUA_SEAT_MT "shady.seat"
#define SHADY_LUA_MODULE_MT "shady.module"

struct lua_window_handle { struct shady_toplevel *ptr; };
struct lua_output_handle { struct shady_output *ptr; };
struct lua_seat_handle { struct wlr_seat *ptr; };
struct lua_module_handle { const struct shady_module *ptr; };

static void push_window(lua_State *L,struct shady_toplevel *t);
static void push_output(lua_State *L,struct shady_output *o);
static void push_seat(lua_State *L,struct wlr_seat *seat);
static void push_module(lua_State *L,const struct shady_module *module);
#define SHADY_LUA_MAX_BINDS 32
#define SHADY_LUA_MAX_RULES 64
#define SHADY_LUA_RULE_TEXT_MAX 128
struct lua_bind { xkb_keysym_t sym; uint32_t modifiers; int ref; };
static struct lua_bind lua_binds[SHADY_LUA_MAX_BINDS]; static size_t lua_bind_count;

struct lua_window_rule {
	char app_id[SHADY_LUA_RULE_TEXT_MAX];
	char title[SHADY_LUA_RULE_TEXT_MAX];
	char workspace[SHADY_WORKSPACE_NAME_MAX];
	bool has_app_id;
	bool has_title;
	bool has_workspace;
	bool has_maximized;
	bool maximized;
	bool has_fullscreen;
	bool fullscreen;
};
static struct lua_window_rule lua_rules[SHADY_LUA_MAX_RULES];
static size_t lua_rule_count;

static uint32_t parse_mod(const char *s) {
	if (!strcmp(s, "Alt")) return WLR_MODIFIER_ALT;
	if (!strcmp(s, "Shift")) return WLR_MODIFIER_SHIFT;
	if (!strcmp(s, "Ctrl") || !strcmp(s, "Control")) return WLR_MODIFIER_CTRL;
	if (!strcmp(s, "Super") || !strcmp(s, "Logo")) return WLR_MODIFIER_LOGO;
	return 0;
}
static bool parse_lua_bind(const char *spec,xkb_keysym_t *sym,uint32_t *mods){
	char buf[128];if(strlen(spec)>=sizeof(buf))return false;strcpy(buf,spec);*mods=0;char *save=NULL,*tok=strtok_r(buf,"+",&save),*key=NULL;
	while(tok){uint32_t m=parse_mod(tok);if(m)*mods|=m;else{if(key)return false;key=tok;}tok=strtok_r(NULL,"+",&save);}
	if (!key) return false;
	*sym = xkb_keysym_from_name(key, XKB_KEYSYM_CASE_INSENSITIVE);
	return *sym != XKB_KEY_NoSymbol;
}
static int l_shady_bind(lua_State *L){
	const char *spec=luaL_checkstring(L,1);luaL_checktype(L,2,LUA_TFUNCTION);
	if(lua_bind_count>=SHADY_LUA_MAX_BINDS)return luaL_error(L,"too many Lua key bindings");
	struct lua_bind *b=&lua_binds[lua_bind_count];if(!parse_lua_bind(spec,&b->sym,&b->modifiers))return luaL_error(L,"invalid key binding: %s",spec);
	lua_pushvalue(L,2);b->ref=luaL_ref(L,LUA_REGISTRYINDEX);lua_bind_count++;return 0;
}

static bool rule_string_field(lua_State *L, int table_index, const char *key,
		char *dst, size_t dst_size, bool *present) {
	lua_getfield(L, table_index, key);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		*present = false;
		return true;
	}
	if (!lua_isstring(L, -1)) {
		lua_pop(L, 1);
		return false;
	}
	const char *value = lua_tostring(L, -1);
	if (!value || !*value || strlen(value) >= dst_size) {
		lua_pop(L, 1);
		return false;
	}
	snprintf(dst, dst_size, "%s", value);
	*present = true;
	lua_pop(L, 1);
	return true;
}

static bool rule_bool_field(lua_State *L, int table_index, const char *key,
		bool *value, bool *present) {
	lua_getfield(L, table_index, key);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		*present = false;
		return true;
	}
	if (!lua_isboolean(L, -1)) {
		lua_pop(L, 1);
		return false;
	}
	*value = lua_toboolean(L, -1);
	*present = true;
	lua_pop(L, 1);
	return true;
}

static int l_shady_rule(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	if (lua_rule_count >= SHADY_LUA_MAX_RULES)
		return luaL_error(L, "too many Shady window rules");
	struct lua_window_rule rule = {0};
	if (!rule_string_field(L, 1, "app_id", rule.app_id, sizeof(rule.app_id), &rule.has_app_id))
		return luaL_error(L, "rule.app_id must be a non-empty string");
	if (!rule_string_field(L, 1, "title", rule.title, sizeof(rule.title), &rule.has_title))
		return luaL_error(L, "rule.title must be a non-empty string");
	if (!rule_string_field(L, 1, "workspace", rule.workspace, sizeof(rule.workspace), &rule.has_workspace))
		return luaL_error(L, "rule.workspace must be a non-empty string");
	if (!rule_bool_field(L, 1, "maximized", &rule.maximized, &rule.has_maximized))
		return luaL_error(L, "rule.maximized must be boolean");
	if (!rule_bool_field(L, 1, "fullscreen", &rule.fullscreen, &rule.has_fullscreen))
		return luaL_error(L, "rule.fullscreen must be boolean");
	if (!rule.has_app_id && !rule.has_title)
		return luaL_error(L, "rule requires app_id and/or title match");
	if (!rule.has_workspace && !rule.has_maximized && !rule.has_fullscreen)
		return luaL_error(L, "rule requires workspace, maximized, or fullscreen action");
	lua_rules[lua_rule_count++] = rule;
	lua_pushinteger(L, (lua_Integer)lua_rule_count);
	return 1;
}
void shady_lua_apply_window_rules(struct shady_toplevel *toplevel) {
	if (!toplevel || !toplevel->xdg_toplevel) return;
	const char *app_id = toplevel->xdg_toplevel->app_id ? toplevel->xdg_toplevel->app_id : "";
	const char *title = toplevel->xdg_toplevel->title ? toplevel->xdg_toplevel->title : "";
	for (size_t i = 0; i < lua_rule_count; i++) {
		const struct lua_window_rule *rule = &lua_rules[i];
		if (rule->has_app_id && strcmp(rule->app_id, app_id) != 0) continue;
		if (rule->has_title && strcmp(rule->title, title) != 0) continue;
		if (rule->has_workspace)
			(void)shady_workspace_move_toplevel(toplevel, rule->workspace);
		if (rule->has_fullscreen)
			shady_toplevel_set_fullscreen(toplevel, rule->fullscreen);
		else if (rule->has_maximized)
			shady_toplevel_set_maximized(toplevel, rule->maximized);
		wlr_log(WLR_INFO, "[SHADY LUA] rule %zu matched %s / %s",
			i + 1, app_id, title);
		return;
	}
}

static struct shady_spatial_state *lua_spatial(lua_State *L) {
	struct shady_spatial_state *state = shady_spatial_state(lua_server);
	if (!state) {
		luaL_error(L, "spatial module is not available in this build/session");
		return NULL;
	}
	return state;
}

static int l_shady_camera(lua_State *L){
	const char *key=luaL_checkstring(L,1);float v=(float)luaL_checknumber(L,2);struct shady_camera *c=&lua_spatial(L)->runtime.camera;
	if(!strcmp(key,"yaw"))c->yaw=v;else if(!strcmp(key,"pitch"))c->pitch=v;else if(!strcmp(key,"distance"))c->distance=v;
	else if(!strcmp(key,"target_x"))c->target_x=v;else if(!strcmp(key,"target_y"))c->target_y=v;else if(!strcmp(key,"target_z"))c->target_z=v;
	else return luaL_error(L, "unknown camera property: %s", key);
	return 0;
}
static void lua_quit_idle(void *data){wl_display_terminate(data);}
static int l_shady_quit(lua_State *L){(void)L;if(lua_server->wl_display){struct wl_event_loop*loop=wl_display_get_event_loop(lua_server->wl_display);if(!loop||!wl_event_loop_add_idle(loop,lua_quit_idle,lua_server->wl_display))wl_display_terminate(lua_server->wl_display);}return 0;}
/* "#RGB", "#RRGGBB" or "#RRGGBBAA" -> premultiplied RGBA. */
static bool layer_effect_color(const char *text, float out[4]) {
	if (!text || text[0] != '#') return false;
	size_t len = strlen(text + 1);
	unsigned v[4] = {0, 0, 0, 255};
	if (len == 6 || len == 8) {
		for (size_t i = 0; i < len / 2; i++)
			if (sscanf(text + 1 + 2 * i, "%2x", &v[i]) != 1) return false;
	} else if (len == 3) {
		for (size_t i = 0; i < 3; i++) {
			if (sscanf(text + 1 + i, "%1x", &v[i]) != 1) return false;
			v[i] *= 17;
		}
	} else {
		return false;
	}
	float a = (float)v[3] / 255.f;
	for (int i = 0; i < 3; i++) out[i] = (float)v[i] / 255.f * a;
	out[3] = a;
	return true;
}

/* shady.layer_effect(namespace, {blur=, saturation=, tint=, shader=, uniforms=})
 * gives layer surfaces with that namespace a backdrop effect; nil removes it.
 * Spatial mode only. Returns true when the effect is in place. */
static int l_shady_layer_effect(lua_State *L) {
	const char *name_space = luaL_checkstring(L, 1);
	if (lua_isnoneornil(L, 2)) {
		lua_pushboolean(L, shady_render_set_layer_effect(lua_server, name_space, NULL));
		return 1;
	}
	luaL_checktype(L, 2, LUA_TTABLE);
	struct shady_layer_effect_desc desc = { .blur = 12.f, .saturation = 1.f };
	lua_getfield(L, 2, "blur");
	if (!lua_isnil(L, -1)) desc.blur = (float)luaL_checknumber(L, -1);
	lua_getfield(L, 2, "saturation");
	if (!lua_isnil(L, -1)) desc.saturation = (float)luaL_checknumber(L, -1);
	lua_getfield(L, 2, "tint");
	if (!lua_isnil(L, -1) && !layer_effect_color(luaL_checkstring(L, -1), desc.tint))
		return luaL_error(L, "layer_effect tint must be #RGB, #RRGGBB or #RRGGBBAA");
	lua_getfield(L, 2, "shader");
	desc.shader = lua_isnil(L, -1) ? NULL : luaL_checkstring(L, -1);
	lua_getfield(L, 2, "uniforms");
	if (lua_istable(L, -1)) {
		int t = lua_gettop(L);
		lua_pushnil(L);
		while (lua_next(L, t)) {
			if (desc.uniform_count == SHADY_LAYER_EFFECT_UNIFORMS || lua_type(L, -2) != LUA_TSTRING)
				return luaL_error(L, "layer_effect uniforms: at most %d named values",
					SHADY_LAYER_EFFECT_UNIFORMS);
			struct shady_layer_effect_uniform *u = &desc.uniforms[desc.uniform_count];
			snprintf(u->name, sizeof(u->name), "u_%s", lua_tostring(L, -2));
			if (lua_type(L, -1) == LUA_TNUMBER) {
				u->size = 1;
				u->value[0] = (float)lua_tonumber(L, -1);
			} else if (lua_type(L, -1) == LUA_TSTRING && layer_effect_color(lua_tostring(L, -1), u->value)) {
				u->size = 4;
			} else if (lua_istable(L, -1) && lua_rawlen(L, -1) >= 1 && lua_rawlen(L, -1) <= 4) {
				u->size = (int)lua_rawlen(L, -1);
				for (int i = 0; i < u->size; i++) {
					lua_rawgeti(L, -1, i + 1);
					u->value[i] = (float)lua_tonumber(L, -1);
					lua_pop(L, 1);
				}
			} else {
				return luaL_error(L, "layer_effect uniform %s must be a number, colour or 1-4 numbers",
					lua_tostring(L, -2));
			}
			desc.uniform_count++;
			lua_pop(L, 1);
		}
	}
	lua_pushboolean(L, shady_render_set_layer_effect(lua_server, name_space, &desc));
	return 1;
}

static int l_shady_toggle_launcher(lua_State *L){(void)L;shady_shell_protocol_toggle_launcher(lua_server);return 0;}
static int l_shady_spawn(lua_State *L){
	const char *command=luaL_checkstring(L,1);
	pid_t pid=fork();
	if(pid<0){lua_pushboolean(L,0);return 1;}
	if(pid==0){
		setsid();
		execl("/bin/sh","sh","-lc",command,(char*)NULL);
		_exit(127);
	}
	lua_pushboolean(L,1);return 1;
}
static int l_shady_toggle_gravity(lua_State *L){lua_spatial(L);shady_physics_toggle_gravity(lua_server);return 0;}
static bool lua_window_live(struct shady_toplevel *needle){struct shady_toplevel*t;wl_list_for_each(t,&lua_server->toplevels,link)if(t==needle)return true;return false;}
static bool lua_output_live(struct shady_output *needle){struct shady_output*o;wl_list_for_each(o,&lua_server->outputs,link)if(o==needle)return true;return false;}
static bool lua_module_registered(const struct shady_module *needle,size_t *index){for(size_t i=0;i<lua_server->modules.count;i++)if(lua_server->modules.modules[i]==needle){if(index)*index=i;return true;}return false;}

static int l_window_focus(lua_State *L){struct lua_window_handle*h=luaL_checkudata(L,1,SHADY_LUA_WINDOW_MT);if(!h->ptr||!lua_window_live(h->ptr)||!h->ptr->scene_tree||!h->ptr->scene_tree->node.enabled){lua_pushboolean(L,0);return 1;}focus_toplevel(h->ptr);lua_pushboolean(L,1);return 1;}
static int l_window_close(lua_State *L){struct lua_window_handle*h=luaL_checkudata(L,1,SHADY_LUA_WINDOW_MT);if(!h->ptr||!lua_window_live(h->ptr)||!h->ptr->xdg_toplevel){lua_pushboolean(L,0);return 1;}shady_close_animation_begin_window(lua_server,h->ptr);lua_pushboolean(L,1);return 1;}
static int l_window_maximize(lua_State *L){struct lua_window_handle*h=luaL_checkudata(L,1,SHADY_LUA_WINDOW_MT);if(!h->ptr||!lua_window_live(h->ptr)){lua_pushboolean(L,0);return 1;}bool enabled=lua_gettop(L)>=2?lua_toboolean(L,2):!h->ptr->maximized;shady_toplevel_set_maximized(h->ptr,enabled);lua_pushboolean(L,1);return 1;}
static int l_window_fullscreen(lua_State *L){struct lua_window_handle*h=luaL_checkudata(L,1,SHADY_LUA_WINDOW_MT);if(!h->ptr||!lua_window_live(h->ptr)){lua_pushboolean(L,0);return 1;}bool enabled=lua_gettop(L)>=2?lua_toboolean(L,2):!h->ptr->fullscreen;shady_toplevel_set_fullscreen(h->ptr,enabled);lua_pushboolean(L,1);return 1;}
static int l_window_move_workspace(lua_State *L){struct lua_window_handle*h=luaL_checkudata(L,1,SHADY_LUA_WINDOW_MT);const char*name=luaL_checkstring(L,2);lua_pushboolean(L,h->ptr&&lua_window_live(h->ptr)&&shady_workspace_move_toplevel(h->ptr,name));return 1;}
static int l_window_index(lua_State *L){struct lua_window_handle*h=luaL_checkudata(L,1,SHADY_LUA_WINDOW_MT);const char*k=luaL_checkstring(L,2);if(!h->ptr||!lua_window_live(h->ptr)){lua_pushnil(L);return 1;}struct shady_toplevel*t=h->ptr;if(!strcmp(k,"title")){lua_pushstring(L,t->xdg_toplevel->title?t->xdg_toplevel->title:"");return 1;}if(!strcmp(k,"app_id")){lua_pushstring(L,t->xdg_toplevel->app_id?t->xdg_toplevel->app_id:"");return 1;}if(!strcmp(k,"mapped")){lua_pushboolean(L,t->xdg_toplevel->base->surface->mapped);return 1;}if(!strcmp(k,"x")){lua_pushinteger(L,t->scene_tree?t->scene_tree->node.x:0);return 1;}if(!strcmp(k,"y")){lua_pushinteger(L,t->scene_tree?t->scene_tree->node.y:0);return 1;}if(!strcmp(k,"width")){lua_pushinteger(L,t->xdg_toplevel->base->surface->current.width);return 1;}if(!strcmp(k,"height")){lua_pushinteger(L,t->xdg_toplevel->base->surface->current.height);return 1;}if(!strcmp(k,"visible")){lua_pushboolean(L,t->scene_tree&&t->scene_tree->node.enabled);return 1;}if(!strcmp(k,"workspace")){lua_pushstring(L,shady_workspace_toplevel_name(t));return 1;}if(!strcmp(k,"maximized")){lua_pushboolean(L,t->maximized);return 1;}if(!strcmp(k,"fullscreen")){lua_pushboolean(L,t->fullscreen);return 1;}if(!strcmp(k,"z")){const struct shady_toplevel_experimental_state*s=shady_spatial_toplevel_state_const(t);if(s)lua_pushnumber(L,s->z);else lua_pushnil(L);return 1;}if(!strcmp(k,"focus")){lua_pushcfunction(L,l_window_focus);return 1;}if(!strcmp(k,"close")){lua_pushcfunction(L,l_window_close);return 1;}if(!strcmp(k,"maximize")){lua_pushcfunction(L,l_window_maximize);return 1;}if(!strcmp(k,"set_fullscreen")){lua_pushcfunction(L,l_window_fullscreen);return 1;}if(!strcmp(k,"move_to_workspace")){lua_pushcfunction(L,l_window_move_workspace);return 1;}lua_pushnil(L);return 1;}
static int l_window_tostring(lua_State *L){struct lua_window_handle*h=luaL_checkudata(L,1,SHADY_LUA_WINDOW_MT);if(!h->ptr||!lua_window_live(h->ptr)){lua_pushliteral(L,"Window<dead>");return 1;}lua_pushfstring(L,"Window<%s>",h->ptr->xdg_toplevel->app_id?h->ptr->xdg_toplevel->app_id:"");return 1;}

static int l_output_index(lua_State *L){struct lua_output_handle*h=luaL_checkudata(L,1,SHADY_LUA_OUTPUT_MT);const char*k=luaL_checkstring(L,2);if(!h->ptr||!lua_output_live(h->ptr)){lua_pushnil(L);return 1;}struct wlr_output*o=h->ptr->wlr_output;if(!strcmp(k,"name")){lua_pushstring(L,o->name?o->name:"");return 1;}if(!strcmp(k,"scale")){lua_pushnumber(L,o->scale);return 1;}if(!strcmp(k,"width")||!strcmp(k,"height")){int w=0,hg=0;wlr_output_effective_resolution(o,&w,&hg);lua_pushinteger(L,!strcmp(k,"width")?w:hg);return 1;}lua_pushnil(L);return 1;}
static int l_output_tostring(lua_State *L){struct lua_output_handle*h=luaL_checkudata(L,1,SHADY_LUA_OUTPUT_MT);lua_pushfstring(L,"Output<%s>",h->ptr&&lua_output_live(h->ptr)&&h->ptr->wlr_output->name?h->ptr->wlr_output->name:"dead");return 1;}

static int l_seat_index(lua_State *L){struct lua_seat_handle*h=luaL_checkudata(L,1,SHADY_LUA_SEAT_MT);const char*k=luaL_checkstring(L,2);if(!strcmp(k,"name")){lua_pushstring(L,h->ptr&&h->ptr->name?h->ptr->name:"");return 1;}lua_pushnil(L);return 1;}
static int l_seat_tostring(lua_State *L){struct lua_seat_handle*h=luaL_checkudata(L,1,SHADY_LUA_SEAT_MT);lua_pushfstring(L,"Seat<%s>",h->ptr&&h->ptr->name?h->ptr->name:"");return 1;}

static int l_module_index(lua_State *L){struct lua_module_handle*h=luaL_checkudata(L,1,SHADY_LUA_MODULE_MT);const char*k=luaL_checkstring(L,2);size_t i=0;if(!h->ptr||!lua_module_registered(h->ptr,&i)){lua_pushnil(L);return 1;}if(!strcmp(k,"name")){lua_pushstring(L,h->ptr->name?h->ptr->name:"");return 1;}if(!strcmp(k,"active")){lua_pushboolean(L,lua_server->modules.active[i]);return 1;}lua_pushnil(L);return 1;}
static int l_module_tostring(lua_State *L){struct lua_module_handle*h=luaL_checkudata(L,1,SHADY_LUA_MODULE_MT);if(!h->ptr||!lua_module_registered(h->ptr,NULL)){lua_pushliteral(L,"Module<dead>");return 1;}lua_pushfstring(L,"Module<%s>",h->ptr->name?h->ptr->name:"");return 1;}

static void register_object_types(lua_State *L){struct{const char*name;lua_CFunction index;lua_CFunction tostring;}mts[]={{SHADY_LUA_WINDOW_MT,l_window_index,l_window_tostring},{SHADY_LUA_OUTPUT_MT,l_output_index,l_output_tostring},{SHADY_LUA_SEAT_MT,l_seat_index,l_seat_tostring},{SHADY_LUA_MODULE_MT,l_module_index,l_module_tostring}};for(size_t i=0;i<sizeof(mts)/sizeof(mts[0]);i++){luaL_newmetatable(L,mts[i].name);lua_pushcfunction(L,mts[i].index);lua_setfield(L,-2,"__index");lua_pushcfunction(L,mts[i].tostring);lua_setfield(L,-2,"__tostring");lua_pop(L,1);}}

static void push_window(lua_State *L,struct shady_toplevel*t){struct lua_window_handle*h=lua_newuserdatauv(L,sizeof(*h),0);h->ptr=t;luaL_setmetatable(L,SHADY_LUA_WINDOW_MT);}
static void push_output(lua_State *L,struct shady_output*o){struct lua_output_handle*h=lua_newuserdatauv(L,sizeof(*h),0);h->ptr=o;luaL_setmetatable(L,SHADY_LUA_OUTPUT_MT);}
static void push_seat(lua_State *L,struct wlr_seat*seat){struct lua_seat_handle*h=lua_newuserdatauv(L,sizeof(*h),0);h->ptr=seat;luaL_setmetatable(L,SHADY_LUA_SEAT_MT);}
static void push_module(lua_State *L,const struct shady_module*m){struct lua_module_handle*h=lua_newuserdatauv(L,sizeof(*h),0);h->ptr=m;luaL_setmetatable(L,SHADY_LUA_MODULE_MT);}

static int l_shady_windows(lua_State *L){lua_newtable(L);int n=1;struct shady_toplevel*t;wl_list_for_each(t,&lua_server->toplevels,link){push_window(L,t);lua_rawseti(L,-2,n++);}return 1;}
static int l_shady_focused_window(lua_State *L){struct wlr_surface*surface=lua_server->seat->keyboard_state.focused_surface;if(surface){struct wlr_surface*root=wlr_surface_get_root_surface(surface);struct wlr_xdg_toplevel*xdg=wlr_xdg_toplevel_try_from_wlr_surface(root);if(xdg){struct shady_toplevel*t;wl_list_for_each(t,&lua_server->all_toplevels,all_link){if(t->xdg_toplevel==xdg){push_window(L,t);return 1;}}}}struct shady_toplevel*t;wl_list_for_each(t,&lua_server->toplevels,link){if(t->mapped&&t->scene_tree&&t->scene_tree->node.enabled){push_window(L,t);return 1;}}lua_pushnil(L);return 1;}
static int l_shady_outputs(lua_State *L){lua_newtable(L);int n=1;struct shady_output*o;wl_list_for_each(o,&lua_server->outputs,link){push_output(L,o);lua_rawseti(L,-2,n++);}return 1;}
static int l_shady_seat(lua_State *L){push_seat(L,lua_server->seat);return 1;}
static int l_shady_modules_runtime(lua_State *L){lua_newtable(L);for(size_t i=0;i<lua_server->modules.count;i++){push_module(L,lua_server->modules.modules[i]);lua_rawseti(L,-2,(lua_Integer)i+1);}return 1;}
static int l_shady_workspaces(lua_State *L){lua_newtable(L);size_t n=shady_workspace_count(lua_server);for(size_t i=0;i<n;i++){lua_pushstring(L,shady_workspace_name_at(lua_server,i));lua_rawseti(L,-2,(lua_Integer)i+1);}return 1;}
static int l_shady_current_workspace(lua_State *L){lua_pushstring(L,shady_workspace_current_name(lua_server));return 1;}
static int l_shady_workspace(lua_State *L){const char*name=luaL_checkstring(L,1);lua_pushboolean(L,shady_workspace_switch(lua_server,name));return 1;}
static int l_shady_expand_all(lua_State *L){lua_spatial(L);struct shady_fps_state*fps=shady_fps_state_for(lua_server);struct shady_toplevel*t;wl_list_for_each(t,&lua_server->toplevels,link){shady_fps_toplevel_state(t)->expanded=true;shady_physics_stop(t);}fps->expanded_toplevel=NULL;fps->input_capture=false;shady_render_schedule_all_outputs(lua_server);return 0;}
static int l_shady_fold_all(lua_State *L){struct shady_spatial_state *spatial=lua_spatial(L);struct shady_fps_state*fps=shady_fps_state_for(lua_server);struct shady_toplevel*t;wl_list_for_each(t,&lua_server->toplevels,link)shady_fps_toplevel_state(t)->expanded=false;fps->expanded_toplevel=NULL;fps->input_capture=spatial->runtime.camera.first_person;shady_render_schedule_all_outputs(lua_server);return 0;}
static int l_shady_respawn_all(lua_State *L){lua_spatial(L);shady_physics_respawn_all(lua_server);return 0;}
static int l_shady_toggle_fps(lua_State *L){lua_spatial(L);shady_fps_toggle(lua_server);return 0;}

static int l_shady_config(lua_State *L){
	const char *key=luaL_checkstring(L,1),*value;
	char boolean[6];
	if(lua_isboolean(L,2)){snprintf(boolean,sizeof(boolean),"%s",lua_toboolean(L,2)?"true":"false");value=boolean;}
	else value=luaL_checkstring(L,2);
	if(!shady_config_set(&lua_server->config,key,value))
		return luaL_error(L,"invalid Shady config: %s = %s",key,value);
	if(lua_server->renderer)shady_render_schedule_all_outputs(lua_server);
	return 0;
}
static int l_shady_log(lua_State *L){
	const char *message=luaL_checkstring(L,1);
	wlr_log(WLR_INFO,"[SHADY LUA] 🌙 %s",message);
	return 0;
}
static bool lua_event_type(const char *name, enum shady_event_type *type){for(int i=SHADY_EVENT_WINDOW_CREATED;i<SHADY_EVENT_COUNT;i++){if(!strcmp(name,shady_event_name((enum shady_event_type)i))){*type=(enum shady_event_type)i;return true;}}return false;}
static int l_shady_on(lua_State *L){const char*name=luaL_checkstring(L,1);luaL_checktype(L,2,LUA_TFUNCTION);enum shady_event_type type;if(!lua_event_type(name,&type))return luaL_error(L,"unknown Shady event: %s",name);lua_getglobal(L,"shady_event_handlers");if(!lua_istable(L,-1)){lua_pop(L,1);lua_newtable(L);lua_pushvalue(L,-1);lua_setglobal(L,"shady_event_handlers");}lua_getfield(L,-1,name);if(!lua_istable(L,-1)){lua_pop(L,1);lua_newtable(L);lua_pushvalue(L,-1);lua_setfield(L,-3,name);}lua_Integer n=(lua_Integer)lua_rawlen(L,-1);uint64_t id=lua_next_handler_id++;if(id==0)id=lua_next_handler_id++;lua_newtable(L);lua_pushinteger(L,(lua_Integer)id);lua_setfield(L,-2,"id");lua_pushvalue(L,2);lua_setfield(L,-2,"fn");lua_rawseti(L,-2,n+1);lua_pop(L,2);lua_pushinteger(L,(lua_Integer)id);return 1;}
static int l_shady_off(lua_State *L){uint64_t id=(uint64_t)luaL_checkinteger(L,1);lua_getglobal(L,"shady_event_handlers");if(!lua_istable(L,-1)){lua_pop(L,1);lua_pushboolean(L,0);return 1;}lua_pushnil(L);while(lua_next(L,-2)!=0){if(lua_istable(L,-1)){size_t n=lua_rawlen(L,-1);for(size_t i=1;i<=n;i++){lua_rawgeti(L,-1,(lua_Integer)i);if(lua_istable(L,-1)){lua_getfield(L,-1,"id");uint64_t current=(uint64_t)lua_tointeger(L,-1);lua_pop(L,1);if(current==id){lua_pop(L,1);for(size_t j=i;j<n;j++){lua_rawgeti(L,-1,(lua_Integer)j+1);lua_rawseti(L,-2,(lua_Integer)j);}lua_pushnil(L);lua_rawseti(L,-2,(lua_Integer)n);lua_pop(L,2);lua_pushboolean(L,1);return 1;}}lua_pop(L,1);}}lua_pop(L,1);}lua_pop(L,1);lua_pushboolean(L,0);return 1;}
static int l_shady_reload_plugin(lua_State *L){const char*name=luaL_checkstring(L,1);lua_pushboolean(L,shady_plugin_reload(lua_server,shady_plugin_manager_module_name(lua_server,name)));return 1;}
static int l_shady_unload_plugin(lua_State *L){const char*name=luaL_checkstring(L,1);lua_pushboolean(L,shady_plugin_unload(lua_server,shady_plugin_manager_module_name(lua_server,name)));return 1;}
static int l_shady_has_capability(lua_State *L){
	const char *capability=luaL_checkstring(L,1);
	lua_pushboolean(L,shady_module_has_capability(lua_server,capability));
	return 1;
}

static int lua_timer_fire(void *data) {
	struct lua_timer *timer = data;
	wl_list_remove(&timer->link);
	if (timer->source) wl_event_source_remove(timer->source);
	lua_rawgeti(timer->L, LUA_REGISTRYINDEX, timer->callback_ref);
	luaL_unref(timer->L, LUA_REGISTRYINDEX, timer->callback_ref);
	if (lua_pcall(timer->L, 0, 0, 0) != LUA_OK) {
		wlr_log(WLR_ERROR, "[SHADY LUA] automation timer: %s",
			lua_tostring(timer->L, -1));
		lua_pop(timer->L, 1);
	}
	free(timer);
	return 0;
}

static int l_automation_after(lua_State *L) {
	int ms = (int)luaL_checkinteger(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);
	if (ms < 0) return luaL_error(L, "delay must be >= 0");
	struct lua_timer *timer = calloc(1, sizeof(*timer));
	if (!timer) return luaL_error(L, "out of memory");
	timer->L = L;
	lua_pushvalue(L, 2);
	timer->callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	timer->source = wl_event_loop_add_timer(
		wl_display_get_event_loop(lua_server->wl_display), lua_timer_fire, timer);
	if (!timer->source) {
		luaL_unref(L, LUA_REGISTRYINDEX, timer->callback_ref);
		free(timer);
		return luaL_error(L, "failed to create timer");
	}
	wl_list_insert(&lua_timers, &timer->link);
	wl_event_source_timer_update(timer->source, ms);
	return 0;
}

static int l_automation_key(lua_State *L) {
	const char *spec = luaL_checkstring(L, 1);
	xkb_keysym_t sym;
	uint32_t mods;
	if (!parse_lua_bind(spec, &sym, &mods))
		return luaL_error(L, "invalid shortcut: %s", spec);
	bool press = shady_input_automation_key(lua_server, sym, mods, true);
	bool release = shady_input_automation_key(lua_server, sym, mods, false);
	lua_pushboolean(L, press || release);
	return 1;
}

static pid_t automation_spawn_wtype(const char *option, const char *value) {
	pid_t pid = fork();
	if (pid != 0) return pid;
	if (option) execlp("wtype", "wtype", option, value, (char *)NULL);
	else execlp("wtype", "wtype", value, (char *)NULL);
	_exit(127);
}

static int l_automation_type_text(lua_State *L) {
	const char *text = luaL_checkstring(L, 1);
	pid_t pid = automation_spawn_wtype(NULL, text);
	if (pid < 0) { lua_pushnil(L); lua_pushstring(L, "fork failed"); return 2; }
	lua_pushinteger(L, (lua_Integer)pid);
	return 1;
}

static int l_automation_key_state(lua_State *L, bool pressed) {
	const char *spec = luaL_checkstring(L, 1);
	xkb_keysym_t sym;
	uint32_t mods;
	if (!parse_lua_bind(spec, &sym, &mods))
		return luaL_error(L, "invalid shortcut: %s", spec);
	lua_pushboolean(L,
		shady_input_automation_key(lua_server, sym, mods, pressed));
	return 1;
}

static int l_automation_key_down(lua_State *L) {
	return l_automation_key_state(L, true);
}

static int l_automation_key_up(lua_State *L) {
	return l_automation_key_state(L, false);
}

static int l_automation_move_pointer(lua_State *L) {
	double x = luaL_checknumber(L, 1);
	double y = luaL_checknumber(L, 2);
	shady_input_automation_pointer_move(lua_server, x, y);
	return 0;
}

static uint32_t automation_button(lua_State *L, int index) {
	if (lua_isinteger(L, index)) return (uint32_t)lua_tointeger(L, index);
	const char *name = luaL_checkstring(L, index);
	if (!strcmp(name, "left")) return BTN_LEFT;
	if (!strcmp(name, "right")) return BTN_RIGHT;
	if (!strcmp(name, "middle")) return BTN_MIDDLE;
	luaL_error(L, "unknown mouse button: %s", name);
	return 0;
}

static int l_automation_click(lua_State *L) {
	uint32_t button = automation_button(L, 1);
	shady_input_automation_pointer_button(lua_server, button, true);
	shady_input_automation_pointer_button(lua_server, button, false);
	return 0;
}

static int l_automation_drag(lua_State *L) {
	double x1 = luaL_checknumber(L, 1);
	double y1 = luaL_checknumber(L, 2);
	double x2 = luaL_checknumber(L, 3);
	double y2 = luaL_checknumber(L, 4);
	uint32_t button = lua_gettop(L) >= 5 ? automation_button(L, 5) : BTN_LEFT;
	int steps = lua_gettop(L) >= 6 ? (int)luaL_checkinteger(L, 6) : 8;
	if (steps < 1 || steps > 256) return luaL_error(L, "drag steps must be 1..256");

	shady_input_automation_pointer_move(lua_server, x1, y1);
	shady_input_automation_pointer_button(lua_server, button, true);
	for (int i = 1; i <= steps; i++) {
		double t = (double)i / (double)steps;
		shady_input_automation_pointer_move(lua_server,
			x1 + (x2 - x1) * t, y1 + (y2 - y1) * t);
	}
	shady_input_automation_pointer_button(lua_server, button, false);
	return 0;
}

static int l_automation_scroll(lua_State *L) {
	double vertical = luaL_optnumber(L, 1, 0.0);
	double horizontal = luaL_optnumber(L, 2, 0.0);
	if (vertical != 0.0)
		shady_input_automation_pointer_axis(lua_server,
			WL_POINTER_AXIS_VERTICAL_SCROLL, vertical, (int32_t)vertical);
	if (horizontal != 0.0)
		shady_input_automation_pointer_axis(lua_server,
			WL_POINTER_AXIS_HORIZONTAL_SCROLL, horizontal, (int32_t)horizontal);
	return 0;
}

static int lua_child_watch_ready(int fd, uint32_t mask, void *data) {
	struct lua_child_watch *watch = data;
	if (!(mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR))) return 0;
	if (watch->source) wl_event_source_remove(watch->source);
	close(fd);

	int child_status = 0;
	pid_t waited;
	do {
		waited = waitpid(watch->pid, &child_status, 0);
	} while (waited < 0 && errno == EINTR);

	struct stat st;
	bool file_ok = stat(watch->path, &st) == 0 && st.st_size > 0;
	bool child_ok = waited == watch->pid && WIFEXITED(child_status) &&
		WEXITSTATUS(child_status) == 0;
	bool ok = child_ok && file_ok;
	if (!ok) {
		if (waited != watch->pid) {
			wlr_log(WLR_ERROR, "screenshot worker wait failed for %s: %s",
				watch->path, strerror(errno));
		} else if (WIFEXITED(child_status)) {
			wlr_log(WLR_ERROR,
				"screenshot worker exited status=%d path=%s file_ok=%d",
				WEXITSTATUS(child_status), watch->path, file_ok);
		} else if (WIFSIGNALED(child_status)) {
			wlr_log(WLR_ERROR,
				"screenshot worker killed by signal=%d path=%s file_ok=%d",
				WTERMSIG(child_status), watch->path, file_ok);
		}
	}
	lua_rawgeti(watch->L, LUA_REGISTRYINDEX, watch->callback_ref);
	luaL_unref(watch->L, LUA_REGISTRYINDEX, watch->callback_ref);
	lua_pushboolean(watch->L, ok);
	lua_pushstring(watch->L, watch->path);
	if (lua_pcall(watch->L, 2, 0, 0) != LUA_OK) {
		wlr_log(WLR_ERROR, "[SHADY LUA] screenshot callback: %s",
			lua_tostring(watch->L, -1));
		lua_pop(watch->L, 1);
	}
	wl_list_remove(&watch->link);
	free(watch);
	return 0;
}

static int l_automation_screenshot(lua_State *L) {
	const char *path = luaL_checkstring(L, 1);
	bool has_callback = lua_gettop(L) >= 2 && !lua_isnil(L, 2);
	if (has_callback) luaL_checktype(L, 2, LUA_TFUNCTION);
	struct shady_output *output = NULL;
	if (!wl_list_empty(&lua_server->outputs))
		output = wl_container_of(lua_server->outputs.next, output, link);
	if (!output || !output->wlr_output) {
		lua_pushnil(L);
		lua_pushstring(L, "no output available");
		return 2;
	}
	struct wlr_box box = {0};
	wlr_output_layout_get_box(lua_server->output_layout, output->wlr_output, &box);
	if (box.width <= 0 || box.height <= 0) {
		lua_pushnil(L);
		lua_pushstring(L, "output has no layout geometry");
		return 2;
	}
	char geometry[128];
	snprintf(geometry, sizeof(geometry), "%d,%d %dx%d",
		box.x, box.y, box.width, box.height);

	int pipefd[2] = {-1, -1};
	if (has_callback && pipe(pipefd) != 0) {
		lua_pushnil(L);
		lua_pushstring(L, "pipe failed");
		return 2;
	}
	shady_render_schedule_all_outputs(lua_server);
	pid_t pid = fork();
	if (pid < 0) {
		if (pipefd[0] >= 0) { close(pipefd[0]); close(pipefd[1]); }
		lua_pushnil(L);
		lua_pushstring(L, "fork failed");
		return 2;
	}
	if (pid == 0) {
		if (!has_callback) {
			execlp("grim", "grim", "-g", geometry, path, (char *)NULL);
			_exit(127);
		}
		close(pipefd[0]);
		pid_t worker = fork();
		if (worker < 0) _exit(127);
		if (worker == 0) {
			close(pipefd[1]);
			execlp("grim", "grim", "-g", geometry, path, (char *)NULL);
			_exit(127);
		}
		int status = 0;
		while (waitpid(worker, &status, 0) < 0) {
			if (errno != EINTR) _exit(127);
		}
		_exit(WIFEXITED(status) ? WEXITSTATUS(status) : 128);
	}

	if (has_callback) {
		close(pipefd[1]);
		fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL) | O_NONBLOCK);
		struct lua_child_watch *watch = calloc(1, sizeof(*watch));
		if (!watch) { close(pipefd[0]); return luaL_error(L, "out of memory"); }
		watch->L = L;
		watch->pid = pid;
		watch->fd = pipefd[0];
		snprintf(watch->path, sizeof(watch->path), "%s", path);
		lua_pushvalue(L, 2);
		watch->callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
		watch->source = wl_event_loop_add_fd(
			wl_display_get_event_loop(lua_server->wl_display), watch->fd,
			WL_EVENT_HANGUP | WL_EVENT_ERROR, lua_child_watch_ready, watch);
		if (!watch->source) {
			luaL_unref(L, LUA_REGISTRYINDEX, watch->callback_ref);
			close(watch->fd);
			free(watch);
			return luaL_error(L, "failed to watch screenshot process");
		}
		wl_list_insert(&lua_child_watches, &watch->link);
	}
	lua_pushinteger(L, (lua_Integer)pid);
	return 1;
}
static void install_api(lua_State *L){
	register_object_types(L);
	lua_newtable(L);
	lua_pushcfunction(L,l_shady_log);lua_setfield(L,-2,"log");
	lua_pushcfunction(L,l_shady_config);lua_setfield(L,-2,"config");
	lua_pushcfunction(L,l_shady_config);lua_setfield(L,-2,"set");
	lua_pushcfunction(L,l_shady_bind);lua_setfield(L,-2,"bind");
	lua_pushcfunction(L,l_shady_rule);lua_setfield(L,-2,"rule");
	lua_pushcfunction(L,l_shady_quit);lua_setfield(L,-2,"quit");
	lua_pushcfunction(L,l_shady_spawn);lua_setfield(L,-2,"spawn");
	lua_pushcfunction(L,l_shady_toggle_launcher);lua_setfield(L,-2,"toggle_launcher");
	lua_pushcfunction(L,l_shady_layer_effect);lua_setfield(L,-2,"layer_effect");
	lua_pushcfunction(L,l_shady_windows);lua_setfield(L,-2,"windows");
	lua_pushcfunction(L,l_shady_focused_window);lua_setfield(L,-2,"focused_window");
	lua_pushcfunction(L,l_shady_outputs);lua_setfield(L,-2,"outputs");
	lua_pushcfunction(L,l_shady_seat);lua_setfield(L,-2,"seat");
	lua_pushcfunction(L,l_shady_modules_runtime);lua_setfield(L,-2,"modules");
	lua_pushcfunction(L,l_shady_workspaces);lua_setfield(L,-2,"workspaces");
	lua_pushcfunction(L,l_shady_current_workspace);lua_setfield(L,-2,"current_workspace");
	lua_pushcfunction(L,l_shady_workspace);lua_setfield(L,-2,"workspace");
	lua_pushcfunction(L,l_shady_has_capability);lua_setfield(L,-2,"has_capability");
	lua_newtable(L);
	lua_pushcfunction(L,l_automation_after);lua_setfield(L,-2,"after");
	lua_pushcfunction(L,l_automation_key);lua_setfield(L,-2,"key");
	lua_pushcfunction(L,l_automation_key_down);lua_setfield(L,-2,"key_down");
	lua_pushcfunction(L,l_automation_key_up);lua_setfield(L,-2,"key_up");
	lua_pushcfunction(L,l_automation_type_text);lua_setfield(L,-2,"type_text");
	lua_pushcfunction(L,l_automation_move_pointer);lua_setfield(L,-2,"move_pointer");
	lua_pushcfunction(L,l_automation_click);lua_setfield(L,-2,"click");
	lua_pushcfunction(L,l_automation_drag);lua_setfield(L,-2,"drag");
	lua_pushcfunction(L,l_automation_scroll);lua_setfield(L,-2,"scroll");
	lua_pushcfunction(L,l_automation_screenshot);lua_setfield(L,-2,"screenshot");
	lua_setfield(L,-2,"automation");
	lua_pushcfunction(L,l_shady_on);lua_setfield(L,-2,"on");
	lua_pushcfunction(L,l_shady_off);lua_setfield(L,-2,"off");
	lua_pushcfunction(L,l_shady_reload_plugin);lua_setfield(L,-2,"reload_plugin");
	lua_pushcfunction(L,l_shady_unload_plugin);lua_setfield(L,-2,"unload_plugin");
	shady_plugin_manager_lua_install(L,lua_server,SHADY_PLUGIN_LUA_RUNTIME);
	if(shady_module_has_capability(lua_server,"spatial")){
		lua_pushcfunction(L,l_shady_camera);lua_setfield(L,-2,"camera");
	}
	if(shady_module_has_capability(lua_server,"spatial.physics")){
		lua_pushcfunction(L,l_shady_toggle_gravity);lua_setfield(L,-2,"toggle_gravity");
		lua_pushcfunction(L,l_shady_respawn_all);lua_setfield(L,-2,"respawn_all");
	}
	if(shady_module_has_capability(lua_server,"spatial.fps")){
		lua_pushcfunction(L,l_shady_toggle_fps);lua_setfield(L,-2,"toggle_fps");
		lua_pushcfunction(L,l_shady_expand_all);lua_setfield(L,-2,"expand_all");
		lua_pushcfunction(L,l_shady_fold_all);lua_setfield(L,-2,"fold_all");
	}
	lua_setglobal(L,"shady");
}
static void lua_event_callback(shady_host host,const struct shady_event *event,void *user_data){(void)host;(void)user_data;lua_State*L=shady_lua_state_for(lua_server)->L;if(!L)return;const char*name=shady_event_name(event->type);lua_getglobal(L,"shady_event_handlers");if(!lua_istable(L,-1)){lua_pop(L,1);return;}lua_getfield(L,-1,name);if(!lua_istable(L,-1)){lua_pop(L,2);return;}size_t n=lua_rawlen(L,-1);for(size_t i=1;i<=n;i++){lua_rawgeti(L,-1,(lua_Integer)i);if(!lua_istable(L,-1)){lua_pop(L,1);continue;}lua_getfield(L,-1,"fn");if(!lua_isfunction(L,-1)){lua_pop(L,2);continue;}switch(event->type){case SHADY_EVENT_WINDOW_CREATED:case SHADY_EVENT_WINDOW_MAPPED:case SHADY_EVENT_WINDOW_UNMAPPED:case SHADY_EVENT_WINDOW_FOCUSED:case SHADY_EVENT_WINDOW_RESIZED:case SHADY_EVENT_WINDOW_STATE_CHANGED:case SHADY_EVENT_WINDOW_TITLE_CHANGED:case SHADY_EVENT_WINDOW_DESTROYED:push_window(L,(struct shady_toplevel*)event->object.window);break;case SHADY_EVENT_OUTPUT_ADDED:case SHADY_EVENT_OUTPUT_REMOVED:push_output(L,(struct shady_output*)event->object.output);break;case SHADY_EVENT_WORKSPACE_CHANGED:lua_pushstring(L,event->object.workspace?event->object.workspace:"");break;case SHADY_EVENT_MODULE_STARTED:case SHADY_EVENT_MODULE_STOPPED:push_module(L,(const struct shady_module*)event->object.module);break;case SHADY_EVENT_COUNT:lua_pushnil(L);break;}if(lua_pcall(L,1,0,0)!=LUA_OK){wlr_log(WLR_ERROR,"[SHADY LUA] event %s: %s",name,lua_tostring(L,-1));lua_pop(L,1);}lua_pop(L,1);}lua_pop(L,2);}
static void default_script_path(char *buf,size_t size){
	const char *override=getenv("SHADY_LUA_INIT");if(override&&*override){snprintf(buf,size,"%s",override);return;}
	const char *xdg=getenv("XDG_CONFIG_HOME"),*home=getenv("HOME");
	if(xdg&&*xdg)snprintf(buf,size,"%s/shady/init.lua",xdg);
	else if(home&&*home)snprintf(buf,size,"%s/.config/shady/init.lua",home);
	else snprintf(buf,size,"init.lua");
}
bool shady_lua_init(struct shady_server *server){
	wl_list_init(&lua_timers);
	wl_list_init(&lua_child_watches);
	lua_State *L=luaL_newstate();if(!L){wlr_log(WLR_ERROR,"[SHADY LUA] failed to create Lua state");return false;}
	shady_lua_state_for(server)->L=L;lua_server=server;luaL_openlibs(L);install_api(L);for(uint32_t type=SHADY_EVENT_WINDOW_CREATED;type<SHADY_EVENT_COUNT;type++){if(!shady_event_subscribe(server,type,lua_event_callback,NULL))return false;}return true;
}
bool shady_lua_start(struct shady_server *server){
	lua_State *L=shady_lua_state_for(server)->L;if(!L)return false;
	char path[4096];default_script_path(path,sizeof(path));
	if(luaL_loadfile(L,path)!=LUA_OK){
		const char *e=lua_tostring(L,-1);
		if(e&&strstr(e,"No such file or directory"))wlr_log(WLR_INFO,"[SHADY LUA] 💤 no init script: %s",path);
		else wlr_log(WLR_ERROR,"[SHADY LUA] load error: %s",e?e:"unknown error");
		lua_pop(L,1);return true;
	}
	if(lua_pcall(L,0,0,0)!=LUA_OK){wlr_log(WLR_ERROR,"[SHADY LUA] runtime error: %s",lua_tostring(L,-1));lua_pop(L,1);return true;}
	wlr_log(WLR_INFO,"[SHADY LUA] ✅ loaded %s (Lua %s)",path,LUA_VERSION);return true;
}
bool shady_lua_handle_key(struct shady_server *server,xkb_keysym_t sym,uint32_t modifiers){
	lua_State *L=shady_lua_state_for(server)->L;uint32_t mask=WLR_MODIFIER_ALT|WLR_MODIFIER_SHIFT|WLR_MODIFIER_CTRL|WLR_MODIFIER_LOGO;
	xkb_keysym_t normalized=xkb_keysym_to_lower(sym);
	for(size_t i=0;L&&i<lua_bind_count;i++)if(xkb_keysym_to_lower(lua_binds[i].sym)==normalized&&lua_binds[i].modifiers==(modifiers&mask)){
		lua_rawgeti(L,LUA_REGISTRYINDEX,lua_binds[i].ref);if(lua_pcall(L,0,0,0)!=LUA_OK){wlr_log(WLR_ERROR,"[SHADY LUA] keybind: %s",lua_tostring(L,-1));lua_pop(L,1);}return true;}return false;
}
void shady_lua_fini(struct shady_server *server){
	lua_State *L=shady_lua_state_for(server)->L;
	struct lua_timer *timer,*tmp;
	wl_list_for_each_safe(timer,tmp,&lua_timers,link){
		wl_list_remove(&timer->link);
		if(timer->source)wl_event_source_remove(timer->source);
		if(L)luaL_unref(L,LUA_REGISTRYINDEX,timer->callback_ref);
		free(timer);
	}
	struct lua_child_watch *watch,*watch_tmp;
	wl_list_for_each_safe(watch,watch_tmp,&lua_child_watches,link){
		wl_list_remove(&watch->link);
		if(watch->source)wl_event_source_remove(watch->source);
		if(watch->fd>=0)close(watch->fd);
		if(L)luaL_unref(L,LUA_REGISTRYINDEX,watch->callback_ref);
		free(watch);
	}
	if(L){lua_close(L);shady_lua_state_for(server)->L=NULL;}lua_bind_count=0;lua_rule_count=0;lua_next_handler_id=1;
}
