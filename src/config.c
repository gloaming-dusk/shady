#include "shady.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
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
		.background_color = { 0.055f, 0.060f, 0.085f },
		.background_top = { 0.055f, 0.060f, 0.085f },
		.background_horizon = { 0.075f, 0.090f, 0.125f },
		.background_bottom = { 0.025f, 0.030f, 0.045f },
		.floor_base_color = { 0.018f, 0.024f, 0.038f },
		.floor_grid_color = { 0.08f, 0.24f, 0.38f },
		.floor_grid_strength = 0.22f,
		.floor_major_strength = 0.28f,
		.floor_fade_start = 1.4f,
		.floor_fade_end = 5.2f,
		.floor_horizon_fog = 1.0f,
		.window_tint = { 0.85f, 0.90f, 1.10f },
		.window_effect_strength = 1.0f,
		.window_brightness = 1.0f,
		.window_opacity = 1.0f,
		.window_border_width = 3.0f,
		.window_corner_radius = 10.0f,
		.window_border_color = { 0.055f, 0.20f, 0.30f },
		.window_border_focus_color = { 0.10f, 0.65f, 1.0f },
		.window_titlebar = true,
		.window_titlebar_height = 28.0f,
		.window_titlebar_color = { 0.035f, 0.075f, 0.10f },
		.window_titlebar_focus_color = { 0.055f, 0.16f, 0.22f },
		.window_titlebar_text_color = { 0.90f, 0.96f, 1.0f },
		.environment_obj = false,
		.environment_obj_path = "",
		.bind_quit = { XKB_KEY_Escape, 0 },
		.bind_cycle_windows = { XKB_KEY_Tab, WLR_MODIFIER_ALT },
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

static bool parse_color(const char *s, float out[3]) {
	if (!s || !out) return false;
	if (s[0] == '#' && strlen(s) == 7) {
		unsigned r = 0, g = 0, b = 0;
		if (sscanf(s + 1, "%02x%02x%02x", &r, &g, &b) != 3) return false;
		out[0] = (float)r / 255.0f;
		out[1] = (float)g / 255.0f;
		out[2] = (float)b / 255.0f;
		return true;
	}
	float r = 0.f, g = 0.f, b = 0.f;
	if (sscanf(s, "%f,%f,%f", &r, &g, &b) == 3) {
		if (r < 0.f || r > 1.f || g < 0.f || g > 1.f || b < 0.f || b > 1.f) return false;
		out[0] = r; out[1] = g; out[2] = b;
		return true;
	}
	return false;
}

static bool parse_float_range(const char *s, float min, float max, float *out) {
	if (!s || !out) return false;
	char *end = NULL;
	float value = strtof(s, &end);
	if (!end || end == s) return false;
	while (*end && isspace((unsigned char)*end)) end++;
	if (*end != '\0' || value < min || value > max) return false;
	*out = value;
	return true;
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
	if(!strcmp(key,"background_color")){
		if(!parse_color(value,c->background_color))return false;
		memcpy(c->background_top,c->background_color,sizeof(c->background_top));
		memcpy(c->background_horizon,c->background_color,sizeof(c->background_horizon));
		memcpy(c->background_bottom,c->background_color,sizeof(c->background_bottom));
		return true;
	}
	if(!strcmp(key,"background_top"))return parse_color(value,c->background_top);
	if(!strcmp(key,"background_horizon"))return parse_color(value,c->background_horizon);
	if(!strcmp(key,"background_bottom"))return parse_color(value,c->background_bottom);
	if(!strcmp(key,"floor_base_color"))return parse_color(value,c->floor_base_color);
	if(!strcmp(key,"floor_grid_color"))return parse_color(value,c->floor_grid_color);
	if(!strcmp(key,"floor_grid_strength"))return parse_float_range(value,0.f,2.f,&c->floor_grid_strength);
	if(!strcmp(key,"floor_major_strength"))return parse_float_range(value,0.f,2.f,&c->floor_major_strength);
	if(!strcmp(key,"floor_fade_start"))return parse_float_range(value,0.f,20.f,&c->floor_fade_start);
	if(!strcmp(key,"floor_fade_end"))return parse_float_range(value,0.01f,40.f,&c->floor_fade_end);
	if(!strcmp(key,"floor_horizon_fog"))return parse_float_range(value,0.f,1.f,&c->floor_horizon_fog);
	if(!strcmp(key,"window_tint"))return parse_color(value,c->window_tint);
	if(!strcmp(key,"window_effect_strength"))return parse_float_range(value,0.f,1.f,&c->window_effect_strength);
	if(!strcmp(key,"window_brightness"))return parse_float_range(value,0.25f,3.f,&c->window_brightness);
	if(!strcmp(key,"window_opacity"))return parse_float_range(value,0.f,1.f,&c->window_opacity);
	if(!strcmp(key,"window_border_width"))return parse_float_range(value,0.f,32.f,&c->window_border_width);
	if(!strcmp(key,"window_corner_radius"))return parse_float_range(value,0.f,48.f,&c->window_corner_radius);
	if(!strcmp(key,"window_border_color"))return parse_color(value,c->window_border_color);
	if(!strcmp(key,"window_border_focus_color"))return parse_color(value,c->window_border_focus_color);
	if(!strcmp(key,"window_titlebar_height"))return parse_float_range(value,16.f,64.f,&c->window_titlebar_height);
	if(!strcmp(key,"window_titlebar_color"))return parse_color(value,c->window_titlebar_color);
	if(!strcmp(key,"window_titlebar_focus_color"))return parse_color(value,c->window_titlebar_focus_color);
	if(!strcmp(key,"window_titlebar_text_color"))return parse_color(value,c->window_titlebar_text_color);
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
	BOOL_SET("window_titlebar",window_titlebar)
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
