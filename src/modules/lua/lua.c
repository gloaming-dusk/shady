#include "lua.h"
#include "state.h"
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <wlr/util/log.h>
#include "../../shady.h"
#include "../../render/render.h"
#include "../../event/event.h"
#include "../../plugin/plugin.h"
#include "../physics/physics.h"
#include "../fps/fps.h"
#include "../fps/state.h"
#include "../spatial/state.h"
#include <string.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>

static struct shady_server *lua_server;
static uint64_t lua_next_handler_id = 1;

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
static int l_shady_quit(lua_State *L){(void)L;if(lua_server->wl_display)wl_display_terminate(lua_server->wl_display);return 0;}
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

static int l_window_focus(lua_State *L){struct lua_window_handle*h=luaL_checkudata(L,1,SHADY_LUA_WINDOW_MT);if(!h->ptr||!lua_window_live(h->ptr)){lua_pushboolean(L,0);return 1;}focus_toplevel(h->ptr);lua_pushboolean(L,1);return 1;}
static int l_window_close(lua_State *L){struct lua_window_handle*h=luaL_checkudata(L,1,SHADY_LUA_WINDOW_MT);if(!h->ptr||!lua_window_live(h->ptr)||!h->ptr->xdg_toplevel){lua_pushboolean(L,0);return 1;}wlr_xdg_toplevel_send_close(h->ptr->xdg_toplevel);lua_pushboolean(L,1);return 1;}
static int l_window_index(lua_State *L){struct lua_window_handle*h=luaL_checkudata(L,1,SHADY_LUA_WINDOW_MT);const char*k=luaL_checkstring(L,2);if(!h->ptr||!lua_window_live(h->ptr)){lua_pushnil(L);return 1;}struct shady_toplevel*t=h->ptr;if(!strcmp(k,"title")){lua_pushstring(L,t->xdg_toplevel->title?t->xdg_toplevel->title:"");return 1;}if(!strcmp(k,"app_id")){lua_pushstring(L,t->xdg_toplevel->app_id?t->xdg_toplevel->app_id:"");return 1;}if(!strcmp(k,"mapped")){lua_pushboolean(L,t->xdg_toplevel->base->surface->mapped);return 1;}if(!strcmp(k,"z")){const struct shady_toplevel_experimental_state*s=shady_spatial_toplevel_state_const(t);if(s)lua_pushnumber(L,s->z);else lua_pushnil(L);return 1;}if(!strcmp(k,"focus")){lua_pushcfunction(L,l_window_focus);return 1;}if(!strcmp(k,"close")){lua_pushcfunction(L,l_window_close);return 1;}lua_pushnil(L);return 1;}
static int l_window_tostring(lua_State *L){struct lua_window_handle*h=luaL_checkudata(L,1,SHADY_LUA_WINDOW_MT);if(!h->ptr||!lua_window_live(h->ptr)){lua_pushliteral(L,"Window<dead>");return 1;}lua_pushfstring(L,"Window<%s>",h->ptr->xdg_toplevel->app_id?h->ptr->xdg_toplevel->app_id:"");return 1;}

static int l_output_index(lua_State *L){struct lua_output_handle*h=luaL_checkudata(L,1,SHADY_LUA_OUTPUT_MT);const char*k=luaL_checkstring(L,2);if(!h->ptr||!lua_output_live(h->ptr)){lua_pushnil(L);return 1;}struct wlr_output*o=h->ptr->wlr_output;if(!strcmp(k,"name")){lua_pushstring(L,o->name?o->name:"");return 1;}if(!strcmp(k,"scale")){lua_pushnumber(L,o->scale);return 1;}if(!strcmp(k,"width")||!strcmp(k,"height")){int w=0,hg=0;wlr_output_effective_resolution(o,&w,&hg);lua_pushinteger(L,!strcmp(k,"width")?w:hg);return 1;}lua_pushnil(L);return 1;}
static int l_output_tostring(lua_State *L){struct lua_output_handle*h=luaL_checkudata(L,1,SHADY_LUA_OUTPUT_MT);lua_pushfstring(L,"Output<%s>",h->ptr&&lua_output_live(h->ptr)&&h->ptr->wlr_output->name?h->ptr->wlr_output->name:"dead");return 1;}

