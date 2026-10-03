#include <shady/plugin.h>
#include <shady/motion.h>
#include <math.h>
#include <string.h>
static const struct shady_plugin_api_v1 *api;
static const struct shady_motion_api_v1 *motion;
struct motion_state {
    float wobble_x, wobble_y, wobble_vx, wobble_vy;
    float tilt_x, tilt_y, tilt_vx, tilt_vy;
    double last_move_x, last_move_y;
    bool wobble_dragging;
};
static struct motion_state *state_for(shady_window w) {
    return api->window_state(w, "window-motion");
}
static void publish(shady_host h, shady_window w) {
    const struct motion_state *s = state_for(w);
    if (!s) return;
    struct shady_motion_visual v = {
        .wobble_x = s->wobble_x, .wobble_y = s->wobble_y,
        .tilt_x = s->tilt_x, .tilt_y = s->tilt_y,
        .animating = fabsf(s->wobble_x) > .00005f || fabsf(s->wobble_y) > .00005f ||
            fabsf(s->wobble_vx) > .00005f || fabsf(s->wobble_vy) > .00005f ||
            fabsf(s->tilt_vx) > .00005f || fabsf(s->tilt_vy) > .00005f,
    };
    motion->set_visual(h, w, &v);
}
static void motion_begin_drag(shady_host h, shady_window w, double x, double y);
static void motion_update_toplevel(shady_host server,
		shady_window toplevel, float dt) {
	struct motion_state *s = state_for(toplevel);
	if (!s || dt <= 0.f) return;

	/* Flexible wobble is a configurable visual effect. When disabled, clear
	 * all accumulated state so other modules cannot leave a latent impulse. */
	if (motion->wobble_enabled(server)) {
		const float spring = 42.f, damping_rate = 7.5f;
		s->wobble_vx += -s->wobble_x * spring * dt;
		s->wobble_vy += -s->wobble_y * spring * dt;
		float damping = 1.f - damping_rate * dt;
		if (damping < 0.f) damping = 0.f;
		s->wobble_vx *= damping;
		s->wobble_vy *= damping;
		s->wobble_x += s->wobble_vx * dt;
		s->wobble_y += s->wobble_vy * dt;
		if (fabsf(s->wobble_x)<.00005f && fabsf(s->wobble_vx)<.00005f)
			s->wobble_x=s->wobble_vx=0.f;
		if (fabsf(s->wobble_y)<.00005f && fabsf(s->wobble_vy)<.00005f)
			s->wobble_y=s->wobble_vy=0.f;
	} else {
		s->wobble_x=s->wobble_y=0.f;
		s->wobble_vx=s->wobble_vy=0.f;
	}

	/* Rigid tilt is intentionally independent from flexible wobble. */
	const float tilt_damping_rate = 9.f;
	float damping = 1.f - tilt_damping_rate * dt;
	if (damping < 0.f) damping = 0.f;
	s->tilt_vx *= damping;
	s->tilt_vy *= damping;
	s->tilt_x += s->tilt_vx * dt;
	s->tilt_y += s->tilt_vy * dt;
	if (fabsf(s->tilt_vx) < .00005f)
		s->tilt_vx = 0.f;
	if (fabsf(s->tilt_vy) < .00005f)
		s->tilt_vy = 0.f;
	publish(server, toplevel);
}

static void motion_add_impulse(shady_host server,
		shady_window toplevel, float wobble_x, float wobble_y,
		float tilt_x, float tilt_y) {
	struct motion_state *s = state_for(toplevel);
	if (!s) return;
	if (motion->wobble_enabled(server)) {
		s->wobble_vx += wobble_x;
		s->wobble_vy += wobble_y;
	}
	s->tilt_vx += tilt_x;
	s->tilt_vy += tilt_y;
	publish(server, toplevel);
}

