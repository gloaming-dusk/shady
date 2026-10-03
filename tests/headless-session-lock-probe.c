#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "ext-session-lock-v1-client-protocol.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"

struct probe;

struct output_head {
	struct probe *probe;
	struct zwlr_output_head_v1 *head;
	char name[128];
	bool enabled;
	bool finished;
};

struct probe {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_output *output;
	struct ext_session_lock_manager_v1 *manager;
	struct zwlr_output_manager_v1 *output_manager;
	struct output_head heads[16];
	size_t head_count;
	uint32_t output_serial;
	bool output_manager_done;
	struct zwlr_output_configuration_v1 *output_config;
	bool output_config_done;
	bool output_config_success;
	struct ext_session_lock_v1 *lock;
	struct ext_session_lock_surface_v1 *lock_surface;
	struct wl_surface *surface;
	struct wl_buffer *buffer;
	const char *status_path;
	bool locked;
	bool configured;
	bool finished;
	uint64_t locked_at_ms;
	uint32_t hold_ms;
	bool exit_locked;
	const char *disable_output;
	bool disable_before_locked;
};

static uint64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void status(struct probe *p, const char *line) {
	if (!p->status_path || !*p->status_path) return;
	FILE *f = fopen(p->status_path, "a");
	if (!f) return;
	fprintf(f, "%s\n", line);
	fclose(f);
}

