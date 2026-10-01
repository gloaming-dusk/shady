#include "shady.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_keyboard.h>

void shady_config_defaults(struct shady_config *c) {
	*c = (struct shady_config){
		.spatial_mode = true,
		.physics_enabled = true,
		.window_gravity = false,
		.window_wobble = true,
		.window_sides = true,
		.shadows = true,
		.floor = true,
		.close_animation = true,
		.fps_mode = true,
		.sky = false,
		.sky_path = "",
		.environment_obj = false,
		.environment_obj_path = "",
		.bind_quit = { XKB_KEY_Escape, 0 },
		.bind_cycle_windows = { XKB_KEY_F1, 0 },
		.bind_close_window = { XKB_KEY_F11, WLR_MODIFIER_ALT },
		.bind_fps_toggle = { XKB_KEY_F2, 0 },
		.bind_fps_capture = { XKB_KEY_F3, 0 },
		.bind_gravity_toggle = { XKB_KEY_F4, 0 },
		.bind_debug_ray = { XKB_KEY_F5, 0 },
		.bind_camera_left = { XKB_KEY_Left, WLR_MODIFIER_ALT },
		.bind_camera_right = { XKB_KEY_Right, WLR_MODIFIER_ALT },
		.bind_camera_up = { XKB_KEY_Up, WLR_MODIFIER_ALT },
		.bind_camera_down = { XKB_KEY_Down, WLR_MODIFIER_ALT },
		.bind_camera_yaw_left = { XKB_KEY_q, WLR_MODIFIER_ALT },
		.bind_camera_yaw_right = { XKB_KEY_e, WLR_MODIFIER_ALT },
		.bind_camera_zoom_in = { XKB_KEY_equal, WLR_MODIFIER_ALT },
		.bind_camera_zoom_out = { XKB_KEY_minus, WLR_MODIFIER_ALT },
		.bind_camera_reset = { XKB_KEY_0, WLR_MODIFIER_ALT },
	};
}

static char *trim(char *s) {
	while (isspace((unsigned char)*s)) s++;
	char *end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1])) end--;
	*end = '\0';
	return s;
}

static bool parse_bool(const char *s, bool *out) {
	if (!strcasecmp(s,"true") || !strcasecmp(s,"yes") || !strcasecmp(s,"on") || !strcmp(s,"1")) { *out=true; return true; }
	if (!strcasecmp(s,"false") || !strcasecmp(s,"no") || !strcasecmp(s,"off") || !strcmp(s,"0")) { *out=false; return true; }
	return false;
}


static bool parse_keybind(const char *s, struct shady_keybind *out) {
	char buf[128];
	if (strlen(s) >= sizeof(buf)) return false;
	strcpy(buf, s);
	uint32_t mods = 0;
	char *save = NULL, *token = strtok_r(buf, "+", &save), *key = NULL;
	while (token) {
		token = trim(token);
		if (!strcasecmp(token, "Alt")) mods |= WLR_MODIFIER_ALT;
		else if (!strcasecmp(token, "Shift")) mods |= WLR_MODIFIER_SHIFT;
		else if (!strcasecmp(token, "Ctrl") || !strcasecmp(token, "Control")) mods |= WLR_MODIFIER_CTRL;
		else if (!strcasecmp(token, "Super") || !strcasecmp(token, "Logo")) mods |= WLR_MODIFIER_LOGO;
		else {
			if (key) return false;
			key = token;
		}
		token = strtok_r(NULL, "+", &save);
	}
	if (!key || !*key) return false;
	xkb_keysym_t sym = xkb_keysym_from_name(key, XKB_KEYSYM_CASE_INSENSITIVE);
	if (sym == XKB_KEY_NoSymbol) return false;
	*out = (struct shady_keybind){ .sym = sym, .modifiers = mods };
	return true;
}

