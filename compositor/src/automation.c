/*
 * Automation input: see automation.h.
 */
#define _POSIX_C_SOURCE 200809L
#include "automation.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "shady.h"
#include "idle.h"

/* A keymap made for text holds at most this many keysyms at a time. */
#define UNICODE_KEYMAP_KEYS 200

static struct {
	struct wlr_keyboard *wlr_keyboard;
	struct shady_keyboard *keyboard;
	struct wl_list ops;
	bool ops_ready;
	uint64_t last_commit_ms;   /* any output drew a frame */
	uint64_t last_window_ms;   /* a window-like surface committed */
	char last_window[48];      /* which: "window 3", "the lock screen", ... */
	uint64_t last_layout_ms;   /* the layout moved windows */
} automation;

struct shady_automation_op {
	struct wl_list link;
	struct shady_server *server;
	struct wl_event_source *timer;
	shady_automation_done_fn done;
	void *data;
	uint64_t start_ms;
	bool is_wait;
	/* drag */
	double x1, y1, x2, y2;
	uint32_t button;
	int steps, step, interval_ms;
	bool pressed;
	/* wait */
	uint64_t min_until_ms, deadline_ms;
	int idle_ms;
	enum shady_automation_scope scope;
	uint32_t window;
};

static uint64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* ---- the automation keyboard ------------------------------------------ */

static const struct wlr_keyboard_impl automation_keyboard_impl = {
	.name = "shady-automation-keyboard",
};

static struct shady_keyboard *automation_keyboard(struct shady_server *server) {
	if (automation.keyboard) return automation.keyboard;
	struct wlr_keyboard *wlr_keyboard = calloc(1, sizeof(*wlr_keyboard));
	if (!wlr_keyboard) return NULL;
	wlr_keyboard_init(wlr_keyboard, &automation_keyboard_impl, "shady-automation");
	struct shady_keyboard *keyboard = shady_input_add_keyboard(server, &wlr_keyboard->base);
	if (!keyboard) {
		wlr_keyboard_finish(wlr_keyboard);
		free(wlr_keyboard);
		return NULL;
	}
	keyboard->automation = true;
	automation.wlr_keyboard = wlr_keyboard;
	automation.keyboard = keyboard;
	shady_input_update_seat_capabilities(server);
	return keyboard;
}

struct stroke {
	xkb_keycode_t keycode;
	bool shift;
};

/* The key that types `sym` in the first layout, with Shift or without:
 * without when both do. */
static bool find_key(struct xkb_keymap *keymap, xkb_keysym_t sym, struct stroke *out) {
	if (!keymap || sym == XKB_KEY_NoSymbol) return false;
	xkb_mod_index_t shift_index = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
	xkb_mod_mask_t shift = shift_index == XKB_MOD_INVALID ? 0 : 1u << shift_index;
	bool found = false;
	xkb_keycode_t max = xkb_keymap_max_keycode(keymap);
	for (xkb_keycode_t kc = xkb_keymap_min_keycode(keymap); kc <= max && kc >= 8; kc++) {
		if (xkb_keymap_num_layouts_for_key(keymap, kc) == 0) continue;
		xkb_level_index_t levels = xkb_keymap_num_levels_for_key(keymap, kc, 0);
		for (xkb_level_index_t level = 0; level < levels; level++) {
			const xkb_keysym_t *syms;
			int n = xkb_keymap_key_get_syms_by_level(keymap, kc, 0, level, &syms);
			if (n != 1 || syms[0] != sym) continue;
			xkb_mod_mask_t masks[16];
			size_t count = xkb_keymap_key_get_mods_for_level(keymap, kc, 0, level,
				masks, sizeof(masks) / sizeof(masks[0]));
			if (count == 0 && level == 0) {
				masks[0] = 0;
				count = 1;
			}
			for (size_t i = 0; i < count; i++) {
				if (masks[i] & ~shift) continue; /* AltGr, Lock: not ours to press */
				bool need_shift = masks[i] != 0;
				if (!found || (out->shift && !need_shift)) {
					*out = (struct stroke){ .keycode = kc, .shift = need_shift };
					found = true;
				}
				if (!need_shift) return true;
			}
		}
	}
	return found;
}

static bool key_down(struct wlr_keyboard *keyboard, xkb_keycode_t keycode) {
	for (size_t i = 0; i < keyboard->num_keycodes; i++)
		if (keyboard->keycodes[i] == keycode - 8) return true;
	return false;
}

