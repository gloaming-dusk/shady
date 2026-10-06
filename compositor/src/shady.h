/* Adapted from wlroots 0.20.2 TinyWL (CC0). See LICENSES/tinywl-CC0.txt. */
#ifndef SHADY_H
#define SHADY_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <xkbcommon/xkbcommon.h>

#include "experimental/state.h"
#include "module/module.h"
#include "plugin/manager.h"
#include "event/event.h"
#include <shady/plugin.h>
#include <shady/motion.h>

struct wlr_allocator;
struct wlr_backend;
struct wlr_session;
struct wlr_cursor;
struct wlr_output;
struct wlr_output_layout;
struct wlr_renderer;
struct wlr_seat;
struct wlr_surface;
struct wlr_xcursor_manager;
struct wlr_xdg_shell;
struct wlr_xdg_toplevel;
struct wlr_xdg_popup;
struct wlr_layer_shell_v1;
struct wlr_relative_pointer_manager_v1;
struct wlr_pointer_constraints_v1;
struct wlr_pointer_constraint_v1;
struct wlr_screencopy_manager_v1;
struct wlr_idle_notifier_v1;
struct wlr_primary_selection_v1_device_manager;
struct wlr_data_control_manager_v1;
struct wlr_xdg_decoration_manager_v1;
struct wlr_output_manager_v1;
struct wlr_session_lock_manager_v1;
struct wlr_session_lock_v1;
struct wlr_scene_tree;
struct wlr_scene_buffer;
struct wlr_buffer;
struct wlr_texture;
struct wlr_layer_surface_v1;
struct wlr_scene_layer_surface_v1;
struct wlr_keyboard;
struct wlr_input_device;

enum shady_cursor_mode {
	SHADY_CURSOR_PASSTHROUGH,
	SHADY_CURSOR_MOVE,
	SHADY_CURSOR_RESIZE,
	SHADY_CURSOR_CAMERA_ORBIT,
	SHADY_CURSOR_CAMERA_PAN,
};

struct shady_keybind {
	xkb_keysym_t sym;
	uint32_t modifiers;
};

struct shady_config {
	bool spatial_mode;
	bool physics_enabled;
	bool window_gravity;
	bool window_wobble;
	bool window_sides;
	bool shadows;
	bool floor;
	bool close_animation;
	bool fps_mode;
	bool sky;
	char sky_path[512];
	float background_color[3];
	float background_top[3];
	float background_horizon[3];
	float background_bottom[3];
	float floor_base_color[3];
	float floor_grid_color[3];
	float floor_grid_strength;
	float floor_major_strength;
	float floor_fade_start;
	float floor_fade_end;
	float floor_horizon_fog;
	float window_tint[3];
	float window_effect_strength;
	float window_brightness;
	float window_opacity;
	float window_border_width;
	float window_corner_radius;
	float window_border_color[3];
	float window_border_focus_color[3];
	bool window_titlebar;
	float window_titlebar_height;
	float window_titlebar_color[3];
	float window_titlebar_focus_color[3];
	float window_titlebar_text_color[3];
	/* Static 3D environment, loaded by the plugin registered for the
	 * path's extension (see docs/ENVIRONMENT_API.md). */
	bool environment;
	char environment_path[512];

	struct shady_keybind bind_quit;
	struct shady_keybind bind_cycle_windows;
	struct shady_keybind bind_close_window;
	struct shady_keybind bind_fps_toggle;
	struct shady_keybind bind_fps_capture;
	struct shady_keybind bind_gravity_toggle;
	struct shady_keybind bind_debug_ray;
	struct shady_keybind bind_camera_left;
	struct shady_keybind bind_camera_right;
	struct shady_keybind bind_camera_up;
	struct shady_keybind bind_camera_down;
	struct shady_keybind bind_camera_yaw_left;
	struct shady_keybind bind_camera_yaw_right;
	struct shady_keybind bind_camera_zoom_in;
	struct shady_keybind bind_camera_zoom_out;
	struct shady_keybind bind_camera_reset;
};

struct shady_representation_state;