bool shady_config_set(struct shady_config *c,const char *key,const char *value){
	if(!strcmp(key,"sky_path")){snprintf(c->sky_path,sizeof(c->sky_path),"%s",value);return true;}
	if(!strcmp(key,"environment_obj_path")){snprintf(c->environment_obj_path,sizeof(c->environment_obj_path),"%s",value);return true;}
#define BIND_SET(name,field) if(!strcmp(key,"bind." name))return parse_keybind(value,&c->field);
	BIND_SET("quit",bind_quit) BIND_SET("cycle_windows",bind_cycle_windows)
	BIND_SET("close_window",bind_close_window) BIND_SET("fps_toggle",bind_fps_toggle)
	BIND_SET("fps_capture",bind_fps_capture) BIND_SET("gravity_toggle",bind_gravity_toggle)
	BIND_SET("debug_ray",bind_debug_ray) BIND_SET("camera_left",bind_camera_left)
	BIND_SET("camera_right",bind_camera_right) BIND_SET("camera_up",bind_camera_up)
	BIND_SET("camera_down",bind_camera_down) BIND_SET("camera_yaw_left",bind_camera_yaw_left)
	BIND_SET("camera_yaw_right",bind_camera_yaw_right) BIND_SET("camera_zoom_in",bind_camera_zoom_in)
	BIND_SET("camera_zoom_out",bind_camera_zoom_out) BIND_SET("camera_reset",bind_camera_reset)
#undef BIND_SET
	bool v;if(!parse_bool(value,&v))return false;
#define BOOL_SET(name,field) if(!strcmp(key,name)){c->field=v;return true;}
	BOOL_SET("spatial_mode",spatial_mode) BOOL_SET("physics_enabled",physics_enabled) BOOL_SET("window_gravity",window_gravity)
	BOOL_SET("window_wobble",window_wobble) BOOL_SET("window_sides",window_sides)
	BOOL_SET("shadows",shadows) BOOL_SET("floor",floor) BOOL_SET("close_animation",close_animation)
	BOOL_SET("fps_mode",fps_mode) BOOL_SET("sky",sky) BOOL_SET("environment_obj",environment_obj)
#undef BOOL_SET
	return false;
}

bool shady_config_load(struct shady_config *c, const char *path) {
	FILE *fp = fopen(path, "r");
	if (!fp) {
		if (errno != ENOENT) wlr_log(WLR_ERROR, "config: cannot open %s: %s", path, strerror(errno));
		else wlr_log(WLR_INFO, "config: %s not found, using defaults", path);
		return false;
	}
	char line[512]; unsigned lineno=0;
	while (fgets(line, sizeof(line), fp)) {
		lineno++;
		char *p=trim(line);
		if (!*p || *p=='#' || *p==';') continue;
		char *eq=strchr(p,'=');
		if (!eq) { wlr_log(WLR_ERROR,"config:%u: expected key = value",lineno); continue; }
		*eq='\0'; char *key=trim(p), *value=trim(eq+1);
		char *comment=strpbrk(value,"#;");
		if (!strcmp(key,"sky_path")) { if (comment) *comment='\0'; value=trim(value); snprintf(c->sky_path,sizeof(c->sky_path),"%s",value); continue; }
		if (!strcmp(key,"environment_obj_path")) { if (comment) *comment='\0'; value=trim(value); snprintf(c->environment_obj_path,sizeof(c->environment_obj_path),"%s",value); continue; }
		if (comment) { *comment='\0'; value=trim(value); }
#define BIND(name, field) if (!strcmp(key, "bind." name)) { \
			if (!parse_keybind(value, &c->field)) wlr_log(WLR_ERROR, "config:%u: invalid keybind '%s'", lineno, value); \
			continue; \
		}
		BIND("quit", bind_quit)
		BIND("cycle_windows", bind_cycle_windows)
		BIND("close_window", bind_close_window)
		BIND("fps_toggle", bind_fps_toggle)
		BIND("fps_capture", bind_fps_capture)
		BIND("gravity_toggle", bind_gravity_toggle)
		BIND("debug_ray", bind_debug_ray)
		BIND("camera_left", bind_camera_left)
		BIND("camera_right", bind_camera_right)
		BIND("camera_up", bind_camera_up)
		BIND("camera_down", bind_camera_down)
		BIND("camera_yaw_left", bind_camera_yaw_left)
		BIND("camera_yaw_right", bind_camera_yaw_right)
		BIND("camera_zoom_in", bind_camera_zoom_in)
		BIND("camera_zoom_out", bind_camera_zoom_out)
		BIND("camera_reset", bind_camera_reset)
#undef BIND
		bool v;
		if (!parse_bool(value,&v)) { wlr_log(WLR_ERROR,"config:%u: invalid boolean '%s'",lineno,value); continue; }
#define KEY(name, field) if (!strcmp(key,name)) { c->field=v; continue; }
		KEY("spatial_mode",spatial_mode)
		KEY("physics_enabled",physics_enabled)
		KEY("window_gravity",window_gravity)
		KEY("window_wobble",window_wobble)
		KEY("window_sides",window_sides)
		KEY("shadows",shadows)
		KEY("floor",floor)
		KEY("close_animation",close_animation)
		KEY("fps_mode",fps_mode)
		KEY("sky",sky)
		KEY("environment_obj",environment_obj)
#undef KEY
		wlr_log(WLR_ERROR,"config:%u: unknown key '%s'",lineno,key);
	}
	fclose(fp);
	wlr_log(WLR_INFO,"config: loaded %s",path);
	return true;
}