static void send_key(struct wlr_keyboard *keyboard, xkb_keycode_t keycode, bool pressed) {
	if (key_down(keyboard, keycode) == pressed) return;
	struct wlr_keyboard_key_event event = {
		.time_msec = (uint32_t)now_ms(),
		.keycode = keycode - 8,
		.update_state = true,
		.state = pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED,
	};
	wlr_keyboard_notify_key(keyboard, &event);
}

static const struct {
	uint32_t mod;
	xkb_keysym_t sym;
} mod_keys[] = {
	{ WLR_MODIFIER_LOGO, XKB_KEY_Super_L },
	{ WLR_MODIFIER_CTRL, XKB_KEY_Control_L },
	{ WLR_MODIFIER_ALT, XKB_KEY_Alt_L },
	{ WLR_MODIFIER_SHIFT, XKB_KEY_Shift_L },
};
#define MOD_KEY_COUNT (sizeof(mod_keys) / sizeof(mod_keys[0]))

/* Press or release a shortcut on the automation keyboard, for the focused
 * surface only: the compositor's own handling has seen it already. */
static bool deliver(struct shady_server *server, xkb_keysym_t sym, uint32_t mods,
		bool pressed) {
	struct shady_keyboard *keyboard = automation_keyboard(server);
	if (!keyboard) return false;
	struct wlr_keyboard *wlr_keyboard = keyboard->wlr_keyboard;
	struct stroke key;
	if (!find_key(wlr_keyboard->keymap, sym, &key)) return false;
	if (key.shift) mods |= WLR_MODIFIER_SHIFT;

	keyboard->route = SHADY_KEYS_DELIVER;
	if (pressed) {
		for (size_t i = 0; i < MOD_KEY_COUNT; i++) {
			struct stroke mod;
			if ((mods & mod_keys[i].mod) && mod_keys[i].sym != sym &&
					find_key(wlr_keyboard->keymap, mod_keys[i].sym, &mod))
				send_key(wlr_keyboard, mod.keycode, true);
		}
		send_key(wlr_keyboard, key.keycode, true);
	} else {
		send_key(wlr_keyboard, key.keycode, false);
		for (size_t i = MOD_KEY_COUNT; i-- > 0;) {
			struct stroke mod;
			if ((mods & mod_keys[i].mod) && mod_keys[i].sym != sym &&
					find_key(wlr_keyboard->keymap, mod_keys[i].sym, &mod))
				send_key(wlr_keyboard, mod.keycode, false);
		}
	}
	keyboard->route = SHADY_KEYS_NORMAL;
	return true;
}

void shady_automation_key_state(struct shady_server *server, xkb_keysym_t sym,
		uint32_t mods, bool pressed, struct shady_automation_key_result *result) {
	struct shady_automation_key_result r = {0};
	r.handled = shady_input_automation_key(server, sym, mods, pressed);
	if (!r.handled) r.sent = deliver(server, sym, mods, pressed);
	if (result) *result = r;
}

void shady_automation_key(struct shady_server *server, xkb_keysym_t sym,
		uint32_t mods, struct shady_automation_key_result *result) {
	struct shady_automation_key_result r = {0};
	r.handled = shady_input_automation_key(server, sym, mods, true);
	if (!r.handled)
		r.sent = deliver(server, sym, mods, true) && deliver(server, sym, mods, false);
	r.handled |= shady_input_automation_key(server, sym, mods, false);
	if (result) *result = r;
}

/* ---- text ------------------------------------------------------------- */

