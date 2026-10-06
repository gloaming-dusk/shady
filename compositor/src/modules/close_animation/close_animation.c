#include "close_animation.h"
#include "state.h"
#include "../window_motion/window_motion.h"

#include <stdlib.h>
#include <GLES2/gl2.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <shady/plugin.h>

#include "../../render/render.h"

#define DEFAULT_CLOSE_SECONDS .42f
#define WAIT_SECONDS .25f
#define DEFAULT_RESTORE_SECONDS .32f

/* Snapshot prefix shared with renderer; keep this layout in sync with
 * struct shady_close_snapshot in render.c. */
struct close_snapshot_lifetime {
	struct wl_list link;
	struct shady_toplevel *toplevel;
	struct shady_server *server;
	GLuint texture;
	int texture_width, texture_height;
	float x, y, width, height, tilt_x, tilt_y, z, wobble_x, wobble_y;
	bool has_alpha, dirty, animating;
	float progress;
	uint32_t close_style;
	float close_duration;
	float close_strength;
	float close_direction_x;
	float close_direction_y;
};

static float clamp_duration(float duration) {
	if (duration < .08f) return .08f;
	if (duration > 2.5f) return 2.5f;
	return duration;
}

void shady_close_animation_get_effect(const struct shady_toplevel *toplevel,
		uint32_t *style, float *duration, float *strength,
		float *direction_x, float *direction_y) {
	uint32_t out_style = SHADY_CLOSE_EFFECT_CRUMPLE;
	float out_duration = DEFAULT_CLOSE_SECONDS;
	float out_strength = 1.f;
	float out_dx = 0.f;
	float out_dy = 0.f;

	if (toplevel && toplevel->close_effect_override) {
		out_style = toplevel->close_effect_style;
		out_duration = clamp_duration(toplevel->close_effect_duration);
		out_strength = toplevel->close_effect_strength;
		out_dx = toplevel->close_effect_direction_x;
		out_dy = toplevel->close_effect_direction_y;
	}

	if (out_style > SHADY_CLOSE_EFFECT_SLIDE_FADE)
		out_style = SHADY_CLOSE_EFFECT_CRUMPLE;
	if (out_strength < 0.f) out_strength = 0.f;
	if (out_strength > 2.f) out_strength = 2.f;
	if (out_dx < -2.f) out_dx = -2.f;
	if (out_dx > 2.f) out_dx = 2.f;
	if (out_dy < -2.f) out_dy = -2.f;
	if (out_dy > 2.f) out_dy = 2.f;

	if (style) *style = out_style;
	if (duration) *duration = out_duration;
	if (strength) *strength = out_strength;
	if (direction_x) *direction_x = out_dx;
	if (direction_y) *direction_y = out_dy;
}

void shady_close_animation_begin_window(struct shady_server *server,
		struct shady_toplevel *toplevel) {
	if (!server || !toplevel || !toplevel->xdg_toplevel) return;

	if (!server->config.close_animation) {
		wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
		return;
	}

	struct shady_close_animation_state *state = shady_close_state_for(toplevel);
	if (!state) {
		wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
		return;
	}
	if (state->state != SHADY_CLOSE_IDLE &&
			state->state != SHADY_CLOSE_ARMED) {
		return;
	}

	shady_close_animation_get_effect(toplevel,
		&state->style, &state->duration, &state->strength,
		&state->direction_x, &state->direction_y);
	state->state = SHADY_CLOSE_CRUMPLING;
	state->progress = 0.f;
	state->wait_time = 0.f;

	if (state->style == SHADY_CLOSE_EFFECT_CRUMPLE) {
		shady_window_motion_add_impulse(server, toplevel,
			.10f * state->strength, -.07f * state->strength, 0.f, 0.f);
	}
	shady_render_schedule_all_outputs(server);
}

void shady_close_animation_begin(struct shady_server *server) {
	struct shady_toplevel *toplevel = NULL;
	if (!server || wl_list_empty(&server->toplevels)) return;
	toplevel = wl_container_of(server->toplevels.next, toplevel, link);
	shady_close_animation_begin_window(server, toplevel);
}

void shady_close_animation_update_toplevel(struct shady_toplevel *toplevel,
		float dt) {
	struct shady_close_animation_state *state = shady_close_state_for(toplevel);
	if (!state) return;

	float duration = clamp_duration(state->duration > 0.f
		? state->duration : DEFAULT_CLOSE_SECONDS);
	float restore = duration * (DEFAULT_RESTORE_SECONDS / DEFAULT_CLOSE_SECONDS);
	if (restore < .12f) restore = .12f;

	switch (state->state) {
	case SHADY_CLOSE_CRUMPLING:
		state->progress += dt / duration;
		if (state->progress >= 1.f) {
			state->progress = 1.f;
			state->wait_time = 0.f;
			wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
			state->state = SHADY_CLOSE_WAITING;
		}
		break;
	case SHADY_CLOSE_WAITING:
		state->wait_time += dt;
		if (state->wait_time >= WAIT_SECONDS)
			state->state = SHADY_CLOSE_RESTORING;
		break;
	case SHADY_CLOSE_RESTORING:
		state->progress -= dt / restore;
		if (state->progress <= 0.f) {
			state->progress = 0.f;
			state->wait_time = 0.f;
			state->state = SHADY_CLOSE_ARMED;
		}
		break;
	default:
		break;
	}
}

void shady_close_animation_update_snapshots(struct wl_list *snapshots,
		float dt) {
	struct close_snapshot_lifetime *snapshot, *tmp;
	wl_list_for_each_safe(snapshot, tmp, snapshots, link) {
		if (!snapshot->animating) continue;
		float duration = clamp_duration(snapshot->close_duration > 0.f
			? snapshot->close_duration : DEFAULT_CLOSE_SECONDS);
		snapshot->progress += dt / duration;
		if (snapshot->progress >= 1.f) {
			if (snapshot->texture) glDeleteTextures(1, &snapshot->texture);
			wl_list_remove(&snapshot->link);
			free(snapshot);
		}
	}
}

float shady_close_animation_progress(const struct shady_toplevel *toplevel) {
	const struct shady_close_animation_state *state =
		shady_close_state_for_const(toplevel);
	return state ? state->progress : 0.f;
}

enum shady_close_state shady_close_animation_state(
		const struct shady_toplevel *toplevel) {
	const struct shady_close_animation_state *state =
		shady_close_state_for_const(toplevel);
	return state ? state->state : SHADY_CLOSE_IDLE;
}
