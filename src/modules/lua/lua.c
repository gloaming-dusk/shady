#include "lua.h"
#include <stdbool.h>
#include <stdlib.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <wlr/util/log.h>
#include "../../shady.h"
#include "../../render/render.h"
#include "../physics/physics.h"
#include "../fps/fps.h"
#include <string.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_xdg_shell.h>

static struct shady_server *lua_server;
static void push_window(lua_State *L,struct shady_toplevel *t);
#define SHADY_LUA_MAX_BINDS 32
struct lua_bind { xkb_keysym_t sym; uint32_t modifiers; int ref; };
static struct lua_bind lua_binds[SHADY_LUA_MAX_BINDS]; static size_t lua_bind_count;

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
static int l_shady_camera(lua_State *L){
	const char *key=luaL_checkstring(L,1);float v=(float)luaL_checknumber(L,2);struct shady_camera *c=&lua_server->experimental.camera;
	if(!strcmp(key,"yaw"))c->yaw=v;else if(!strcmp(key,"pitch"))c->pitch=v;else if(!strcmp(key,"distance"))c->distance=v;
	else if(!strcmp(key,"target_x"))c->target_x=v;else if(!strcmp(key,"target_y"))c->target_y=v;else if(!strcmp(key,"target_z"))c->target_z=v;
	else return luaL_error(L, "unknown camera property: %s", key);
	return 0;
}
static int l_shady_quit(lua_State *L){(void)L;if(lua_server->wl_display)wl_display_terminate(lua_server->wl_display);return 0;}
static int l_shady_toggle_gravity(lua_State *L){(void)L;shady_physics_toggle_gravity(lua_server);return 0;}
static int l_shady_windows(lua_State *L){
	lua_newtable(L);int n=1;struct shady_toplevel*t;
	wl_list_for_each(t,&lua_server->toplevels,link){push_window(L,t);lua_rawseti(L,-2,n++);}return 1;
}
static int l_shady_expand_all(lua_State *L){(void)L;struct shady_toplevel*t;wl_list_for_each(t,&lua_server->toplevels,link){t->experimental.fps_expanded=true;shady_physics_stop(t);}lua_server->experimental.fps.expanded_toplevel=NULL;lua_server->experimental.fps.input_capture=false;shady_render_schedule_all_outputs(lua_server);return 0;}
static int l_shady_fold_all(lua_State *L){(void)L;struct shady_toplevel*t;wl_list_for_each(t,&lua_server->toplevels,link)t->experimental.fps_expanded=false;lua_server->experimental.fps.expanded_toplevel=NULL;lua_server->experimental.fps.input_capture=lua_server->experimental.camera.first_person;shady_render_schedule_all_outputs(lua_server);return 0;}
static int l_shady_respawn_all(lua_State *L){(void)L;shady_physics_respawn_all(lua_server);return 0;}
static int l_shady_toggle_fps(lua_State *L){(void)L;shady_fps_toggle(lua_server);return 0;}

