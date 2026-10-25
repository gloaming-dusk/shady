#include "idle.h"

#include <stdlib.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "shady.h"
#include "modules/desktop/state.h"
#include "render/render.h"

#define MAX_TIMEOUTS 8

struct idle_timeout {
	bool used;
	bool fired;
	uint32_t timeout_ms;
	struct wl_event_source *timer;
	shady_idle_fn on_idle, on_resume;
	void *data;
	struct shady_idle *idle;
};

struct idle_inhibitor {
	struct wl_list link;
	struct shady_idle *idle;
	struct wlr_idle_inhibitor_v1 *inhibitor;
	struct wl_listener destroy;
	/* Only a mapped surface inhibits: follow it on and off screen. */
	struct wl_listener map;
	struct wl_listener unmap;
};

struct shady_idle {
	struct shady_server *server;
	struct wlr_idle_inhibit_manager_v1 *manager;
	struct wl_listener new_inhibitor;
	struct wl_list inhibitors;
	uint64_t last_activity_ms;
	/* Held off by the session itself (a "caffeine" toggle). */
	bool manual_inhibit;
	/* Outputs turned off: any input turns them back on. */
	bool screen_off;
	/* The release of the key that turned the screen off is not a wake-up. */
	uint64_t wake_after_ms;
	struct idle_timeout timeouts[MAX_TIMEOUTS];
};

static uint64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

bool shady_idle_inhibited(struct shady_server *server) {
	struct shady_idle *idle = server ? server->idle : NULL;
	if (!idle) return false;
	if (idle->manual_inhibit) return true;
	struct idle_inhibitor *it;
	wl_list_for_each(it, &idle->inhibitors, link) {
		struct wlr_surface *surface = it->inhibitor->surface;
		if (surface && surface->mapped) return true;
	}
	return false;
}

static void update_inhibited(struct shady_idle *idle) {
	struct shady_desktop_state *desktop = shady_desktop_state(idle->server);
	if (desktop && desktop->idle_notifier)
		wlr_idle_notifier_v1_set_inhibited(desktop->idle_notifier,
			shady_idle_inhibited(idle->server));
}

static void inhibitor_remove(struct idle_inhibitor *it) {
	wl_list_remove(&it->link);
	wl_list_remove(&it->destroy.link);
	wl_list_remove(&it->map.link);
	wl_list_remove(&it->unmap.link);
	free(it);
}

static void inhibitor_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct idle_inhibitor *it = wl_container_of(listener, it, map);
	update_inhibited(it->idle);
}

static void inhibitor_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct idle_inhibitor *it = wl_container_of(listener, it, unmap);
	update_inhibited(it->idle);
	/* Idle time starts again from when the video went away. */
	it->idle->last_activity_ms = now_ms();
}

static void inhibitor_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct idle_inhibitor *it = wl_container_of(listener, it, destroy);
	struct shady_idle *idle = it->idle;
	inhibitor_remove(it);
	update_inhibited(idle);
	/* Idle time starts again from when the video stopped. */
	idle->last_activity_ms = now_ms();
}

static void new_inhibitor(struct wl_listener *listener, void *data) {
	struct shady_idle *idle = wl_container_of(listener, idle, new_inhibitor);
	struct wlr_idle_inhibitor_v1 *inhibitor = data;
	struct idle_inhibitor *it = calloc(1, sizeof(*it));
	if (!it) return;
	it->idle = idle;
	it->inhibitor = inhibitor;
	it->destroy.notify = inhibitor_destroy;
	wl_signal_add(&inhibitor->events.destroy, &it->destroy);
	it->map.notify = inhibitor_map;
	wl_signal_add(&inhibitor->surface->events.map, &it->map);
	it->unmap.notify = inhibitor_unmap;
	wl_signal_add(&inhibitor->surface->events.unmap, &it->unmap);
	wl_list_insert(&idle->inhibitors, &it->link);
	update_inhibited(idle);
}

static int timeout_fire(void *data) {
	struct idle_timeout *t = data;
	struct shady_idle *idle = t->idle;
	if (!t->used || t->fired) return 0;
	uint64_t idle_for = now_ms() - idle->last_activity_ms;
	if (shady_idle_inhibited(idle->server)) {
		wl_event_source_timer_update(t->timer, (int)t->timeout_ms);
		return 0;
	}
	if (idle_for + 5 < t->timeout_ms) {
		wl_event_source_timer_update(t->timer, (int)(t->timeout_ms - idle_for));
		return 0;
	}
	t->fired = true;
	if (t->on_idle) t->on_idle(t->data);
	return 0;
}