/* One code point from UTF-8, or -1 for a malformed sequence. */
static int32_t utf8_next(const unsigned char **p) {
	const unsigned char *s = *p;
	uint32_t cp;
	int extra;
	if (s[0] < 0x80) { cp = s[0]; extra = 0; }
	else if ((s[0] & 0xe0) == 0xc0) { cp = s[0] & 0x1f; extra = 1; }
	else if ((s[0] & 0xf0) == 0xe0) { cp = s[0] & 0x0f; extra = 2; }
	else if ((s[0] & 0xf8) == 0xf0) { cp = s[0] & 0x07; extra = 3; }
	else return -1;
	for (int i = 1; i <= extra; i++) {
		if ((s[i] & 0xc0) != 0x80) return -1;
		cp = (cp << 6) | (s[i] & 0x3f);
	}
	static const uint32_t min[] = { 0, 0x80, 0x800, 0x10000 };
	if (cp < min[extra] || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return -1;
	*p = s + extra + 1;
	return (int32_t)cp;
}

static xkb_keysym_t char_keysym(uint32_t cp) {
	switch (cp) {
	case '\n': case '\r': return XKB_KEY_Return;
	case '\t': return XKB_KEY_Tab;
	case '\b': return XKB_KEY_BackSpace;
	case 0x1b: return XKB_KEY_Escape;
	default: return xkb_utf32_to_keysym(cp);
	}
}

/* A keymap with one key per keysym and nothing else, as wtype makes. */
static struct xkb_keymap *text_keymap(const xkb_keysym_t *syms, size_t count) {
	char *text = NULL;
	size_t len = 0;
	FILE *f = open_memstream(&text, &len);
	if (!f) return NULL;
	fprintf(f, "xkb_keymap {\nxkb_keycodes \"shady\" {\nminimum = 8;\nmaximum = %zu;\n",
		count + 9);
	for (size_t i = 0; i < count; i++) fprintf(f, "<K%zu> = %zu;\n", i, i + 9);
	fputs("};\nxkb_types \"shady\" { include \"complete\" };\n"
		"xkb_compatibility \"shady\" { include \"complete\" };\n"
		"xkb_symbols \"shady\" {\n", f);
	for (size_t i = 0; i < count; i++) {
		char name[64];
		if (xkb_keysym_get_name(syms[i], name, sizeof(name)) <= 0)
			snprintf(name, sizeof(name), "0x%08x", syms[i]);
		fprintf(f, "key <K%zu> {[ %s ]};\n", i, name);
	}
	fputs("};\n};\n", f);
	if (fclose(f) != 0) {
		free(text);
		return NULL;
	}
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = context ? xkb_keymap_new_from_string(context, text,
		XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS) : NULL;
	if (context) xkb_context_unref(context);
	free(text);
	return keymap;
}

static void release_all(struct wlr_keyboard *keyboard) {
	while (keyboard->num_keycodes > 0)
		send_key(keyboard, keyboard->keycodes[keyboard->num_keycodes - 1] + 8, false);
}

/* syms[0..count): typed through a keymap of their own, a chunk of distinct
 * keysyms at a time. */
static bool type_with_text_keymap(struct shady_server *server,
		struct shady_keyboard *keyboard, const xkb_keysym_t *syms, size_t count) {
	struct wlr_keyboard *wlr_keyboard = keyboard->wlr_keyboard;
	keyboard->route = SHADY_KEYS_RAW;
	release_all(wlr_keyboard);
	bool ok = true;
	size_t start = 0;
	while (ok && start < count) {
		xkb_keysym_t table[UNICODE_KEYMAP_KEYS];
		size_t table_count = 0, end = start;
		for (; end < count; end++) {
			size_t j = 0;
			while (j < table_count && table[j] != syms[end]) j++;
			if (j < table_count) continue;
			if (table_count == UNICODE_KEYMAP_KEYS) break;
			table[table_count++] = syms[end];
		}
		struct xkb_keymap *keymap = text_keymap(table, table_count);
		if (!keymap) {
			ok = false;
			break;
		}
		/* The seat sends this keymap on to the clients. */
		wlr_seat_set_keyboard(server->seat, wlr_keyboard);
		wlr_keyboard_set_keymap(wlr_keyboard, keymap);
		xkb_keymap_unref(keymap);
		for (size_t i = start; i < end; i++) {
			size_t j = 0;
			while (table[j] != syms[i]) j++;
			send_key(wlr_keyboard, (xkb_keycode_t)(j + 9), true);
			send_key(wlr_keyboard, (xkb_keycode_t)(j + 9), false);
		}
		start = end;
	}
	keyboard->route = SHADY_KEYS_NORMAL;
	shady_input_keyboard_apply_config(keyboard);
	return ok;
}

int shady_automation_type(struct shady_server *server, const char *text,
		const char **error) {
	const char *dummy;
	if (!error) error = &dummy;
	struct shady_keyboard *keyboard = automation_keyboard(server);
	if (!keyboard) {
		*error = "no automation keyboard";
		return -1;
	}
	size_t cap = strlen(text);
	xkb_keysym_t *syms = calloc(cap ? cap : 1, sizeof(*syms));
	struct stroke *strokes = calloc(cap ? cap : 1, sizeof(*strokes));
	if (!syms || !strokes) {
		free(syms);
		free(strokes);
		*error = "out of memory";
		return -1;
	}
	size_t count = 0;
	bool in_keymap = true;
	const unsigned char *p = (const unsigned char *)text;
	while (*p) {
		int32_t cp = utf8_next(&p);
		if (cp < 0) {
			free(syms);
			free(strokes);
			*error = "text is not valid UTF-8";
			return -1;
		}
		xkb_keysym_t sym = char_keysym((uint32_t)cp);
		if (sym == XKB_KEY_NoSymbol) {
			free(syms);
			free(strokes);
			*error = "text has a character no key can type";
			return -1;
		}
		if (in_keymap && !find_key(keyboard->wlr_keyboard->keymap, sym, &strokes[count]))
			in_keymap = false;
		syms[count++] = sym;
	}

	bool ok = true;
	if (in_keymap) {
		/* As typed on the keyboard: the compositor's handling sees it too. */
		struct wlr_keyboard *wlr_keyboard = keyboard->wlr_keyboard;
		struct stroke shift = {0};
		bool have_shift = find_key(wlr_keyboard->keymap, XKB_KEY_Shift_L, &shift);
		for (size_t i = 0; i < count; i++) {
			if (strokes[i].shift && have_shift) send_key(wlr_keyboard, shift.keycode, true);
			send_key(wlr_keyboard, strokes[i].keycode, true);
			send_key(wlr_keyboard, strokes[i].keycode, false);
			if (strokes[i].shift && have_shift) send_key(wlr_keyboard, shift.keycode, false);
		}
	} else {
		ok = type_with_text_keymap(server, keyboard, syms, count);
		if (!ok) *error = "cannot make a keymap for the text";
	}
	free(syms);
	free(strokes);
	return ok ? (int)count : -1;
}

/* ---- pointer ---------------------------------------------------------- */

bool shady_automation_button_from_name(const char *name, uint32_t *button) {
	static const struct { const char *name; uint32_t button; } buttons[] = {
		{ "left", BTN_LEFT }, { "right", BTN_RIGHT }, { "middle", BTN_MIDDLE },
		{ "side", BTN_SIDE }, { "extra", BTN_EXTRA },
		{ "forward", BTN_FORWARD }, { "back", BTN_BACK },
	};
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(buttons[i].name, name) != 0) continue;
		*button = buttons[i].button;
		return true;
	}
	return false;
}

