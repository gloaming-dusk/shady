/*
 * rewind: the desktop remembers its layouts and can scrub back through them.
 *
 * Whenever the arrangement of windows changes and then settles (a drag
 * ends, a window maps or closes, one is maximized or sent to another
 * workspace) the plugin records a keyframe: every mapped window's position,
 * depth, workspace and maximized/fullscreen state. Super+BackSpace opens the
 * timeline. Stepping back slides the live windows to where they were; windows
 * that did not exist yet, or lived elsewhere, drift up out of view, and the
 * world drains of colour the further back you go. Return restores that moment
 * (workspaces and maximize state included), Escape returns to the present.
 *
 * Restoring is itself recorded, so a rewind can be rewound. History lives in
 * memory only; it starts empty with each session.
 *
 * Published values: rewind.active ("1" while scrubbing), rewind.frames (the
 * number of keyframes), rewind.position (the selected keyframe, 1-based) and
 * rewind.label ("3 min ago · 4 windows"), for a shell to show.
 */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <shady/event.h>
#include <shady/plugin.h>

#define REWIND_MAX_FRAMES 96
#define REWIND_MAX_WINDOWS 24
#define REWIND_TICKS 64 /* timeline marks the shader draws: 16 vec4 */
#define REWIND_SETTLE_SECONDS 0.9f
#define REWIND_ABSENT_Z -1.2f /* windows that were not there drift up and away */

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static shady_shader_program program;
static shady_render_hook_id hook;

struct rewind_entry {
	shady_window window; /* NULL once the window is gone */
	char app_id[64];
	char workspace[32];
	double x, y;
	float z;
	bool maximized;
	bool fullscreen;
};

struct rewind_frame {
	double when; /* CLOCK_MONOTONIC seconds */
	uint32_t count;
	struct rewind_entry entries[REWIND_MAX_WINDOWS];
};

struct rewind_state {
	/* History as a ring: frames[(head + i) % MAX] for i in [0, count). */
	struct rewind_frame frames[REWIND_MAX_FRAMES];
	size_t head;
	size_t count;

	/* Recording: a change waits until the layout holds still. */
	struct rewind_frame pending;
	bool dirty;
	float settle;

	/* Scrubbing. */
	bool active;
	bool animating;
	size_t selected; /* index into history, count - 1 is the present */
	float mix;       /* 0..1 fade of the past grade and timeline */
};

struct rewind_window_state {
	bool saved; /* home_* holds the window's present place */
	double home_x, home_y;
	float home_z;
};

static struct rewind_state *state(void) {
	return api->module_state(host, "rewind");
}

static struct rewind_window_state *window_state(shady_window window) {
	return api->window_state(window, "rewind");
}

