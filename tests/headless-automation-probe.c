#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#define WIDTH 320
#define HEIGHT 200
#define MAX_BUFFERS 64

struct probe {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct wl_pointer *pointer;
	struct xdg_wm_base *wm_base;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wl_buffer *buffers[MAX_BUFFERS];
	size_t buffer_count;
	bool configured;
	bool key_seen;
	bool click_seen;
	bool drag_seen;
	bool scroll_seen;
	bool pointer_down;
	uint32_t base_color;
	bool pattern;
	const char *app_id;
	const char *status_path;
};

static void status_once(struct probe *p, bool *flag, const char *line) {
	if (*flag) return;
	*flag = true;
	if (!p->status_path || !*p->status_path) return;
	FILE *f = fopen(p->status_path, "a");
	if (!f) return;
	fprintf(f, "%s\n", line);
	fclose(f);
}

static void draw(struct probe *p) {
	if (!p->configured || !p->shm || p->buffer_count >= MAX_BUFFERS) return;
	char name[] = "/tmp/shady-probe-XXXXXX";
	int fd = mkstemp(name);
	if (fd < 0) return;
	unlink(name);
	size_t size = WIDTH * HEIGHT * 4u;
	if (ftruncate(fd, (off_t)size) != 0) { close(fd); return; }
	uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED) { close(fd); return; }

	for (int y = 0; y < HEIGHT; y++) {
		for (int x = 0; x < WIDTH; x++) {
			uint32_t c = p->base_color;
			if (p->pattern) {
				int cell = ((x / 24) + (y / 24)) & 1;
				c = cell ? 0xff1f6f8bu : 0xffd7eef7u;
				if ((x % 80) < 4 || (y % 64) < 4) c = 0xff163447u;
			}
			if (p->key_seen && y < HEIGHT / 2) c = 0xff2e8b57u;
			if (p->click_seen && x < 48) c = 0xffb33a3au;
			if (p->scroll_seen && x >= WIDTH - 48) c = 0xff3a64b3u;
			if (p->drag_seen && x >= 96 && x < 224 && y >= 76 && y < 124)
				c = 0xffd4a017u;
			pixels[y * WIDTH + x] = c;
		}
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(p->shm, fd, (int)size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
		WIDTH, HEIGHT, WIDTH * 4, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	munmap(pixels, size);
	if (!buffer) return;
	p->buffers[p->buffer_count++] = buffer;
	wl_surface_attach(p->surface, buffer, 0, 0);
	wl_surface_damage_buffer(p->surface, 0, 0, WIDTH, HEIGHT);
	wl_surface_commit(p->surface);
}

static void wm_ping(void *data, struct xdg_wm_base *wm, uint32_t serial) {
	(void)data;
	xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
		uint32_t serial) {
	struct probe *p = data;
	xdg_surface_ack_configure(surface, serial);
	p->configured = true;
	draw(p);
}
static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
		int32_t width, int32_t height, struct wl_array *states) {
	(void)data; (void)toplevel; (void)width; (void)height; (void)states;
}
static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
	(void)toplevel;
	struct probe *p = data;
	wl_display_disconnect(p->display);
	exit(0);
}
static void toplevel_bounds(void *data, struct xdg_toplevel *toplevel,
		int32_t width, int32_t height) {
	(void)data; (void)toplevel; (void)width; (void)height;
}
static void toplevel_wm_capabilities(void *data, struct xdg_toplevel *toplevel,
		struct wl_array *capabilities) {
	(void)data; (void)toplevel; (void)capabilities;
}
static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
	.configure_bounds = toplevel_bounds,
	.wm_capabilities = toplevel_wm_capabilities,
};

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
		uint32_t format, int32_t fd, uint32_t size) {
	(void)data; (void)keyboard; (void)format; (void)size;
	close(fd);
}
static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	(void)data; (void)keyboard; (void)serial; (void)surface; (void)keys;
}
static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, struct wl_surface *surface) {
	(void)data; (void)keyboard; (void)serial; (void)surface;
}
static void keyboard_key(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
	(void)keyboard; (void)serial; (void)time; (void)key;
	struct probe *p = data;
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		status_once(p, &p->key_seen, "KEY=PASS");
		draw(p);
	}
}
static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, uint32_t depressed, uint32_t latched,
		uint32_t locked, uint32_t group) {
	(void)data; (void)keyboard; (void)serial; (void)depressed;
	(void)latched; (void)locked; (void)group;
}
static void keyboard_repeat(void *data, struct wl_keyboard *keyboard,
		int32_t rate, int32_t delay) {
	(void)data; (void)keyboard; (void)rate; (void)delay;
}
static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat,
};

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
		struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	(void)data; (void)pointer; (void)serial; (void)surface; (void)sx; (void)sy;
}
static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
		struct wl_surface *surface) {
	(void)data; (void)pointer; (void)serial; (void)surface;
}
static void pointer_motion(void *data, struct wl_pointer *pointer,
		uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
	(void)pointer; (void)time; (void)sx; (void)sy;
	struct probe *p = data;
	if (p->pointer_down) {
		status_once(p, &p->drag_seen, "DRAG=PASS");
		draw(p);
	}
}
static void pointer_button(void *data, struct wl_pointer *pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	(void)pointer; (void)serial; (void)time; (void)button;
	struct probe *p = data;
	p->pointer_down = state == WL_POINTER_BUTTON_STATE_PRESSED;
	if (p->pointer_down) {
		status_once(p, &p->click_seen, "CLICK=PASS");
		draw(p);
	}
}
static void pointer_axis(void *data, struct wl_pointer *pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	(void)pointer; (void)time; (void)axis; (void)value;
	struct probe *p = data;
	status_once(p, &p->scroll_seen, "SCROLL=PASS");
	draw(p);
}
static void pointer_frame(void *data, struct wl_pointer *pointer) {
	(void)data; (void)pointer;
}
static void pointer_axis_source(void *data, struct wl_pointer *pointer,
		uint32_t source) {
	(void)data; (void)pointer; (void)source;
}
static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
		uint32_t time, uint32_t axis) {
	(void)data; (void)pointer; (void)time; (void)axis;
}
static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
		uint32_t axis, int32_t discrete) {
	(void)data; (void)pointer; (void)axis; (void)discrete;
}
static void pointer_axis_value120(void *data, struct wl_pointer *pointer,
		uint32_t axis, int32_t value120) {
	(void)data; (void)pointer; (void)axis; (void)value120;
}
static void pointer_axis_relative_direction(void *data,
		struct wl_pointer *pointer, uint32_t axis, uint32_t direction) {
	(void)data; (void)pointer; (void)axis; (void)direction;
}
static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
	.axis_value120 = pointer_axis_value120,
	.axis_relative_direction = pointer_axis_relative_direction,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
		uint32_t caps) {
	struct probe *p = data;
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !p->keyboard) {
		p->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(p->keyboard, &keyboard_listener, p);
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !p->pointer) {
		p->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(p->pointer, &pointer_listener, p);
	}
}
static void seat_name(void *data, struct wl_seat *seat, const char *name) {
	(void)data; (void)seat; (void)name;
}
static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