static int l_seat_index(lua_State *L){struct lua_seat_handle*h=luaL_checkudata(L,1,SHADY_LUA_SEAT_MT);const char*k=luaL_checkstring(L,2);if(!strcmp(k,"name")){lua_pushstring(L,h->ptr&&h->ptr->name?h->ptr->name:"");return 1;}lua_pushnil(L);return 1;}
static int l_seat_tostring(lua_State *L){struct lua_seat_handle*h=luaL_checkudata(L,1,SHADY_LUA_SEAT_MT);lua_pushfstring(L,"Seat<%s>",h->ptr&&h->ptr->name?h->ptr->name:"");return 1;}

static int l_module_index(lua_State *L){struct lua_module_handle*h=luaL_checkudata(L,1,SHADY_LUA_MODULE_MT);const char*k=luaL_checkstring(L,2);size_t i=0;if(!h->ptr||!lua_module_registered(h->ptr,&i)){lua_pushnil(L);return 1;}if(!strcmp(k,"name")){lua_pushstring(L,h->ptr->name?h->ptr->name:"");return 1;}if(!strcmp(k,"active")){lua_pushboolean(L,lua_server->modules.active[i]);return 1;}lua_pushnil(L);return 1;}
static int l_module_tostring(lua_State *L){struct lua_module_handle*h=luaL_checkudata(L,1,SHADY_LUA_MODULE_MT);lua_pushfstring(L,"Module<%s>",h->ptr&&h->ptr->name?h->ptr->name:"");return 1;}

static void register_object_types(lua_State *L){struct{const char*name;lua_CFunction index;lua_CFunction tostring;}mts[]={{SHADY_LUA_WINDOW_MT,l_window_index,l_window_tostring},{SHADY_LUA_OUTPUT_MT,l_output_index,l_output_tostring},{SHADY_LUA_SEAT_MT,l_seat_index,l_seat_tostring},{SHADY_LUA_MODULE_MT,l_module_index,l_module_tostring}};for(size_t i=0;i<sizeof(mts)/sizeof(mts[0]);i++){luaL_newmetatable(L,mts[i].name);lua_pushcfunction(L,mts[i].index);lua_setfield(L,-2,"__index");lua_pushcfunction(L,mts[i].tostring);lua_setfield(L,-2,"__tostring");lua_pop(L,1);}}

static void push_window(lua_State *L,struct shady_toplevel*t){struct lua_window_handle*h=lua_newuserdatauv(L,sizeof(*h),0);h->ptr=t;luaL_setmetatable(L,SHADY_LUA_WINDOW_MT);}
static void push_output(lua_State *L,struct shady_output*o){struct lua_output_handle*h=lua_newuserdatauv(L,sizeof(*h),0);h->ptr=o;luaL_setmetatable(L,SHADY_LUA_OUTPUT_MT);}
static void push_seat(lua_State *L,struct wlr_seat*seat){struct lua_seat_handle*h=lua_newuserdatauv(L,sizeof(*h),0);h->ptr=seat;luaL_setmetatable(L,SHADY_LUA_SEAT_MT);}
static void push_module(lua_State *L,const struct shady_module*m){struct lua_module_handle*h=lua_newuserdatauv(L,sizeof(*h),0);h->ptr=m;luaL_setmetatable(L,SHADY_LUA_MODULE_MT);}