static struct wl_buffer *make_buffer(struct probe *p, uint32_t width,
		uint32_t height) {
	char name[] = "/tmp/shady-lock-probe-XXXXXX";
	int fd = mkstemp(name);
	if (fd < 0) return NULL;
	unlink(name);

	size_t stride = (size_t)width * 4u;
	size_t size = stride * height;
	if (ftruncate(fd, (off_t)size) != 0) {
		close(fd);
		return NULL;
	}
	uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED) {
		close(fd);
		return NULL;
	}
	for (size_t i = 0; i < (size_t)width * height; ++i) {
		pixels[i] = 0xff101820u;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(p->shm, fd, (int)size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
		(int)width, (int)height, (int)stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	munmap(pixels, size);
	close(fd);
	return buffer;
}

static void lock_locked(void *data, struct ext_session_lock_v1 *lock) {
	(void)lock;
	struct probe *p = data;
	p->locked = true;
	p->locked_at_ms = now_ms();
	status(p, "LOCKED=PASS");
	if (p->disable_output && *p->disable_output && !p->output_config_done)
		status(p, "OUTPUT_DISABLED_BEFORE_LOCKED=FAIL");
}

static void lock_finished(void *data, struct ext_session_lock_v1 *lock) {
	(void)lock;
	struct probe *p = data;
	p->finished = true;
	status(p, "FINISHED=FAIL");
}

static const struct ext_session_lock_v1_listener lock_listener = {
	.locked = lock_locked,
	.finished = lock_finished,
};

static void lock_surface_configure(void *data,
		struct ext_session_lock_surface_v1 *lock_surface,
		uint32_t serial, uint32_t width, uint32_t height) {
	struct probe *p = data;
	ext_session_lock_surface_v1_ack_configure(lock_surface, serial);
	if (p->buffer) {
		wl_buffer_destroy(p->buffer);
		p->buffer = NULL;
	}
	p->buffer = make_buffer(p, width, height);
	if (!p->buffer) return;
	wl_surface_attach(p->surface, p->buffer, 0, 0);
	wl_surface_damage_buffer(p->surface, 0, 0, (int32_t)width, (int32_t)height);
	wl_surface_commit(p->surface);
	if (!p->configured) {
		p->configured = true;
		status(p, "SURFACE_CONFIGURED=PASS");
	}
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
	.configure = lock_surface_configure,
};

static void head_name(void *data, struct zwlr_output_head_v1 *head,
		const char *name) {
	(void)head;
	struct output_head *state = data;
	snprintf(state->name, sizeof(state->name), "%s", name ? name : "");
}
static void head_description(void *data, struct zwlr_output_head_v1 *head,
		const char *description) {(void)data;(void)head;(void)description;}
static void head_physical_size(void *data, struct zwlr_output_head_v1 *head,
		int32_t width, int32_t height) {(void)data;(void)head;(void)width;(void)height;}
static void head_mode(void *data, struct zwlr_output_head_v1 *head,
		struct zwlr_output_mode_v1 *mode) {(void)data;(void)head;(void)mode;}
static void head_enabled(void *data, struct zwlr_output_head_v1 *head,
		int32_t enabled) {(void)head;((struct output_head *)data)->enabled = enabled != 0;}
static void head_current_mode(void *data, struct zwlr_output_head_v1 *head,
		struct zwlr_output_mode_v1 *mode) {(void)data;(void)head;(void)mode;}
static void head_position(void *data, struct zwlr_output_head_v1 *head,
		int32_t x, int32_t y) {(void)data;(void)head;(void)x;(void)y;}
static void head_transform(void *data, struct zwlr_output_head_v1 *head,
		int32_t transform) {(void)data;(void)head;(void)transform;}
static void head_scale(void *data, struct zwlr_output_head_v1 *head,
		wl_fixed_t scale) {(void)data;(void)head;(void)scale;}
static void head_finished(void *data, struct zwlr_output_head_v1 *head) {
	(void)head; ((struct output_head *)data)->finished = true;
}
static void head_make(void *data, struct zwlr_output_head_v1 *head,
		const char *make) {(void)data;(void)head;(void)make;}
static void head_model(void *data, struct zwlr_output_head_v1 *head,
		const char *model) {(void)data;(void)head;(void)model;}
static void head_serial_number(void *data, struct zwlr_output_head_v1 *head,
		const char *serial_number) {(void)data;(void)head;(void)serial_number;}
static void head_adaptive_sync(void *data, struct zwlr_output_head_v1 *head,
		uint32_t state) {(void)data;(void)head;(void)state;}

static const struct zwlr_output_head_v1_listener output_head_listener = {
	.name = head_name,
	.description = head_description,
	.physical_size = head_physical_size,
	.mode = head_mode,
	.enabled = head_enabled,
	.current_mode = head_current_mode,
	.position = head_position,
	.transform = head_transform,
	.scale = head_scale,
	.finished = head_finished,
	.make = head_make,
	.model = head_model,
	.serial_number = head_serial_number,
	.adaptive_sync = head_adaptive_sync,
};

static void output_manager_head(void *data, struct zwlr_output_manager_v1 *manager,
		struct zwlr_output_head_v1 *head) {
	(void)manager;
	struct probe *p = data;
	if (p->head_count >= sizeof(p->heads) / sizeof(p->heads[0])) return;
	struct output_head *state = &p->heads[p->head_count++];
	state->probe = p;
	state->head = head;
	zwlr_output_head_v1_add_listener(head, &output_head_listener, state);
}
static void output_manager_done(void *data, struct zwlr_output_manager_v1 *manager,
		uint32_t serial) {
	(void)manager;
	struct probe *p = data;
	p->output_serial = serial;
	p->output_manager_done = true;
}
static void output_manager_finished(void *data,
		struct zwlr_output_manager_v1 *manager) {(void)data;(void)manager;}
static const struct zwlr_output_manager_v1_listener output_manager_listener = {
	.head = output_manager_head,
	.done = output_manager_done,
	.finished = output_manager_finished,
};

static void output_config_succeeded(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct probe *p = data;
	p->output_config_done = true;
	p->output_config_success = true;
	p->disable_before_locked = !p->locked;
	status(p, "OUTPUT_DISABLE_RACE=PASS");
	status(p, p->disable_before_locked ?
		"OUTPUT_DISABLED_BEFORE_LOCKED=PASS" :
		"OUTPUT_DISABLED_BEFORE_LOCKED=FAIL");
	zwlr_output_configuration_v1_destroy(config);
	p->output_config = NULL;
}
static void output_config_failed(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct probe *p = data;
	p->output_config_done = true;
	status(p, "OUTPUT_DISABLE_RACE=FAIL");
	zwlr_output_configuration_v1_destroy(config);
	p->output_config = NULL;
}
static void output_config_cancelled(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct probe *p = data;
	p->output_config_done = true;
	status(p, "OUTPUT_DISABLE_RACE=FAIL");
	zwlr_output_configuration_v1_destroy(config);
	p->output_config = NULL;
}
static const struct zwlr_output_configuration_v1_listener output_config_listener = {
	.succeeded = output_config_succeeded,
	.failed = output_config_failed,
	.cancelled = output_config_cancelled,
};

static bool request_output_disable(struct probe *p) {
	if (!p || !p->output_manager || !p->output_manager_done) return false;
	p->output_config = zwlr_output_manager_v1_create_configuration(
		p->output_manager, p->output_serial);
	if (!p->output_config) return false;
	zwlr_output_configuration_v1_add_listener(
		p->output_config, &output_config_listener, p);

	bool found = false;
	for (size_t i = 0; i < p->head_count; ++i) {
		struct output_head *head = &p->heads[i];
		if (!head->head || head->finished) continue;
		if (strcmp(head->name, p->disable_output) == 0) {
			zwlr_output_configuration_v1_disable_head(p->output_config, head->head);
			found = true;
		} else if (head->enabled) {
			(void)zwlr_output_configuration_v1_enable_head(
				p->output_config, head->head);
		} else {
			zwlr_output_configuration_v1_disable_head(p->output_config, head->head);
		}
	}
	if (!found) return false;
	zwlr_output_configuration_v1_apply(p->output_config);
	return true;
}

static void registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct probe *p = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		p->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, version < 6 ? version : 6);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		p->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0 && !p->output) {
		p->output = wl_registry_bind(registry, name, &wl_output_interface,
			version < 4 ? version : 4);
	} else if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
		p->manager = wl_registry_bind(registry, name,
			&ext_session_lock_manager_v1_interface, 1);
	} else if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
		p->output_manager = wl_registry_bind(registry, name,
			&zwlr_output_manager_v1_interface, version < 4 ? version : 4);
		zwlr_output_manager_v1_add_listener(
			p->output_manager, &output_manager_listener, p);
	}
}