bool shady_idle_init(struct shady_server *server) {
	struct shady_idle *idle = calloc(1, sizeof(*idle));
	if (!idle) return false;
	idle->server = server;
	wl_list_init(&idle->inhibitors);
	idle->last_activity_ms = now_ms();
	idle->manager = wlr_idle_inhibit_v1_create(server->wl_display);
	if (!idle->manager) {
		free(idle);
		return false;
	}
	idle->new_inhibitor.notify = new_inhibitor;
	wl_signal_add(&idle->manager->events.new_inhibitor, &idle->new_inhibitor);
	server->idle = idle;
	return true;
}

void shady_idle_finish(struct shady_server *server) {
	struct shady_idle *idle = server->idle;
	if (!idle) return;
	for (size_t i = 0; i < MAX_TIMEOUTS; i++)
		if (idle->timeouts[i].timer) wl_event_source_remove(idle->timeouts[i].timer);
	struct idle_inhibitor *it, *tmp;
	wl_list_for_each_safe(it, tmp, &idle->inhibitors, link)
		inhibitor_remove(it);
	wl_list_remove(&idle->new_inhibitor.link);
	free(idle);
	server->idle = NULL;
}

void shady_idle_activity(struct shady_server *server) {
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	if (desktop && desktop->idle_notifier)
		wlr_idle_notifier_v1_notify_activity(desktop->idle_notifier, server->seat);
	struct shady_idle *idle = server->idle;
	if (!idle) return;
	idle->last_activity_ms = now_ms();
	if (idle->screen_off && idle->last_activity_ms >= idle->wake_after_ms)
		shady_idle_output_power(server, true);
	for (size_t i = 0; i < MAX_TIMEOUTS; i++) {
		struct idle_timeout *t = &idle->timeouts[i];
		if (!t->used || !t->fired) continue;
		t->fired = false;
		wl_event_source_timer_update(t->timer, (int)t->timeout_ms);
		if (t->on_resume) t->on_resume(t->data);
	}
}

int shady_idle_add(struct shady_server *server, uint32_t timeout_ms,
		shady_idle_fn on_idle, shady_idle_fn on_resume, void *data) {
	struct shady_idle *idle = server->idle;
	if (!idle || timeout_ms == 0) return 0;
	for (size_t i = 0; i < MAX_TIMEOUTS; i++) {
		struct idle_timeout *t = &idle->timeouts[i];
		if (t->used) continue;
		if (!t->timer) {
			t->timer = wl_event_loop_add_timer(
				wl_display_get_event_loop(server->wl_display), timeout_fire, t);
			if (!t->timer) return 0;
		}
		t->used = true;
		t->fired = false;
		t->timeout_ms = timeout_ms;
		t->on_idle = on_idle;
		t->on_resume = on_resume;
		t->data = data;
		t->idle = idle;
		wl_event_source_timer_update(t->timer, (int)timeout_ms);
		return (int)i + 1;
	}
	return 0;
}

void shady_idle_clear(struct shady_server *server, shady_idle_fn release) {
	struct shady_idle *idle = server->idle;
	if (!idle) return;
	for (size_t i = 0; i < MAX_TIMEOUTS; i++) {
		struct idle_timeout *t = &idle->timeouts[i];
		if (!t->used) continue;
		wl_event_source_timer_update(t->timer, 0);
		if (release) release(t->data);
		t->used = false;
		t->data = NULL;
	}
}

void shady_idle_output_power(struct shady_server *server, bool on) {
	if (server->idle) {
		server->idle->screen_off = !on;
		if (!on) server->idle->wake_after_ms = now_ms() + 600;
	}
	struct shady_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_output *wlr_output = output->wlr_output;
		if (wlr_output->enabled == on) continue;
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, on);
		if (!wlr_output_commit_state(wlr_output, &state))
			wlr_log(WLR_ERROR, "idle: could not turn %s %s", wlr_output->name, on ? "on" : "off");
		wlr_output_state_finish(&state);
	}
	if (on) shady_render_schedule_all_outputs(server);
}

void shady_idle_set_inhibit(struct shady_server *server, bool inhibit) {
	struct shady_idle *idle = server->idle;
	if (!idle) return;
	idle->manual_inhibit = inhibit;
	idle->last_activity_ms = now_ms();
	update_inhibited(idle);
}
