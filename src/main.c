/* Adapted from wlroots 0.20.2 TinyWL (CC0). See LICENSES/tinywl-CC0.txt. */
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#if SHADY_HAS_SPATIAL
#include <wlr/render/gles2.h>
#endif
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "shady.h"
#include "titlebar.h"
#include "render/render.h"
#include "module/module.h"
#include "config_lua.h"

static void default_config_path(char *buf, size_t size) {
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (xdg && *xdg) snprintf(buf, size, "%s/shady/config.lua", xdg);
	else if (home && *home) snprintf(buf, size, "%s/.config/shady/config.lua", home);
	else snprintf(buf, size, "config.lua");
}

static int terminate_display(int signal_number, void *data) {
	(void)signal_number;
	wl_display_terminate(data);
	return 0;
}

static int reap_children(int signal_number, void *data) {
	(void)signal_number;
	(void)data;
	while (waitpid(-1, NULL, WNOHANG) > 0) {}
	return 0;
}

static void cleanup_server_after_setup(struct shady_server *server,
		struct wl_event_source *sigint, struct wl_event_source *sigterm,
		struct wl_event_source *sigchld) {
	shady_modules_destroy_all(server);

	wl_list_remove(&server->new_xdg_toplevel.link);
	wl_list_remove(&server->new_xdg_popup.link);
	wl_list_remove(&server->cursor_motion.link);
	wl_list_remove(&server->cursor_motion_absolute.link);
	wl_list_remove(&server->cursor_button.link);
	wl_list_remove(&server->cursor_axis.link);
	wl_list_remove(&server->cursor_frame.link);
	wl_list_remove(&server->new_input.link);
	wl_list_remove(&server->request_cursor.link);
	wl_list_remove(&server->pointer_focus_change.link);
	wl_list_remove(&server->request_set_selection.link);
	wl_list_remove(&server->request_set_primary_selection.link);
	wl_list_remove(&server->new_output.link);

	if (sigint) wl_event_source_remove(sigint);
	if (sigterm) wl_event_source_remove(sigterm);
	if (sigchld) wl_event_source_remove(sigchld);

	wlr_scene_node_destroy(&server->scene->tree.node);
	wlr_xcursor_manager_destroy(server->cursor_mgr);
	wlr_cursor_destroy(server->cursor);
	wlr_allocator_destroy(server->allocator);
	wlr_renderer_destroy(server->renderer);
	wlr_backend_destroy(server->backend);
	wl_display_destroy(server->wl_display);

	shady_modules_release_states(server);
	shady_events_finish(server);
	shady_modules_close_plugins(server);
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);
	char *startup_cmd = NULL;
	char *config_path = NULL;
	char *legacy_config_path = NULL;
	bool safe_mode = false;
	bool native_mode = false;

	int c;
	static const struct option long_options[] = {
		{ "safe", no_argument, NULL, 'S' },
		{ "native", no_argument, NULL, 'N' },
		{ "legacy-config", required_argument, NULL, 'L' },
		{ 0, 0, 0, 0 },
	};
	while ((c = getopt_long(argc, argv, "s:c:hSNL:", long_options, NULL)) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		case 'c':
			config_path = optarg;
			break;
		case 'S':
			safe_mode = true;
			break;
		case 'N':
			native_mode = true;
			break;
		case 'L':
			legacy_config_path = optarg;
			break;
		default:
			printf("Usage: %s [-s startup command] [-c config.lua] [--legacy-config path] [--safe] [--native]\n", argv[0]);
			return 0;
		}
	}
	if (optind < argc) {
		printf("Usage: %s [-s startup command] [-c config.lua] [--legacy-config path] [--safe] [--native]\n", argv[0]);
		return 0;
	}

	if (native_mode) {
		unsetenv("WAYLAND_DISPLAY");
		unsetenv("WAYLAND_SOCKET");
		unsetenv("DISPLAY");
		setenv("WLR_BACKENDS", "drm,libinput", 1);
		if (!getenv("XDG_RUNTIME_DIR") || !*getenv("XDG_RUNTIME_DIR")) {
			fprintf(stderr,
				"Shady native mode requires XDG_RUNTIME_DIR (normally provided by a login session).\n");
			return 1;
		}
		wlr_log(WLR_INFO, "native mode requested: WLR_BACKENDS=drm,libinput");
	}

	struct shady_server server = {0};
	wl_list_init(&server.outputs);
	wl_list_init(&server.toplevels);
	wl_list_init(&server.all_toplevels);
	wl_list_init(&server.popups);
	wl_list_init(&server.keyboards);
	shady_events_init(&server);
	shady_config_defaults(&server.config);
	shady_modules_init(&server.modules);
	if (!shady_register_builtin_modules(&server)) {
		shady_events_finish(&server);
		shady_modules_close_plugins(&server);
		return 1;
	}
	char config_buf[4096];
	if (!config_path) {
		default_config_path(config_buf, sizeof(config_buf));
		config_path = config_buf;
	}
	if (legacy_config_path) {
		shady_config_load(&server.config, legacy_config_path);
	}
	if (!shady_config_lua_load(&server, config_path)) {
		return 1;
	}
	if (safe_mode) {
		server.config.spatial_mode = false;
		shady_modules_set_enabled(&server.modules, "spatial", false);
		shady_modules_set_enabled(&server.modules, "window-motion", false);
		shady_modules_set_enabled(&server.modules, "obj-loader", false);
		shady_modules_set_enabled(&server.modules, "physics", false);
		shady_modules_set_enabled(&server.modules, "fps", false);
		shady_modules_set_enabled(&server.modules, "close-animation", false);
		shady_modules_set_enabled(&server.modules, "scene-effects", false);
		server.config.physics_enabled = false;
		server.config.window_gravity = false;
		server.config.window_wobble = false;
		server.config.window_sides = false;
		server.config.shadows = false;
		server.config.floor = false;
		server.config.close_animation = false;
		server.config.fps_mode = false;
		server.config.sky = false;
		server.config.environment = false;
	}
	server.wl_display = wl_display_create();
	if (!server.wl_display) {
		return 1;
	}
	struct wl_event_loop *loop = wl_display_get_event_loop(server.wl_display);
	struct wl_event_source *sigint = wl_event_loop_add_signal(loop,
		SIGINT, terminate_display, server.wl_display);
	struct wl_event_source *sigterm = wl_event_loop_add_signal(loop,
		SIGTERM, terminate_display, server.wl_display);
	struct wl_event_source *sigchld = wl_event_loop_add_signal(loop,
		SIGCHLD, reap_children, NULL);

	struct wlr_session *session = NULL;
	server.backend = wlr_backend_autocreate(
		wl_display_get_event_loop(server.wl_display), &session);
	if (server.backend == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		if (native_mode) {
			fprintf(stderr,
				"Native backend setup failed. Run Shady from a real VT/TTY login session and ensure libseat can acquire the seat.\n"
				"On seatd systems, start/enable seatd or try: seatd-launch ./build/shady --native\n"
				"On logind systems, make sure this process belongs to an active local login session.\n");
		}
		return 1;
	}
	if (session) {
		wlr_log(WLR_INFO, "session acquired: seat=%s active=%s",
			session->seat, session->active ? "yes" : "no");
	} else if (native_mode) {
		wlr_log(WLR_ERROR, "native mode did not acquire a wlroots session");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	server.renderer = wlr_renderer_autocreate(server.backend);
	if (server.renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_renderer");
		if (native_mode) fprintf(stderr,
			"Native renderer setup failed. Check DRM/GBM driver availability and WLR_DRM_DEVICES/WLR_RENDERER overrides.\n");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}
#if SHADY_HAS_SPATIAL
	if (server.config.spatial_mode && !wlr_renderer_is_gles2(server.renderer)) {
		wlr_log(WLR_ERROR,
			"the spatial module requires the GLES2 renderer (unset WLR_RENDERER=pixman)");
		wlr_renderer_destroy(server.renderer);
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}
#endif

	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	server.allocator = wlr_allocator_autocreate(server.backend,
		server.renderer);
	if (server.allocator == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		wlr_renderer_destroy(server.renderer);
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	server.output_layout = wlr_output_layout_create(server.wl_display);

	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.scene = wlr_scene_create();
	server.content_tree = wlr_scene_tree_create(&server.scene->tree);
	server.overlay_tree = wlr_scene_tree_create(&server.scene->tree);
	server.scene_layout = wlr_scene_attach_output_layout(server.scene,
		server.output_layout);

	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
	server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
	server.new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	server.cursor_mode = SHADY_CURSOR_PASSTHROUGH;
	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute,
			&server.cursor_motion_absolute);
	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);
	server.seat = wlr_seat_create(server.wl_display, "seat0");
	shady_input_update_seat_capabilities(&server);
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor,
			&server.request_cursor);
	server.pointer_focus_change.notify = seat_pointer_focus_change;
	wl_signal_add(&server.seat->pointer_state.events.focus_change,
			&server.pointer_focus_change);
	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection,
			&server.request_set_selection);
	server.request_set_primary_selection.notify = seat_request_set_primary_selection;
	wl_signal_add(&server.seat->events.request_set_primary_selection,
			&server.request_set_primary_selection);

	if (!shady_modules_initialize_all(&server)) {
		wlr_log(WLR_ERROR, "failed to initialize modules");
		cleanup_server_after_setup(&server, sigint, sigterm, sigchld);
		return 1;
	}

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wlr_log(WLR_ERROR, "failed to allocate Wayland socket in XDG_RUNTIME_DIR");
		cleanup_server_after_setup(&server, sigint, sigterm, sigchld);
		return 1;
	}

	if (!wlr_backend_start(server.backend)) {
		wlr_log(WLR_ERROR, "failed to start wlroots backend");
		if (native_mode) fprintf(stderr,
			"Native backend start failed after acquiring the session. Check DRM master availability, GPU driver support, and whether another compositor owns this VT/GPU.\n");
		cleanup_server_after_setup(&server, sigint, sigterm, sigchld);
		return 1;
	}

	if (native_mode) {
		printf("Shady native session is running on WAYLAND_DISPLAY=%s\n", socket);
	} else {
		printf("Shady is listening on WAYLAND_DISPLAY=%s\n"
			"Launch a client from another dev-shell terminal:\n"
			"  WAYLAND_DISPLAY=%s foot\n", socket, socket);
	}
	fflush(stdout);
	if (startup_cmd) {
		pid_t pid = fork();
		if (pid == 0) {
			if (setenv("WAYLAND_DISPLAY", socket, true) < 0) {
				perror("setenv");
				_exit(1);
			}
			unsetenv("WAYLAND_SOCKET");
			execlp("sh", "sh", "-c", startup_cmd, (void *)NULL);
			perror("exec startup command");
			_exit(127);
		} else if (pid < 0) {
			perror("fork");
		}
	}

	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
			socket);
	shady_modules_start_all(&server);
	wl_display_run(server.wl_display);
	shady_modules_stop_all(&server);

	wl_display_destroy_clients(server.wl_display);

	wl_list_remove(&server.new_xdg_toplevel.link);
	wl_list_remove(&server.new_xdg_popup.link);
	wl_list_remove(&server.cursor_motion.link);
	wl_list_remove(&server.cursor_motion_absolute.link);
	wl_list_remove(&server.cursor_button.link);
	wl_list_remove(&server.cursor_axis.link);
	wl_list_remove(&server.cursor_frame.link);

	wl_list_remove(&server.new_input.link);
	wl_list_remove(&server.request_cursor.link);
	wl_list_remove(&server.pointer_focus_change.link);
	wl_list_remove(&server.request_set_selection.link);
	wl_list_remove(&server.request_set_primary_selection.link);

	wl_list_remove(&server.new_output.link);

	shady_modules_destroy_all(&server);

	wl_event_source_remove(sigint);
	wl_event_source_remove(sigterm);
	wl_event_source_remove(sigchld);
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.wl_display);
	shady_modules_release_states(&server);
	shady_events_finish(&server);
	shady_modules_close_plugins(&server);
	shady_titlebar_global_fini();
	return 0;
}