static int l_shady_config(lua_State *L){
	const char *key=luaL_checkstring(L,1),*value;
	char boolean[6];
	if(lua_isboolean(L,2)){snprintf(boolean,sizeof(boolean),"%s",lua_toboolean(L,2)?"true":"false");value=boolean;}
	else value=luaL_checkstring(L,2);
	if(!shady_config_set(&lua_server->config,key,value))
		return luaL_error(L,"invalid Shady config: %s = %s",key,value);
	return 0;
}
static int l_shady_log(lua_State *L){
	const char *message=luaL_checkstring(L,1);
	wlr_log(WLR_INFO,"[SHADY LUA] 🌙 %s",message);
	return 0;
}
static void install_api(lua_State *L){
	lua_newtable(L);
	lua_pushcfunction(L,l_shady_log);lua_setfield(L,-2,"log");
	lua_pushcfunction(L,l_shady_config);lua_setfield(L,-2,"config");
	lua_pushcfunction(L,l_shady_bind);lua_setfield(L,-2,"bind");
	lua_pushcfunction(L,l_shady_camera);lua_setfield(L,-2,"camera");
	lua_pushcfunction(L,l_shady_toggle_gravity);lua_setfield(L,-2,"toggle_gravity");
	lua_pushcfunction(L,l_shady_toggle_fps);lua_setfield(L,-2,"toggle_fps");
	lua_pushcfunction(L,l_shady_quit);lua_setfield(L,-2,"quit");
	lua_pushcfunction(L,l_shady_windows);lua_setfield(L,-2,"windows");
	lua_pushcfunction(L,l_shady_expand_all);lua_setfield(L,-2,"expand_all");
	lua_pushcfunction(L,l_shady_fold_all);lua_setfield(L,-2,"fold_all");
	lua_pushcfunction(L,l_shady_respawn_all);lua_setfield(L,-2,"respawn_all");
	lua_setglobal(L,"shady");
}
static void default_script_path(char *buf,size_t size){
	const char *override=getenv("SHADY_LUA_INIT");if(override&&*override){snprintf(buf,size,"%s",override);return;}
	const char *xdg=getenv("XDG_CONFIG_HOME"),*home=getenv("HOME");
	if(xdg&&*xdg)snprintf(buf,size,"%s/shady/init.lua",xdg);
	else if(home&&*home)snprintf(buf,size,"%s/.config/shady/init.lua",home);
	else snprintf(buf,size,"init.lua");
}
bool shady_lua_init(struct shady_server *server){
	lua_State *L=luaL_newstate();if(!L){wlr_log(WLR_ERROR,"[SHADY LUA] failed to create Lua state");return false;}
	server->lua.L=L;lua_server=server;luaL_openlibs(L);install_api(L);
	char path[4096];default_script_path(path,sizeof(path));
	if(luaL_loadfile(L,path)!=LUA_OK){
		const char *e=lua_tostring(L,-1);
		if(e&&strstr(e,"No such file or directory"))wlr_log(WLR_INFO,"[SHADY LUA] 💤 no init script: %s",path);
		else wlr_log(WLR_ERROR,"[SHADY LUA] load error: %s",e?e:"unknown error");
		lua_pop(L,1);return true;
	}
	if(lua_pcall(L,0,0,0)!=LUA_OK){wlr_log(WLR_ERROR,"[SHADY LUA] runtime error: %s",lua_tostring(L,-1));lua_pop(L,1);return true;}
	wlr_log(WLR_INFO,"[SHADY LUA] ✅ loaded %s (Lua %s)",path,LUA_VERSION);
	return true;
}
static void push_window(lua_State *L,struct shady_toplevel *t){
	lua_newtable(L);const char *title=t&&t->xdg_toplevel->title?t->xdg_toplevel->title:"";const char *app=t&&t->xdg_toplevel->app_id?t->xdg_toplevel->app_id:"";
	lua_pushstring(L,title);lua_setfield(L,-2,"title");lua_pushstring(L,app);lua_setfield(L,-2,"app_id");
	if(t){lua_pushnumber(L,t->experimental.z);lua_setfield(L,-2,"z");}
}
void shady_lua_emit(struct shady_server *server,const char *event,struct shady_toplevel *t){
	lua_State *L=server->lua.L;if(!L)return;lua_getglobal(L,"shady");lua_getfield(L,-1,"on_" );lua_pop(L,2);
	lua_getglobal(L,"shady_events");if(!lua_istable(L,-1)){lua_pop(L,1);return;}lua_getfield(L,-1,event);
	if(lua_isfunction(L,-1)){push_window(L,t);if(lua_pcall(L,1,0,0)!=LUA_OK){wlr_log(WLR_ERROR,"[SHADY LUA] event %s: %s",event,lua_tostring(L,-1));lua_pop(L,1);}}else lua_pop(L,1);lua_pop(L,1);
}
bool shady_lua_handle_key(struct shady_server *server,xkb_keysym_t sym,uint32_t modifiers){
	lua_State *L=server->lua.L;uint32_t mask=WLR_MODIFIER_ALT|WLR_MODIFIER_SHIFT|WLR_MODIFIER_CTRL|WLR_MODIFIER_LOGO;
	for(size_t i=0;L&&i<lua_bind_count;i++)if(lua_binds[i].sym==sym&&lua_binds[i].modifiers==(modifiers&mask)){
		lua_rawgeti(L,LUA_REGISTRYINDEX,lua_binds[i].ref);if(lua_pcall(L,0,0,0)!=LUA_OK){wlr_log(WLR_ERROR,"[SHADY LUA] keybind: %s",lua_tostring(L,-1));lua_pop(L,1);}return true;}return false;
}
void shady_lua_fini(struct shady_server *server){
	if(server->lua.L){lua_close(server->lua.L);server->lua.L=NULL;}lua_bind_count=0;
}