static void motion_begin_drag(shady_host host, shady_window toplevel, double x, double y) {
    (void)host;
    struct motion_state *s = state_for(toplevel);
    if (!s) return;
    s->last_move_x = x;
    s->last_move_y = y;
    s->wobble_dragging = true;
}
static float clamp_velocity(float v, float limit) {
    return fmaxf(-limit, fminf(limit, v));
}
static void motion_drag(shady_host server, shady_window toplevel, double x, double y) {
    struct motion_state *s = state_for(toplevel);
    if (!s) return;
    if (!s->wobble_dragging) {
        motion_begin_drag(server, toplevel, x, y);
        return;
    }
    double dx = x - s->last_move_x, dy = y - s->last_move_y;
    motion_add_impulse(server, toplevel, -(float)dx * .0065f, -(float)dy * .0065f,
        -(float)dy * .00055f, (float)dx * .00055f);
    s->tilt_vx = clamp_velocity(s->tilt_vx, .55f);
    s->tilt_vy = clamp_velocity(s->tilt_vy, .55f);
    s->wobble_vx = clamp_velocity(s->wobble_vx, .45f);
    s->wobble_vy = clamp_velocity(s->wobble_vy, .45f);
    s->last_move_x = x;
    s->last_move_y = y;
}

static void reset(shady_host h, shady_window w) {
    struct motion_state *s = state_for(w);
    if (!s) return;
    *s = (struct motion_state){0};
    publish(h, w);
}
static void damp(shady_host h, shady_window w, float factor) {
    struct motion_state *s = state_for(w);
    if (!s || !isfinite(factor)) return;
    if (factor < 0.f) factor = 0.f;
    s->tilt_vx *= factor; s->tilt_vy *= factor;
    publish(h, w);
}
static const struct shady_motion_driver driver = {
    .struct_size = sizeof(driver), .impulse = motion_add_impulse,
    .begin_drag = motion_begin_drag, .drag = motion_drag, .reset = reset, .damp = damp,
};
static bool enabled(struct shady_server *s) { return motion->spatial_enabled((shady_host)s); }
static bool init(struct shady_server *s) { return motion->attach((shady_host)s, &driver); }
static void destroy(struct shady_server *s) { motion->detach((shady_host)s, &driver); }
static void moved(struct shady_toplevel *w, double x, double y);
static shady_host plugin_host;
static void moved(struct shady_toplevel *w, double x, double y) {
    motion_drag(plugin_host, (shady_window)w, x, y);
}
static void tick(struct shady_server *s, float dt, float width, float height) {
    (void)width; (void)height;
    shady_host h = (shady_host)s;
    for (size_t i = 0, n = api->window_count(h); i < n; ++i) {
        shady_window w = api->window_at(h, i);
        if (api->window_mapped(w)) motion_update_toplevel(h, w, dt);
    }
}
static const char *const provides[] = { "spatial.window-motion", NULL };
static const char *const requires[] = { "spatial.window-state", NULL };
static const struct shady_module module = {
    .name = "window-motion", .provides = provides, .requires = requires,
    .toplevel_state_size = sizeof(struct motion_state),
    .enabled = enabled, .init = init, .destroy = destroy,
    .toplevel_moved = moved, .tick = tick,
};
static size_t snapshot_size(shady_host h, shady_window w, const void *s) {
    (void)h; (void)w; (void)s; return sizeof(struct motion_state);
}
static bool save(shady_host h, shady_window w, const void *s, void *out, size_t size) {
    (void)h; (void)w;
    if (size != sizeof(struct motion_state)) return false;
    memcpy(out, s, size); return true;
}
static bool restore(shady_host h, shady_window w, void *s, const void *data,
        size_t size, uint32_t schema) {
    if (schema != 1 || size != sizeof(struct motion_state)) return false;
    memcpy(s, data, size); publish(h, w); return true;
}
static const struct shady_plugin_v2 descriptor = {
    .struct_size = sizeof(descriptor), .module = &module, .state_schema_version = 1,
    .window_snapshot_size = snapshot_size, .save_window_state = save,
    .restore_window_state = restore,
};
const struct shady_plugin_v2 *shady_plugin_entry_v2(uint32_t abi,
        const struct shady_plugin_api_v1 *host_api, shady_host h) {
    if (abi != SHADY_PLUGIN_ABI_V2 || !SHADY_API_HAS(host_api, query_api)) return NULL;
    const struct shady_motion_api_v1 *feature =
        host_api->query_api(h, SHADY_MOTION_API, SHADY_MOTION_API_VERSION);
    if (!feature || feature->struct_size < sizeof(*feature)) return NULL;
    api = host_api; motion = feature; plugin_host = h;
    return &descriptor;
}