static int l_shady_windows(lua_State *L){lua_newtable(L);int n=1;struct shady_toplevel*t;wl_list_for_each(t,&lua_server->toplevels,link){push_window(L,t);lua_rawseti(L,-2,n++);}return 1;}
static int l_shady_outputs(lua_State *L){lua_newtable(L);int n=1;struct shady_output*o;wl_list_for_each(o,&lua_server->outputs,link){push_output(L,o);lua_rawseti(L,-2,n++);}return 1;}
static int l_shady_seat(lua_State *L){push_seat(L,lua_server->seat);return 1;}
static int l_shady_modules_runtime(lua_State *L){lua_newtable(L);for(size_t i=0;i<lua_server->modules.count;i++){push_module(L,lua_server->modules.modules[i]);lua_rawseti(L,-2,(lua_Integer)i+1);}return 1;}
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
	return 0;
}
static int l_shady_log(lua_State *L){
	const char *message=luaL_checkstring(L,1);
	wlr_log(WLR_INFO,"[SHADY LUA] 🌙 %s",message);
	return 0;
}
static bool lua_event_type(const char *name, enum shady_event_type *type){for(int i=SHADY_EVENT_WINDOW_CREATED;i<=SHADY_EVENT_MODULE_STOPPED;i++){if(!strcmp(name,shady_event_name((enum shady_event_type)i))){*type=(enum shady_event_type)i;return true;}}return false;}
static int l_shady_on(lua_State *L){const char*name=luaL_checkstring(L,1);luaL_checktype(L,2,LUA_TFUNCTION);enum shady_event_type type;if(!lua_event_type(name,&type))return luaL_error(L,"unknown Shady event: %s",name);lua_getglobal(L,"shady_event_handlers");if(!lua_istable(L,-1)){lua_pop(L,1);lua_newtable(L);lua_pushvalue(L,-1);lua_setglobal(L,"shady_event_handlers");}lua_getfield(L,-1,name);if(!lua_istable(L,-1)){lua_pop(L,1);lua_newtable(L);lua_pushvalue(L,-1);lua_setfield(L,-3,name);}lua_Integer n=(lua_Integer)lua_rawlen(L,-1);uint64_t id=lua_next_handler_id++;if(id==0)id=lua_next_handler_id++;lua_newtable(L);lua_pushinteger(L,(lua_Integer)id);lua_setfield(L,-2,"id");lua_pushvalue(L,2);lua_setfield(L,-2,"fn");lua_rawseti(L,-2,n+1);lua_pop(L,2);lua_pushinteger(L,(lua_Integer)id);return 1;}
static int l_shady_off(lua_State *L){uint64_t id=(uint64_t)luaL_checkinteger(L,1);lua_getglobal(L,"shady_event_handlers");if(!lua_istable(L,-1)){lua_pop(L,1);lua_pushboolean(L,0);return 1;}lua_pushnil(L);while(lua_next(L,-2)!=0){if(lua_istable(L,-1)){size_t n=lua_rawlen(L,-1);for(size_t i=1;i<=n;i++){lua_rawgeti(L,-1,(lua_Integer)i);if(lua_istable(L,-1)){lua_getfield(L,-1,"id");uint64_t current=(uint64_t)lua_tointeger(L,-1);lua_pop(L,1);if(current==id){lua_pop(L,1);for(size_t j=i;j<n;j++){lua_rawgeti(L,-1,(lua_Integer)j+1);lua_rawseti(L,-2,(lua_Integer)j);}lua_pushnil(L);lua_rawseti(L,-2,(lua_Integer)n);lua_pop(L,2);lua_pushboolean(L,1);return 1;}}lua_pop(L,1);}}lua_pop(L,1);}lua_pop(L,1);lua_pushboolean(L,0);return 1;}
static int l_shady_reload_plugin(lua_State *L){const char*name=luaL_checkstring(L,1);lua_pushboolean(L,shady_plugin_reload(lua_server,name));return 1;}
static int l_shady_unload_plugin(lua_State *L){const char*name=luaL_checkstring(L,1);lua_pushboolean(L,shady_plugin_unload(lua_server,name));return 1;}
static int l_shady_has_capability(lua_State *L){
	const char *capability=luaL_checkstring(L,1);
	lua_pushboolean(L,shady_module_has_capability(lua_server,capability));
	return 1;
}
static void install_api(lua_State *L){
	register_object_types(L);
	lua_newtable(L);
	lua_pushcfunction(L,l_shady_log);lua_setfield(L,-2,"log");
	lua_pushcfunction(L,l_shady_config);lua_setfield(L,-2,"config");
	lua_pushcfunction(L,l_shady_config);lua_setfield(L,-2,"set");
	lua_pushcfunction(L,l_shady_bind);lua_setfield(L,-2,"bind");
	lua_pushcfunction(L,l_shady_quit);lua_setfield(L,-2,"quit");
	lua_pushcfunction(L,l_shady_spawn);lua_setfield(L,-2,"spawn");
	lua_pushcfunction(L,l_shady_windows);lua_setfield(L,-2,"windows");
	lua_pushcfunction(L,l_shady_outputs);lua_setfield(L,-2,"outputs");
	lua_pushcfunction(L,l_shady_seat);lua_setfield(L,-2,"seat");
	lua_pushcfunction(L,l_shady_modules_runtime);lua_setfield(L,-2,"modules");
	lua_pushcfunction(L,l_shady_has_capability);lua_setfield(L,-2,"has_capability");
	lua_pushcfunction(L,l_shady_on);lua_setfield(L,-2,"on");
	lua_pushcfunction(L,l_shady_off);lua_setfield(L,-2,"off");
	lua_pushcfunction(L,l_shady_reload_plugin);lua_setfield(L,-2,"reload_plugin");
	lua_pushcfunction(L,l_shady_unload_plugin);lua_setfield(L,-2,"unload_plugin");
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
static void lua_event_callback(shady_host host,const struct shady_event *event,void *user_data){(void)host;(void)user_data;lua_State*L=shady_lua_state_for(lua_server)->L;if(!L)return;const char*name=shady_event_name(event->type);lua_getglobal(L,"shady_event_handlers");if(!lua_istable(L,-1)){lua_pop(L,1);return;}lua_getfield(L,-1,name);if(!lua_istable(L,-1)){lua_pop(L,2);return;}size_t n=lua_rawlen(L,-1);for(size_t i=1;i<=n;i++){lua_rawgeti(L,-1,(lua_Integer)i);if(!lua_istable(L,-1)){lua_pop(L,1);continue;}lua_getfield(L,-1,"fn");if(!lua_isfunction(L,-1)){lua_pop(L,2);continue;}switch(event->type){case SHADY_EVENT_WINDOW_CREATED:case SHADY_EVENT_WINDOW_MAPPED:case SHADY_EVENT_WINDOW_UNMAPPED:case SHADY_EVENT_WINDOW_FOCUSED:case SHADY_EVENT_WINDOW_DESTROYED:push_window(L,(struct shady_toplevel*)event->object.window);break;case SHADY_EVENT_OUTPUT_ADDED:case SHADY_EVENT_OUTPUT_REMOVED:push_output(L,(struct shady_output*)event->object.output);break;case SHADY_EVENT_MODULE_STARTED:case SHADY_EVENT_MODULE_STOPPED:push_module(L,(const struct shady_module*)event->object.module);break;}if(lua_pcall(L,1,0,0)!=LUA_OK){wlr_log(WLR_ERROR,"[SHADY LUA] event %s: %s",name,lua_tostring(L,-1));lua_pop(L,1);}lua_pop(L,1);}lua_pop(L,2);}
static void default_script_path(char *buf,size_t size){
	const char *override=getenv("SHADY_LUA_INIT");if(override&&*override){snprintf(buf,size,"%s",override);return;}
	const char *xdg=getenv("XDG_CONFIG_HOME"),*home=getenv("HOME");
	if(xdg&&*xdg)snprintf(buf,size,"%s/shady/init.lua",xdg);
	else if(home&&*home)snprintf(buf,size,"%s/.config/shady/init.lua",home);
	else snprintf(buf,size,"init.lua");
}
bool shady_lua_init(struct shady_server *server){
	lua_State *L=luaL_newstate();if(!L){wlr_log(WLR_ERROR,"[SHADY LUA] failed to create Lua state");return false;}
	shady_lua_state_for(server)->L=L;lua_server=server;luaL_openlibs(L);install_api(L);for(uint32_t type=SHADY_EVENT_WINDOW_CREATED;type<=SHADY_EVENT_MODULE_STOPPED;type++){if(!shady_event_subscribe(server,type,lua_event_callback,NULL))return false;}return true;
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
	if(shady_lua_state_for(server)->L){lua_close(shady_lua_state_for(server)->L);shady_lua_state_for(server)->L=NULL;}lua_bind_count=0;lua_next_handler_id=1;
}
