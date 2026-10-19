#ifndef SHADY_SHELL_NOTIFY_H
#define SHADY_SHELL_NOTIFY_H

/*
 * The desktop notification server (org.freedesktop.Notifications, spec 1.2)
 * on the session bus. A config turns it on with shell.notification_server();
 * it then outlives config reloads. Notifications are kept, newest first, as
 * a short history: `active` ones are still on screen, expired or dismissed
 * ones stay listed until cleared. Every change emits the "notifications"
 * event and repaints the views.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct shell;

#define NOTIFY_MAX_ACTIONS 6

struct shell_notification {
    uint32_t id;
    char *app;
    char *summary;
    char *body;
    char *icon;
    int urgency; /* 0 low, 1 normal, 2 critical */
    int timeout_ms; /* 0: until dismissed */
    int64_t time; /* when it arrived, seconds since the epoch */
    bool active;
    size_t action_count;
    char *action_keys[NOTIFY_MAX_ACTIONS];
    char *action_labels[NOTIFY_MAX_ACTIONS];
};

/* Own the bus name; `timeout_ms` is used when a client asks for the server
 * default. True if the server runs (already or now). */
bool shell_notify_serve(struct shell *shell, int timeout_ms);
bool shell_notify_running(void);
void shell_notify_finish(void);

size_t shell_notify_count(void);
/* Index 0 is the newest. */
const struct shell_notification *shell_notify_at(size_t index);

/* Close it as dismissed by the user; it stays in the history. */
bool shell_notify_dismiss(uint32_t id);
/* Report the action to its client, then dismiss the notification. */
bool shell_notify_invoke(uint32_t id, const char *key);
/* Forget every notification, closing the active ones. */
void shell_notify_clear(void);

#endif