static double now_seconds(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static struct rewind_frame *frame_at(struct rewind_state *s, size_t index) {
	return &s->frames[(s->head + index) % REWIND_MAX_FRAMES];
}

static void copy_string(char *dst, size_t size, const char *src) {
	snprintf(dst, size, "%s", src ? src : "");
}

/* ---- capture ------------------------------------------------------------ */

static void capture(struct rewind_frame *frame) {
	memset(frame, 0, sizeof(*frame));
	frame->when = now_seconds();
	for (size_t i = 0; i < api->window_count(host); i++) {
		if (frame->count >= REWIND_MAX_WINDOWS)
			break;
		shady_window window = api->window_at(host, i);
		if (!window || !api->window_mapped(window))
			continue;
		struct rewind_entry *e = &frame->entries[frame->count];
		if (!api->window_position(window, &e->x, &e->y, &e->z))
			continue;
		e->window = window;
		copy_string(e->app_id, sizeof(e->app_id), api->window_app_id(window));
		copy_string(e->workspace, sizeof(e->workspace), api->window_workspace(window));
		e->maximized = api->window_maximized(window);
		e->fullscreen = api->window_fullscreen(window);
		frame->count++;
	}
}

static const struct rewind_entry *find_entry(const struct rewind_frame *frame,
		shady_window window) {
	for (uint32_t i = 0; i < frame->count; i++) {
		if (frame->entries[i].window == window)
			return &frame->entries[i];
	}
	return NULL;
}

/* Depth alone does not count: focus-depth and friends move it constantly. */
static bool same_layout(const struct rewind_frame *a, const struct rewind_frame *b) {
	if (a->count != b->count)
		return false;
	for (uint32_t i = 0; i < a->count; i++) {
		const struct rewind_entry *x = &a->entries[i];
		const struct rewind_entry *y = find_entry(b, x->window);
		if (!y || !x->window)
			return false;
		if (x->maximized != y->maximized || x->fullscreen != y->fullscreen ||
				strcmp(x->workspace, y->workspace) != 0)
			return false;
		if (fabs(x->x - y->x) > 2.0 || fabs(x->y - y->y) > 2.0)
			return false;
	}
	return true;
}

static void publish_count(struct rewind_state *s) {
	if (!SHADY_API_HAS(api, publish_value))
		return;
	char text[16];
	snprintf(text, sizeof(text), "%zu", s->count);
	api->publish_value(host, "rewind.frames", text);
}

static void push_frame(struct rewind_state *s, const struct rewind_frame *frame) {
	if (s->count > 0 && same_layout(frame_at(s, s->count - 1), frame))
		return;
	if (s->count == REWIND_MAX_FRAMES) {
		s->head = (s->head + 1) % REWIND_MAX_FRAMES;
		s->count--;
	}
	*frame_at(s, s->count) = *frame;
	s->count++;
	publish_count(s);
}

/* Called every tick while not scrubbing: a frame is taken once a change has
 * held still for REWIND_SETTLE_SECONDS. */
static void record(struct rewind_state *s, float dt) {
	struct rewind_frame *last = s->count > 0 ? frame_at(s, s->count - 1) : NULL;
	capture(&s->pending);
	if (last && same_layout(last, &s->pending)) {
		s->dirty = false;
		return;
	}
	if (!s->dirty) {
		s->dirty = true;
		s->settle = 0.f;
	}
	s->settle += dt;
	if (s->settle >= REWIND_SETTLE_SECONDS || !last) {
		push_frame(s, &s->pending);
		s->dirty = false;
	} else {
		/* Ticks stop when nothing draws; keep them coming until it settles. */
		api->schedule_render(host);
	}
}

/* ---- scrubbing ---------------------------------------------------------- */

static bool previewable(shady_window window) {
	return window && api->window_valid(host, window) &&
		api->window_mapped(window) && api->window_visible(window) &&
		!api->window_maximized(window) && !api->window_fullscreen(window);
}

static void publish_position(struct rewind_state *s) {
	if (!SHADY_API_HAS(api, publish_value))
		return;
	char text[128];
	if (!s->active) {
		api->publish_value(host, "rewind.active", NULL);
		api->publish_value(host, "rewind.position", NULL);
		api->publish_value(host, "rewind.label", NULL);
		return;
	}
	api->publish_value(host, "rewind.active", "1");
	snprintf(text, sizeof(text), "%zu", s->selected + 1);
	api->publish_value(host, "rewind.position", text);

	const struct rewind_frame *frame = frame_at(s, s->selected);
	double age = now_seconds() - frame->when;
	char when[48];
	if (s->selected + 1 == s->count && age < 1.0)
		snprintf(when, sizeof(when), "now");
	else if (age < 60.0)
		snprintf(when, sizeof(when), "%d s ago", (int)age);
	else if (age < 3600.0)
		snprintf(when, sizeof(when), "%d min ago", (int)(age / 60.0));
	else
		snprintf(when, sizeof(when), "%d h ago", (int)(age / 3600.0));
	snprintf(text, sizeof(text), "%s · %u window%s", when, frame->count,
		frame->count == 1 ? "" : "s");
	api->publish_value(host, "rewind.label", text);
}

static void target_for(struct rewind_state *s, shady_window window,
		const struct rewind_window_state *ws, double *x, double *y, float *z) {
	*x = ws->home_x;
	*y = ws->home_y;
	*z = ws->home_z;
	if (!s->active)
		return;
	const struct rewind_frame *frame = frame_at(s, s->selected);
	const struct rewind_entry *e = find_entry(frame, window);
	const char *here = api->window_workspace(window);
	if (!e || strcmp(e->workspace, here ? here : "") != 0) {
		/* Out of the picture rather than behind it: windows are drawn in
		 * stacking order, so a far-away newer window would still cover an
		 * older one. */
		int width = 0, height = 0, out_w = 0, out_h = 720;
		api->window_size(window, &width, &height);
		if (api->output_count(host) > 0)
			api->output_size(api->output_at(host, 0), &out_w, &out_h);
		*y = -(double)height - out_h * 0.6;
		*z = REWIND_ABSENT_Z;
		return;
	}
	if (e->maximized || e->fullscreen)
		return; /* shown in place; Return restores the state itself */
	*x = e->x;
	*y = e->y;
	*z = e->z;
}

static void open_timeline(struct rewind_state *s) {
	/* The present is always the newest frame. */
	struct rewind_frame now;
	capture(&now);
	push_frame(s, &now);
	s->dirty = false;
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		struct rewind_window_state *ws = window ? window_state(window) : NULL;
		if (!ws)
			continue;
		ws->saved = api->window_position(window, &ws->home_x, &ws->home_y, &ws->home_z);
	}
	s->active = true;
	s->selected = s->count - 1;
	s->animating = true;
}

