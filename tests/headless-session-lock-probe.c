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

struct probe {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_output *output;
	struct ext_session_lock_manager_v1 *manager;
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
	};
	p.display = wl_display_connect(NULL);
	if (!p.display) return 1;

	struct wl_registry *registry = wl_display_get_registry(p.display);
	wl_registry_add_listener(registry, &registry_listener, &p);
	if (wl_display_roundtrip(p.display) < 0) return 2;
	if (!p.compositor || !p.shm || !p.output || !p.manager) return 3;

	p.lock = ext_session_lock_manager_v1_lock(p.manager);
	ext_session_lock_v1_add_listener(p.lock, &lock_listener, &p);
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
	if (p.manager) ext_session_lock_manager_v1_destroy(p.manager);
	if (p.output) wl_output_destroy(p.output);
	if (p.shm) wl_shm_destroy(p.shm);
	if (p.compositor) wl_compositor_destroy(p.compositor);
	wl_registry_destroy(registry);
	wl_display_disconnect(p.display);
	return rc;
}