void shady_automation_pointer_move(struct shady_server *server, double x, double y) {
	shady_idle_activity(server);
	shady_input_automation_pointer_move(server, x, y);
}

void shady_automation_pointer_button(struct shady_server *server, uint32_t button,
		bool pressed) {
	shady_idle_activity(server);
	shady_input_automation_pointer_button(server, button, pressed);
}

void shady_automation_click(struct shady_server *server, uint32_t button, int count) {
	shady_idle_activity(server);
	for (int i = 0; i < count; i++) {
		shady_input_automation_pointer_button(server, button, true);
		shady_input_automation_pointer_button(server, button, false);
	}
}

void shady_automation_scroll(struct shady_server *server, double dx, double dy) {
	shady_idle_activity(server);
	/* A wheel click is 15 units of scroll and 120 of value120, as libinput. */
	if (dy != 0.0)
		shady_input_automation_pointer_axis(server, WL_POINTER_AXIS_VERTICAL_SCROLL,
			dy * 15.0, (int32_t)lround(dy * 120.0));
	if (dx != 0.0)
		shady_input_automation_pointer_axis(server, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
			dx * 15.0, (int32_t)lround(dx * 120.0));
}

/* ---- operations over time --------------------------------------------- */

static void ops_init(void) {
	if (automation.ops_ready) return;
	wl_list_init(&automation.ops);
	automation.ops_ready = true;
}

static void op_free(struct shady_automation_op *op) {
	wl_list_remove(&op->link);
	if (op->timer) wl_event_source_remove(op->timer);
	free(op);
}

static void op_finish(struct shady_automation_op *op, bool settled, const char *active) {
	struct shady_automation_result result = {
		.settled = settled,
		.elapsed_ms = (uint32_t)(now_ms() - op->start_ms),
		.active = settled ? NULL : active,
	};
	shady_automation_done_fn done = op->done;
	void *data = op->data;
	op_free(op);
	if (done) done(data, &result);
}

static void op_arm(struct shady_automation_op *op, uint64_t ms) {
	/* 0 would disarm the timer. */
	wl_event_source_timer_update(op->timer, ms > 0 ? (int)(ms < 60000 ? ms : 60000) : 1);
}