static void close_timeline(struct rewind_state *s) {
	s->active = false;
	s->animating = true;
	publish_position(s);
	api->schedule_render(host);
}

static void select_frame(struct rewind_state *s, long delta) {
	long next = (long)s->selected + delta;
	if (next < 0) next = 0;
	if (next >= (long)s->count) next = (long)s->count - 1;
	s->selected = (size_t)next;
	s->animating = true;
	publish_position(s);
	api->schedule_render(host);
}

/* Return: make the selected moment the present. */
static void restore(struct rewind_state *s) {
	const struct rewind_frame *frame = frame_at(s, s->selected);
	for (uint32_t i = 0; i < frame->count; i++) {
		const struct rewind_entry *e = &frame->entries[i];
		shady_window window = e->window;
		if (!window || !api->window_valid(host, window) || !api->window_mapped(window))
			continue;
		const char *ws_name = api->window_workspace(window);
		if (e->workspace[0] && strcmp(e->workspace, ws_name ? ws_name : "") != 0)
			api->window_move_to_workspace(host, window, e->workspace);
		if (api->window_fullscreen(window) != e->fullscreen)
			api->window_set_fullscreen(host, window, e->fullscreen);
		if (api->window_maximized(window) != e->maximized)
			api->window_set_maximized(host, window, e->maximized);
		struct rewind_window_state *ws = window_state(window);
		if (!ws)
			continue;
		if (e->maximized || e->fullscreen) {
			ws->saved = false;
			continue;
		}
		/* The slide home now ends at the restored place. */
		ws->home_x = e->x;
		ws->home_y = e->y;
		ws->home_z = e->z;
		ws->saved = true;
	}
	char message[96];
	snprintf(message, sizeof(message), "rewind: restored keyframe %zu of %zu",
		s->selected + 1, s->count);
	api->log(SHADY_PLUGIN_LOG_INFO, message);
	close_timeline(s);
}

static bool key(struct shady_server *server, const xkb_keysym_t *syms,
		int nsyms, uint32_t key_state, uint32_t modifiers) {
	(void)server;
	if (key_state != SHADY_KEY_PRESSED)
		return false;
	struct rewind_state *s = state();
	if (!s)
		return false;

	long step = (modifiers & SHADY_MODIFIER_SHIFT) ? 5 : 1;
	for (int i = 0; i < nsyms; i++) {
		xkb_keysym_t sym = syms[i];
		if ((modifiers & SHADY_MODIFIER_LOGO) && sym == XKB_KEY_BackSpace) {
			if (!s->active)
				open_timeline(s);
			select_frame(s, -step);
			return true;
		}
		if (!s->active)
			continue;
		switch (sym) {
		case XKB_KEY_Left:
		case XKB_KEY_BackSpace:
			select_frame(s, -step);
			return true;
		case XKB_KEY_Right:
			select_frame(s, step);
			return true;
		case XKB_KEY_Home:
			select_frame(s, -(long)s->count);
			return true;
		case XKB_KEY_End:
			select_frame(s, (long)s->count);
			return true;
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
			restore(s);
			return true;
		case XKB_KEY_Escape:
			close_timeline(s);
			return true;
		default:
			break;
		}
	}
	return false;
}

