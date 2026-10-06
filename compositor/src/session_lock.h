#ifndef SHADY_SESSION_LOCK_H
#define SHADY_SESSION_LOCK_H

#include <stdint.h>

struct shady_output;
struct wlr_output_event_present;

void shady_session_lock_output_added(struct shady_output *output);
void shady_session_lock_output_removed(struct shady_output *output);
void shady_session_lock_output_state_changed(struct shady_output *output);
void shady_session_lock_output_committed(struct shady_output *output,
	uint32_t previous_commit_seq);
void shady_session_lock_output_presented(struct shady_output *output,
	const struct wlr_output_event_present *event);

#endif