static struct shady_automation_op *op_new(struct shady_server *server,
		int (*tick)(void *), shady_automation_done_fn done, void *data) {
	ops_init();
	struct shady_automation_op *op = calloc(1, sizeof(*op));
	if (!op) return NULL;
	op->server = server;
	op->done = done;
	op->data = data;
	op->start_ms = now_ms();
	op->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
		tick, op);
	if (!op->timer) {
		free(op);
		return NULL;
	}
	wl_list_insert(&automation.ops, &op->link);
	return op;
}

static void drag_move(struct shady_automation_op *op) {
	double t = (double)op->step / (double)op->steps;
	shady_automation_pointer_move(op->server,
		op->x1 + (op->x2 - op->x1) * t, op->y1 + (op->y2 - op->y1) * t);
}

static int drag_tick(void *data) {
	struct shady_automation_op *op = data;
	if (op->step < op->steps) {
		op->step++;
		drag_move(op);
		op_arm(op, (uint64_t)op->interval_ms);
		return 0;
	}
	shady_automation_pointer_button(op->server, op->button, false);
	op->pressed = false;
	op_finish(op, true, NULL);
	return 0;
}

struct shady_automation_op *shady_automation_drag(struct shady_server *server,
		double x1, double y1, double x2, double y2, uint32_t button, int steps,
		int duration_ms, shady_automation_done_fn done, void *data, bool *now) {
	if (now) *now = false;
	if (steps < 1) steps = 1;
	if (duration_ms <= 0) {
		shady_automation_pointer_move(server, x1, y1);
		shady_automation_pointer_button(server, button, true);
		for (int i = 1; i <= steps; i++) {
			double t = (double)i / (double)steps;
			shady_automation_pointer_move(server, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t);
		}
		shady_automation_pointer_button(server, button, false);
		if (now) *now = true;
		return NULL;
	}
	struct shady_automation_op *op = op_new(server, drag_tick, done, data);
	if (!op) return NULL;
	op->x1 = x1; op->y1 = y1; op->x2 = x2; op->y2 = y2;
	op->button = button;
	op->steps = steps;
	/* The press, every step and the release each get their share. */
	op->interval_ms = duration_ms / (steps + 1);
	if (op->interval_ms < 1) op->interval_ms = 1;
	shady_automation_pointer_move(server, x1, y1);
	shady_automation_pointer_button(server, button, true);
	op->pressed = true;
	op_arm(op, (uint64_t)op->interval_ms);
	return op;
}

static struct shady_toplevel *toplevel_by_id(struct shady_server *server, uint32_t id) {
	struct shady_toplevel *t;
	wl_list_for_each(t, &server->all_toplevels, all_link)
		if (t->shell_id == id) return t;
	return NULL;
}

/* When the wait last saw a change, and what it was. */
static uint64_t wait_last_change(struct shady_automation_op *op, char *what, size_t size) {
	uint64_t last = 0;
	const char *label = NULL;
	char window[32];
	switch (op->scope) {
	case SHADY_WAIT_SCREEN:
		last = automation.last_commit_ms;
		label = "frames";
		break;
	case SHADY_WAIT_WINDOWS:
		if (op->window) {
			/* A window that is gone changes no more. */
			struct shady_toplevel *t = toplevel_by_id(op->server, op->window);
			last = t ? t->automation_commit_ms : 0;
			snprintf(window, sizeof(window), "window %u", op->window);
			label = window;
		} else {
			last = automation.last_window_ms;
			label = automation.last_window;
		}
		if (automation.last_layout_ms > last) {
			last = automation.last_layout_ms;
			label = "layout";
		}
		break;
	}
	snprintf(what, size, "%s", label ? label : "");
	return last;
}

static int wait_tick(void *data) {
	struct shady_automation_op *op = data;
	uint64_t now = now_ms();
	char active[48];
	uint64_t last = wait_last_change(op, active, sizeof(active));
	uint64_t quiet_since = last > op->start_ms ? last : op->start_ms;
	uint64_t ready_at = op->min_until_ms;
	if (op->idle_ms > 0 && quiet_since + (uint64_t)op->idle_ms > ready_at)
		ready_at = quiet_since + (uint64_t)op->idle_ms;
	if (now >= ready_at) {
		op_finish(op, true, NULL);
		return 0;
	}
	if (now >= op->deadline_ms) {
		op_finish(op, false, active);
		return 0;
	}
	uint64_t next = ready_at < op->deadline_ms ? ready_at : op->deadline_ms;
	op_arm(op, next - now);
	return 0;
}

