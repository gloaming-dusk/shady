#ifndef SHADY_MODULE_DESKTOP_IME_H
#define SHADY_MODULE_DESKTOP_IME_H

#include <stdbool.h>
#include <stdint.h>

struct shady_server;
struct wlr_keyboard_modifiers;

bool shady_ime_init(struct shady_server *server);
void shady_ime_finish(struct shady_server *server);
bool shady_ime_handle_key(struct shady_server *server,
	uint32_t time_msec, uint32_t keycode, uint32_t state);
bool shady_ime_handle_modifiers(struct shady_server *server,
	struct wlr_keyboard_modifiers *modifiers);

#endif