struct shady_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	/* NULL without a seat (nested and headless backends). */
	struct wlr_session *session;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_tree *content_tree;
	struct wlr_scene_tree *overlay_tree;
	struct wlr_scene_output_layout *scene_layout;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels;
	struct wl_list all_toplevels;
	uint32_t next_shell_window_id;
	struct wl_list popups;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener pointer_focus_change;
	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;
	struct wl_list keyboards;
	enum shady_cursor_mode cursor_mode;
	struct shady_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;

	struct shady_config config;
	struct shady_module_manager modules;
	struct shady_plugin_manager plugins;
	struct shady_event_bus events;
	const struct shady_motion_driver *motion_driver;
	void *motion_owner;
	/* Spatial builds only; created and destroyed with the spatial module. */
	struct shady_environment *environment;
	/* Compositor IPC socket (src/ipc); NULL when unavailable. */
	struct shady_ipc *ipc;

};

struct shady_output {
	struct wl_list link;
	struct shady_server *server;
	struct wlr_output *wlr_output;
	bool frame_scheduled;
	uint64_t frame_schedule_requests;
	uint64_t frame_schedule_coalesced;
	uint64_t frame_callbacks;
	uint64_t profile_samples;
	uint64_t profile_frame_ns;
	uint64_t profile_effects_ns;
	uint64_t profile_windows_ns;
	uint64_t profile_overlay_ns;
	uint64_t profile_submit_ns;
	uint64_t continuous_camera_frames;
	uint64_t continuous_motion_frames;
	uint64_t continuous_effect_frames;
	uint64_t continuous_physics_frames;
	uint64_t continuous_close_frames;
	uint64_t continuous_snapshot_frames;
	bool lock_frame_pending;
	uint32_t lock_commit_seq;
	struct wl_listener frame;
	struct wl_listener present;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

struct shady_toplevel {
	struct wl_list link;
	struct wl_list all_link;
	struct shady_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	uint32_t shell_id;
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *focus_border_tree;
	struct wlr_scene_rect *focus_border[4];
	struct wlr_scene_tree *titlebar_tree;
	struct wlr_scene_buffer *titlebar_scene_buffer;
	struct wlr_buffer *titlebar_buffer;
	struct wlr_texture *titlebar_texture;
	int titlebar_width;
	int titlebar_height;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener set_title;
	struct wl_listener set_app_id;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	int last_surface_width;
	int last_surface_height;
	bool mapped;
	bool maximized;
	bool fullscreen;
	bool fullscreen_restore_maximized;
	bool restore_geometry_valid;
	struct wlr_box restore_geometry;
	/* Optional native-plugin driven surface wave effect. Zero amplitude means off. */
	float water_amplitude;
	float water_frequency;
	float water_speed;
	float water_phase;
	float water_fresnel;
	float water_specular;
	float water_caustic;
	float water_tint;
	bool border_override;
	float border_width;
	float border_color[4];
	bool close_effect_override;
	uint32_t close_effect_style;
	float close_effect_duration;
	float close_effect_strength;
	float close_effect_direction_x;
	float close_effect_direction_y;
	shady_shader_program plugin_shader_program;
	void *plugin_shader_owner;
	/* u_params for plugin_shader_program; see window_set_shader_params. */
	float plugin_shader_params[SHADY_WINDOW_SHADER_PARAMS * 4];
	struct shady_toplevel *plugin_shader_source;
	void *plugin_shader_source_owner;
	struct shady_representation_state *representation;
	struct shady_motion_visual motion;
	/* Opaque per-module extension state. The core toplevel does not know
	 * which optional modules attach data here. */
	void *module_state[SHADY_MAX_MODULES];
};

struct shady_session_lock {
	struct shady_server *server;
	struct wlr_session_lock_v1 *lock;
	bool unlocked;
	struct wl_listener new_surface;
	struct wl_listener unlock;
	struct wl_listener destroy;
};

struct shady_lock_surface {
	struct shady_session_lock *lock;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_scene_tree *tree;
	struct wl_listener destroy;
};

struct shady_layer_surface {
	struct wl_list link;
	struct shady_server *server;
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_layer_surface_v1 *scene_layer;
	bool mapped;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct shady_popup {
	struct wl_list link;
	struct shady_server *server;
	struct wlr_xdg_popup *xdg_popup;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct shady_keyboard {
	struct wl_list link;
	struct shady_server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct xkb_state *binding_state;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

/* config.c */
void shady_config_defaults(struct shady_config *config);
bool shady_config_load(struct shady_config *config, const char *path);
bool shady_config_set(struct shady_config *config,const char *key,const char *value);

/* Shared helpers used across compositor modules */
void focus_toplevel(struct shady_toplevel *toplevel);
void shady_toplevel_begin_interactive(struct shady_toplevel *toplevel,
	enum shady_cursor_mode mode, uint32_t edges);
void shady_toplevel_set_maximized(struct shady_toplevel *toplevel, bool enabled);
void shady_toplevel_set_fullscreen(struct shady_toplevel *toplevel, bool enabled);
void shady_toplevel_refresh_state(struct shady_toplevel *toplevel);
void shady_toplevel_get_border(const struct shady_toplevel *toplevel,
	float *width, float color[4]);
void shady_toplevel_refresh_border(struct shady_toplevel *toplevel);
void shady_titlebar_refresh(struct shady_toplevel *toplevel);
void shady_toplevel_recover_to_output(struct shady_toplevel *toplevel,
	struct wlr_output *output);
void reset_cursor_mode(struct shady_server *server);

/* input.c */
void server_new_input(struct wl_listener *listener, void *data);
void shady_input_add_keyboard(struct shady_server *server,
	struct wlr_input_device *device);
void shady_input_update_seat_capabilities(struct shady_server *server);
void seat_request_cursor(struct wl_listener *listener, void *data);
void seat_pointer_focus_change(struct wl_listener *listener, void *data);
void seat_request_set_selection(struct wl_listener *listener, void *data);
void seat_request_set_primary_selection(struct wl_listener *listener, void *data);
void server_cursor_motion(struct wl_listener *listener, void *data);
void server_cursor_motion_absolute(struct wl_listener *listener, void *data);
void server_cursor_button(struct wl_listener *listener, void *data);
bool shady_input_pointer_on_overlay(struct shady_server *server);
void server_cursor_axis(struct wl_listener *listener, void *data);
void server_cursor_frame(struct wl_listener *listener, void *data);
bool shady_input_automation_key(struct shady_server *server, xkb_keysym_t sym,
	uint32_t modifiers, bool pressed);
void shady_input_automation_pointer_move(struct shady_server *server,
	double x, double y);
void shady_input_automation_pointer_button(struct shady_server *server,
	uint32_t button, bool pressed);
void shady_input_automation_pointer_axis(struct shady_server *server,
	enum wl_pointer_axis orientation, double delta, int32_t discrete);

/* output.c */
void server_new_output(struct wl_listener *listener, void *data);
void shady_recover_toplevels_to_outputs(struct shady_server *server);
void shady_output_manager_publish(struct shady_server *server);
void shady_output_manager_apply(struct wl_listener *listener, void *data);
void shady_output_manager_test(struct wl_listener *listener, void *data);

/* session_lock.c */
void server_new_session_lock(struct wl_listener *listener, void *data);

/* xdg.c */
void server_new_xdg_toplevel(struct wl_listener *listener, void *data);
void server_new_xdg_popup(struct wl_listener *listener, void *data);

/* layer.c */
void server_new_layer_surface(struct wl_listener *listener, void *data);
void shady_layers_arrange(struct shady_server *server);
void shady_output_work_area(struct shady_server *server,
	struct wlr_output *output, struct wlr_box *box);

bool shady_toplevel_representation_base(const struct shady_toplevel *toplevel,
	struct shady_window_representation *representation);
bool shady_toplevel_representation_model(const struct shady_toplevel *toplevel,
	const struct shady_representation_context *context,
	struct shady_representation_model *model);
bool shady_toplevel_representation_mesh(const struct shady_toplevel *toplevel,
	const struct shady_representation_context *context,
	struct shady_representation_mesh *mesh);
bool shady_toplevel_representation_collision(const struct shady_toplevel *toplevel,
	const struct shady_representation_context *context,
	const struct shady_representation_model *model,
	struct shady_collision_box *box);
bool shady_toplevel_representation_collision_hull(
	const struct shady_toplevel *toplevel,
	const struct shady_representation_context *context,
	struct shady_collision_hull *hull);
bool shady_toplevel_representation_collision_hull_world(
	struct shady_toplevel *toplevel,
	const struct shady_representation_context *context,
	const struct shady_representation_model *model,
	const float (**vertices_world)[3], size_t *vertex_count,
	const uint16_t **indices, size_t *index_count,
	float center[3], float half[3]);

struct shady_resolved_collision_part {
	const float (*vertices)[3];
	size_t vertex_count;
	const uint16_t *indices;
	size_t index_count;
};

struct shady_resolved_collision_compound {
	size_t part_count;
	struct shady_resolved_collision_part parts[8];
	float center[3];
	float half[3];
};

bool shady_toplevel_representation_collision_compound_world(
	struct shady_toplevel *toplevel,
	const struct shady_representation_context *context,
	const struct shady_representation_model *model,
	struct shady_resolved_collision_compound *compound);

#endif