struct shady_automation_op *shady_automation_wait(struct shady_server *server,
		int ms, int idle_ms, int timeout_ms, enum shady_automation_scope scope,
		uint32_t window, shady_automation_done_fn done, void *data) {
	struct shady_automation_op *op = op_new(server, wait_tick, done, data);
	if (!op) return NULL;
	op->is_wait = true;
	op->scope = scope;
	op->window = scope == SHADY_WAIT_WINDOWS ? window : 0;
	op->idle_ms = idle_ms > 0 ? idle_ms : 0;
	op->min_until_ms = op->start_ms + (uint64_t)(ms > 0 ? ms : 0);
	uint64_t deadline = op->start_ms + (uint64_t)(timeout_ms > 0 ? timeout_ms : 0);
	op->deadline_ms = deadline > op->min_until_ms ? deadline : op->min_until_ms;
	/* The first look: when it could be settled, but never past the
	 * deadline, where it gives up. */
	uint64_t first = op->min_until_ms + (uint64_t)op->idle_ms;
	if (first > op->deadline_ms) first = op->deadline_ms;
	op_arm(op, first - op->start_ms);
	return op;
}

/* The surface whose role decides what a commit is: a subsurface's parent,
 * a popup's parent, up to a toplevel, a layer or a lock surface. */
static struct wlr_surface *owner_surface(struct wlr_surface *surface) {
	struct wlr_surface *root = wlr_surface_get_root_surface(surface);
	for (int i = 0; root && i < 16; i++) {
		struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(root);
		if (!popup || !popup->parent) break;
		root = wlr_surface_get_root_surface(popup->parent);
	}
	return root;
}

static bool waiting(void) {
	if (!automation.ops_ready) return false;
	struct shady_automation_op *op;
	wl_list_for_each(op, &automation.ops, link)
		if (op->is_wait) return true;
	return false;
}

static void note_window(uint64_t now, const char *what) {
	automation.last_window_ms = now;
	snprintf(automation.last_window, sizeof(automation.last_window), "%s", what);
}

void shady_automation_surface_committed(struct shady_server *server,
		struct wlr_surface *surface) {
	/* Only a wait looks at these. */
	if (!surface || !waiting()) return;
	uint64_t now = now_ms();
	struct wlr_surface *owner = owner_surface(surface);
	if (!owner) return;
	struct wlr_xdg_toplevel *xdg = wlr_xdg_toplevel_try_from_wlr_surface(owner);
	if (xdg) {
		struct shady_toplevel *t;
		wl_list_for_each(t, &server->all_toplevels, all_link) {
			if (t->xdg_toplevel != xdg) continue;
			t->automation_commit_ms = now;
			char what[32];
			snprintf(what, sizeof(what), "window %u", t->shell_id);
			note_window(now, what);
			break;
		}
		return;
	}
	if (wlr_session_lock_surface_v1_try_from_wlr_surface(owner)) {
		note_window(now, "the lock screen");
		return;
	}
	/* A layer surface counts while it has the keyboard: a launcher being
	 * typed into, not a bar's clock or a wallpaper. */
	struct wlr_layer_surface_v1 *layer = wlr_layer_surface_v1_try_from_wlr_surface(owner);
	struct wlr_surface *focus = server->seat->keyboard_state.focused_surface;
	if (layer && focus && owner_surface(focus) == owner) {
		note_window(now, layer->namespace && *layer->namespace ?
			layer->namespace : "a layer surface with the keyboard");
	}
}

void shady_automation_layout_moved(void) {
	if (waiting()) automation.last_layout_ms = now_ms();
}

void shady_automation_op_cancel(struct shady_automation_op *op) {
	if (!op) return;
	if (!op->is_wait && op->pressed)
		shady_input_automation_pointer_button(op->server, op->button, false);
	op_free(op);
}

void shady_automation_frame_committed(void) {
	automation.last_commit_ms = now_ms();
}

void shady_automation_finish(struct shady_server *server) {
	(void)server;
	if (automation.ops_ready) {
		struct shady_automation_op *op, *tmp;
		wl_list_for_each_safe(op, tmp, &automation.ops, link)
			shady_automation_op_cancel(op);
	}
	if (automation.wlr_keyboard) {
		/* Destroying the device frees the shady_keyboard (input.c). */
		wlr_keyboard_finish(automation.wlr_keyboard);
		free(automation.wlr_keyboard);
		automation.wlr_keyboard = NULL;
		automation.keyboard = NULL;
	}
}