static void registry_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_remove,
};

static int dispatch_until_unlock(struct probe *p) {
	int fd = wl_display_get_fd(p->display);
	for (;;) {
		if (p->finished) return 5;
		if (p->locked && p->configured &&
				(!p->disable_output || !*p->disable_output || p->output_config_done) &&
				now_ms() - p->locked_at_ms >= p->hold_ms) {
			if (p->exit_locked) {
				status(p, "CLIENT_EXIT=PASS");
				return 0;
			}
			ext_session_lock_v1_unlock_and_destroy(p->lock);
			p->lock = NULL;
			if (wl_display_roundtrip(p->display) < 0) return 6;
			status(p, "UNLOCKED=PASS");
			return 0;
		}

		if (wl_display_dispatch_pending(p->display) < 0) return 7;
		if (wl_display_flush(p->display) < 0 && errno != EAGAIN) return 8;
		struct pollfd pollfd = { .fd = fd, .events = POLLIN };
		int rc = poll(&pollfd, 1, 25);
		if (rc < 0 && errno == EINTR) continue;
		if (rc < 0) return 9;
		if (rc > 0 && (pollfd.revents & POLLIN) &&
				wl_display_dispatch(p->display) < 0) return 10;
	}
}

int main(void) {
	const char *hold = getenv("SHADY_SESSION_LOCK_HOLD_MS");
	struct probe p = {
		.status_path = getenv("SHADY_SESSION_LOCK_STATUS"),
		.hold_ms = hold && *hold ? (uint32_t)strtoul(hold, NULL, 10) : 800u,
		.exit_locked = getenv("SHADY_SESSION_LOCK_EXIT_LOCKED") != NULL,
		.disable_output = getenv("SHADY_SESSION_LOCK_DISABLE_OUTPUT"),
	};
	p.display = wl_display_connect(NULL);
	if (!p.display) return 1;

	struct wl_registry *registry = wl_display_get_registry(p.display);
	wl_registry_add_listener(registry, &registry_listener, &p);
	if (wl_display_roundtrip(p.display) < 0) return 2;
	if (!p.compositor || !p.shm || !p.output || !p.manager) return 3;
	if (p.disable_output && *p.disable_output) {
		if (!p.output_manager || wl_display_roundtrip(p.display) < 0 ||
				!p.output_manager_done) return 14;
	}

	p.lock = ext_session_lock_manager_v1_lock(p.manager);
	ext_session_lock_v1_add_listener(p.lock, &lock_listener, &p);
	if (p.disable_output && *p.disable_output) {
		if (!request_output_disable(&p)) return 15;
	}

	p.surface = wl_compositor_create_surface(p.compositor);
	p.lock_surface = ext_session_lock_v1_get_lock_surface(
		p.lock, p.surface, p.output);
	ext_session_lock_surface_v1_add_listener(
		p.lock_surface, &lock_surface_listener, &p);
	wl_display_flush(p.display);

	int rc = dispatch_until_unlock(&p);
	if (p.lock_surface) ext_session_lock_surface_v1_destroy(p.lock_surface);
	if (p.surface) wl_surface_destroy(p.surface);
	if (p.buffer) wl_buffer_destroy(p.buffer);
	if (p.output_config) zwlr_output_configuration_v1_destroy(p.output_config);
	for (size_t i = 0; i < p.head_count; ++i) {
		if (p.heads[i].head) zwlr_output_head_v1_release(p.heads[i].head);
	}
	if (p.output_manager) zwlr_output_manager_v1_stop(p.output_manager);
	if (p.manager) ext_session_lock_manager_v1_destroy(p.manager);
	if (p.output) wl_output_destroy(p.output);
	if (p.shm) wl_shm_destroy(p.shm);
	if (p.compositor) wl_compositor_destroy(p.compositor);
	wl_registry_destroy(registry);
	wl_display_disconnect(p.display);
	return rc;
}