static void tick(struct shady_server *server, float dt,
		float logical_w, float logical_h) {
	(void)server;
	(void)logical_w;
	(void)logical_h;
	struct rewind_state *s = state();
	if (!s)
		return;

	float mix_target = s->active ? 1.f : 0.f;
	if (s->mix != mix_target) {
		float step = dt * 4.f;
		s->mix += s->mix < mix_target ? step : -step;
		if (s->mix < 0.f) s->mix = 0.f;
		if (s->mix > 1.f) s->mix = 1.f;
		api->schedule_render(host);
	}

	if (!s->animating) {
		if (!s->active)
			record(s, dt);
		return;
	}

	float alpha = dt * 9.0f;
	if (alpha > 1.f) alpha = 1.f;
	bool moving = false;
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (!previewable(window))
			continue;
		struct rewind_window_state *ws = window_state(window);
		if (!ws)
			continue;
		double cx, cy;
		float cz;
		if (!api->window_position(window, &cx, &cy, &cz))
			continue;
		if (!ws->saved) {
			if (!s->active)
				continue;
			/* Mapped while scrubbing: its present is where it is now. */
			ws->home_x = cx;
			ws->home_y = cy;
			ws->home_z = cz;
			ws->saved = true;
		}
		double tx, ty;
		float tz;
		target_for(s, window, ws, &tx, &ty, &tz);

		double nx = cx + (tx - cx) * alpha;
		double ny = cy + (ty - cy) * alpha;
		float nz = cz + (tz - cz) * alpha;
		/* Positions are integers in the scene graph; snap the last pixels
		 * (see spatial_overview.c). */
		if (fabs(tx - cx) <= 6.0 && fabs(ty - cy) <= 6.0 && fabsf(tz - cz) < 0.005f) {
			nx = tx;
			ny = ty;
			nz = tz;
			if (!s->active)
				ws->saved = false;
		} else {
			moving = true;
		}
		api->window_set_position(host, window, nx, ny, nz);
	}
	s->animating = moving;
	if (moving)
		api->schedule_render(host);
}

/* ---- drawing ------------------------------------------------------------ */

/* Where a frame sits on the timeline, 0 (oldest) to 1 (now). Time is
 * compressed so the last minutes stay readable next to an hour-old frame. */
static float timeline_position(struct rewind_state *s, size_t index, double now) {
	double oldest = now - frame_at(s, 0)->when;
	double age = now - frame_at(s, index)->when;
	if (index + 1 == s->count) age = 0.0;
	if (oldest < 1.0) return 1.f;
	return (float)(1.0 - log1p(age / 8.0) / log1p(oldest / 8.0));
}

static void draw(shady_host h, const struct shady_render_context *ctx, void *data) {
	(void)data;
	struct rewind_state *s = state();
	if (!s || !program || s->mix <= 0.f || s->count == 0)
		return;

	double now = now_seconds();
	/* Only the newest REWIND_TICKS frames get a mark. */
	size_t first = s->count > REWIND_TICKS ? s->count - REWIND_TICKS : 0;
	for (size_t v = 0; v < REWIND_TICKS / 4; v++) {
		float p[4];
		for (size_t k = 0; k < 4; k++) {
			size_t index = first + v * 4 + k;
			p[k] = index < s->count ? timeline_position(s, index, now) : -1.f;
		}
		char name[16];
		snprintf(name, sizeof(name), "u_ticks[%zu]", v);
		api->shader_uniform_vec4(h, program, name, p[0], p[1], p[2], p[3]);
	}
	float cursor = timeline_position(s, s->selected, now);
	api->shader_uniform_float(h, program, "u_mix", s->mix);
	api->shader_uniform_float(h, program, "u_cursor", cursor);
	api->shader_uniform_float(h, program, "u_time", ctx->time_seconds);
	bool drawn = api->shader_draw_fullscreen_scene(h, program);
	static bool reported;
	if (!reported) {
		reported = true;
		api->log(drawn ? SHADY_PLUGIN_LOG_INFO : SHADY_PLUGIN_LOG_ERROR, drawn
			? "rewind: drawing the timeline"
			: "rewind: scene capture failed; the timeline cannot be drawn");
	}
}