static void registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct probe *p = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		p->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, version < 6 ? version : 6);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		p->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		p->seat = wl_registry_bind(registry, name, &wl_seat_interface,
			version < 9 ? version : 9);
		wl_seat_add_listener(p->seat, &seat_listener, p);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		p->wm_base = wl_registry_bind(registry, name,
			&xdg_wm_base_interface, version < 6 ? version : 6);
		xdg_wm_base_add_listener(p->wm_base, &wm_listener, p);
	}
}
static void registry_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	(void)data; (void)registry; (void)name;
}
static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_remove,
};

int main(void) {
	const char *app_id = getenv("SHADY_AUTOMATION_PROBE_APP_ID");
	const char *color = getenv("SHADY_AUTOMATION_PROBE_COLOR");
	struct probe p = {
		.status_path = getenv("SHADY_AUTOMATION_PROBE_STATUS"),
		.app_id = app_id && *app_id ? app_id : "shady-automation-probe",
		.base_color = color && *color ? (uint32_t)strtoul(color, NULL, 0) : 0xff20242au,
		.pattern = getenv("SHADY_AUTOMATION_PROBE_PATTERN") != NULL,
	};
	p.display = wl_display_connect(NULL);
	if (!p.display) return 1;
	struct wl_registry *registry = wl_display_get_registry(p.display);
	wl_registry_add_listener(registry, &registry_listener, &p);
	wl_display_roundtrip(p.display);
	if (!p.compositor || !p.shm || !p.wm_base || !p.seat) return 2;

	p.surface = wl_compositor_create_surface(p.compositor);
	p.xdg_surface = xdg_wm_base_get_xdg_surface(p.wm_base, p.surface);
	xdg_surface_add_listener(p.xdg_surface, &xdg_surface_listener, &p);
	p.toplevel = xdg_surface_get_toplevel(p.xdg_surface);
	xdg_toplevel_add_listener(p.toplevel, &toplevel_listener, &p);
	xdg_toplevel_set_app_id(p.toplevel, p.app_id);
	const char *title = getenv("SHADY_AUTOMATION_PROBE_TITLE");
	xdg_toplevel_set_title(p.toplevel,
		title && *title ? title : "Shady Automation Probe");
	xdg_toplevel_set_min_size(p.toplevel, WIDTH, HEIGHT);
	xdg_toplevel_set_max_size(p.toplevel, WIDTH, HEIGHT);
	wl_surface_commit(p.surface);

	while (wl_display_dispatch(p.display) != -1) {}
	for (size_t i = 0; i < p.buffer_count; i++) wl_buffer_destroy(p.buffers[i]);
	return 0;
}