/* ---- lifecycle ---------------------------------------------------------- */

static void on_event(shady_host event_host, const struct shady_event *event,
		void *user_data) {
	(void)event_host;
	(void)user_data;
	struct rewind_state *s = state();
	if (!s)
		return;
	if (event->type == SHADY_EVENT_WINDOW_DESTROYED) {
		/* Keep the frame, forget the handle: it may be reused. */
		shady_window gone = event->object.window;
		for (size_t f = 0; f < s->count; f++) {
			struct rewind_frame *frame = frame_at(s, f);
			for (uint32_t i = 0; i < frame->count; i++) {
				if (frame->entries[i].window == gone)
					frame->entries[i].window = NULL;
			}
		}
	}
	if (s->active)
		s->animating = true;
	api->schedule_render(host);
}

static bool init(struct shady_server *server) {
	(void)server;
	const char *root = getenv("SHADY_ROOT");
	if (!root || !*root) root = ".";
	char vert[1024], frag[1024];
	snprintf(vert, sizeof(vert), "%s/plugins/shaders/overlay.vert", root);
	snprintf(frag, sizeof(frag), "%s/plugins/shaders/rewind.frag", root);
	program = api->shader_program_create(host, vert, frag);
	/* After windows: the scene copy sees the world and its windows. */
	hook = program ? api->render_hook_add(host, SHADY_RENDER_STAGE_AFTER_WINDOWS, draw, NULL) : 0;
	if (!program || !hook) {
		api->log(SHADY_PLUGIN_LOG_ERROR, "rewind: failed to build the timeline shader");
		if (program) api->shader_program_destroy(host, program);
		program = 0;
		return false;
	}
	return true;
}

static void start(struct shady_server *server) {
	(void)server;
	api->log(SHADY_PLUGIN_LOG_INFO,
		"rewind: Super+BackSpace scrubs back through the desktop's layouts");
}

static void destroy(struct shady_server *server) {
	(void)server;
	if (hook) api->render_hook_remove(host, hook);
	if (program) api->shader_program_destroy(host, program);
	hook = 0;
	program = 0;
}

static const char *const provides[] = {
	"spatial.rewind",
	NULL,
};

static const char *const requires[] = {
	"spatial.window-state",
	NULL,
};

static const struct shady_module module = {
	.name = "rewind",
	.provides = provides,
	.requires = requires,
	.state_size = sizeof(struct rewind_state),
	.toplevel_state_size = sizeof(struct rewind_window_state),
	.init = init,
	.start = start,
	.destroy = destroy,
	.key = key,
	.tick = tick,
};

/* Listed in the shortcut overlay (Super+Shift+/). */
static void declare_shortcuts(void) {
	if (!SHADY_API_HAS(api, declare_shortcut)) return;
	api->declare_shortcut(host, "Super+BackSpace", "Rewind the desktop's layout");
	api->declare_shortcut(host, "Left / Right", "Step through time (Shift: five steps)");
	api->declare_shortcut(host, "Home / End", "The oldest moment / now");
	api->declare_shortcut(host, "Return", "Restore that moment");
	api->declare_shortcut(host, "Escape", "Back to now");
}

const struct shady_module *shady_plugin_entry_v1(
		uint32_t host_abi,
		const struct shady_plugin_api_v1 *host_api,
		shady_host host_handle) {
	if (host_abi != SHADY_PLUGIN_ABI_V1 ||
			!host_api ||
			host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
			host_api->struct_size < sizeof(*host_api) ||
			!SHADY_API_HAS(host_api, shader_draw_fullscreen_scene))
		return NULL;

	api = host_api;
	host = host_handle;
	declare_shortcuts();
	static const uint32_t events[] = {
		SHADY_EVENT_WINDOW_MAPPED,
		SHADY_EVENT_WINDOW_UNMAPPED,
		SHADY_EVENT_WINDOW_DESTROYED,
		SHADY_EVENT_WINDOW_RESIZED,
		SHADY_EVENT_WINDOW_STATE_CHANGED,
		SHADY_EVENT_WORKSPACE_CHANGED,
	};
	for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++)
		api->subscribe_event(host, events[i], on_event, NULL);
	return &module;
}
