#define _GNU_SOURCE

#include "notify.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "script.h"
#include "shell.h"

#define HISTORY_MAX 50

struct entry {
    struct shell_notification n;
    struct shell_timer *expire;
};

/* The history, newest first. Kept even without a bus so the stubs below
 * can share the accessors. */
static struct {
    struct shell *shell;
    struct entry *items[HISTORY_MAX];
    size_t count;
    uint32_t next_id;
    int default_timeout;
} hist = { .next_id = 1, .default_timeout = 5000 };

size_t shell_notify_count(void) {
    return hist.count;
}

const struct shell_notification *shell_notify_at(size_t index) {
    return index < hist.count ? &hist.items[index]->n : NULL;
}

#if HAVE_SD_BUS

#include <systemd/sd-bus.h>

#define BUS_NAME "org.freedesktop.Notifications"
#define BUS_PATH "/org/freedesktop/Notifications"

enum close_reason { CLOSED_EXPIRED = 1, CLOSED_DISMISSED = 2, CLOSED_BY_CALL = 3 };

static struct {
    sd_bus *bus;
    sd_bus_slot *slot;
    struct shell_watch *watch;
    short watch_events;
    struct shell_timer *timer;
} bus_state;

static void update_watch(void);

static void changed(void) {
    shell_lua_emit(hist.shell, "notifications");
    shell_lua_model_changed(hist.shell);
}

static void entry_free(struct entry *e) {
    shell_timer_cancel(e->expire);
    free(e->n.app);
    free(e->n.summary);
    free(e->n.body);
    free(e->n.icon);
    for (size_t i = 0; i < e->n.action_count; i++) {
        free(e->n.action_keys[i]);
        free(e->n.action_labels[i]);
    }
    free(e);
}

static struct entry *find(uint32_t id, size_t *index) {
    for (size_t i = 0; i < hist.count; i++) {
        if (hist.items[i]->n.id == id) {
            if (index) *index = i;
            return hist.items[i];
        }
    }
    return NULL;
}

static void emit_closed(uint32_t id, uint32_t reason) {
    if (!bus_state.bus) return;
    sd_bus_emit_signal(bus_state.bus, BUS_PATH, BUS_NAME, "NotificationClosed", "uu", id, reason);
    update_watch();
}

static void close_entry(struct entry *e, enum close_reason reason) {
    if (!e->n.active) return;
    e->n.active = false;
    shell_timer_cancel(e->expire);
    e->expire = NULL;
    emit_closed(e->n.id, reason);
}

static void expired(void *data) {
    struct entry *e = data;
    e->expire = NULL;
    close_entry(e, CLOSED_EXPIRED);
    changed();
}

static void arm(struct entry *e) {
    shell_timer_cancel(e->expire);
    e->expire = e->n.timeout_ms > 0
        ? shell_timer_add(&hist.shell->core, (uint32_t)e->n.timeout_ms, expired, e) : NULL;
}

/* Put `e` at the front of the history, dropping the oldest if it is full. */
static void push_front(struct entry *e) {
    if (hist.count == HISTORY_MAX) {
        struct entry *old = hist.items[--hist.count];
        close_entry(old, CLOSED_EXPIRED);
        entry_free(old);
    }
    memmove(&hist.items[1], &hist.items[0], hist.count * sizeof(hist.items[0]));
    hist.items[0] = e;
    hist.count++;
}

static char *dup_text(const char *s) {
    return strdup(s ? s : "");
}

/* Notify(app_name, replaces_id, app_icon, summary, body, actions, hints,
 *        expire_timeout) -> id */
static int method_notify(sd_bus_message *m, void *data, sd_bus_error *error) {
    (void)data; (void)error;
    const char *app, *icon, *summary, *body;
    uint32_t replaces;
    int r = sd_bus_message_read(m, "susss", &app, &replaces, &icon, &summary, &body);
    if (r < 0) return r;

    struct entry *e = calloc(1, sizeof(*e));
    if (!e) return -ENOMEM;
    e->n.app = dup_text(app);
    e->n.icon = dup_text(icon);
    e->n.summary = dup_text(summary);
    e->n.body = dup_text(body);
    e->n.urgency = 1;

    /* Actions come as a flat list: key, label, key, label, ... */
    if ((r = sd_bus_message_enter_container(m, 'a', "s")) < 0) goto fail;
    const char *key;
    while ((r = sd_bus_message_read(m, "s", &key)) > 0) {
        const char *label = NULL;
        if ((r = sd_bus_message_read(m, "s", &label)) <= 0) break;
        if (e->n.action_count < NOTIFY_MAX_ACTIONS) {
            e->n.action_keys[e->n.action_count] = dup_text(key);
            e->n.action_labels[e->n.action_count] = dup_text(label);
            e->n.action_count++;
        }
    }
    if (r < 0 || (r = sd_bus_message_exit_container(m)) < 0) goto fail;

    if ((r = sd_bus_message_enter_container(m, 'a', "{sv}")) < 0) goto fail;
    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        const char *hint;
        if ((r = sd_bus_message_read(m, "s", &hint)) < 0) goto fail;
        const char *contents = NULL;
        if ((r = sd_bus_message_peek_type(m, NULL, &contents)) < 0) goto fail;
        if (!strcmp(hint, "urgency") && contents && !strcmp(contents, "y") &&
                sd_bus_message_enter_container(m, 'v', "y") > 0) {
            uint8_t urgency = 1;
            if ((r = sd_bus_message_read(m, "y", &urgency)) < 0) goto fail;
            e->n.urgency = urgency > 2 ? 2 : urgency;
            if ((r = sd_bus_message_exit_container(m)) < 0) goto fail;
        } else if ((r = sd_bus_message_skip(m, "v")) < 0) {
            goto fail;
        }
        if ((r = sd_bus_message_exit_container(m)) < 0) goto fail;
    }
    if (r < 0 || (r = sd_bus_message_exit_container(m)) < 0) goto fail;

    int32_t timeout;
    if ((r = sd_bus_message_read(m, "i", &timeout)) < 0) goto fail;
    /* -1 asks for the server's default; critical ones then stay. */
    e->n.timeout_ms = timeout < 0 ? (e->n.urgency == 2 ? 0 : hist.default_timeout) : timeout;
    e->n.time = (int64_t)time(NULL);
    e->n.active = true;

    size_t index;
    struct entry *old = replaces ? find(replaces, &index) : NULL;
    if (old) {
        /* Same id, new content, moved to the front. */
        e->n.id = old->n.id;
        memmove(&hist.items[index], &hist.items[index + 1],
            (hist.count - index - 1) * sizeof(hist.items[0]));
        hist.count--;
        entry_free(old);
    } else {
        e->n.id = hist.next_id++;
        if (hist.next_id == 0) hist.next_id = 1;
    }
    push_front(e);
    arm(e);
    changed();
    return sd_bus_reply_method_return(m, "u", e->n.id);

fail:
    entry_free(e);
    return r;
}

static int method_close(sd_bus_message *m, void *data, sd_bus_error *error) {
    (void)data; (void)error;
    uint32_t id;
    int r = sd_bus_message_read(m, "u", &id);
    if (r < 0) return r;
    struct entry *e = find(id, NULL);
    if (e && e->n.active) {
        close_entry(e, CLOSED_BY_CALL);
        changed();
    }
    return sd_bus_reply_method_return(m, "");
}

static int method_capabilities(sd_bus_message *m, void *data, sd_bus_error *error) {
    (void)data; (void)error;
    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r >= 0) r = sd_bus_message_append(reply, "as", 2, "body", "actions");
    if (r >= 0) r = sd_bus_send(NULL, reply, NULL);
    sd_bus_message_unref(reply);
    return r;
}

static int method_server_information(sd_bus_message *m, void *data, sd_bus_error *error) {
    (void)data; (void)error;
    return sd_bus_reply_method_return(m, "ssss", "shady-shell", "Shady", "0.1", "1.2");
}

static const sd_bus_vtable vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Notify", "susssasa{sv}i", "u", method_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("CloseNotification", "u", "", method_close, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetCapabilities", "", "as", method_capabilities, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetServerInformation", "", "ssss", method_server_information,
        SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("NotificationClosed", "uu", 0),
    SD_BUS_SIGNAL("ActionInvoked", "us", 0),
    SD_BUS_VTABLE_END,
};

/* ---- the bus in the shell's poll loop --------------------------------- */

static void stop_bus(void) {
    if (bus_state.watch) shell_watch_remove(bus_state.watch);
    shell_timer_cancel(bus_state.timer);
    sd_bus_slot_unref(bus_state.slot);
    sd_bus_flush_close_unref(bus_state.bus);
    memset(&bus_state, 0, sizeof(bus_state));
}

static void dispatch(void) {
    int r;
    while ((r = sd_bus_process(bus_state.bus, NULL)) > 0) {}
    if (r < 0) {
        fprintf(stderr, "shady-shell: notification bus failed: %s\n", strerror(-r));
        stop_bus();
        return;
    }
    update_watch();
}

static void readable(int fd, short revents, void *data) {
    (void)fd; (void)revents; (void)data;
    dispatch();
}

static void timed_out(void *data) {
    (void)data;
    bus_state.timer = NULL;
    dispatch();
}

/* Follow what sd-bus wants to wait for: its fd events and its timeout. */
static void update_watch(void) {
    if (!bus_state.bus) return;
    int events = sd_bus_get_events(bus_state.bus);
    if (events < 0) events = POLLIN;
    if (!bus_state.watch || events != bus_state.watch_events) {
        if (bus_state.watch) shell_watch_remove(bus_state.watch);
        bus_state.watch = shell_watch_add(&hist.shell->core, sd_bus_get_fd(bus_state.bus),
            (short)events, readable, NULL);
        bus_state.watch_events = (short)events;
    }
    shell_timer_cancel(bus_state.timer);
    bus_state.timer = NULL;
    uint64_t until;
    if (sd_bus_get_timeout(bus_state.bus, &until) >= 0 && until != UINT64_MAX) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t now_us = (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
        uint64_t ms = until > now_us ? (until - now_us + 999) / 1000 : 0;
        bus_state.timer = shell_timer_add(&hist.shell->core, ms > 60000 ? 60000 : (uint32_t)ms,
            timed_out, NULL);
    }
}

bool shell_notify_serve(struct shell *shell, int timeout_ms) {
    hist.shell = shell;
    if (timeout_ms > 0) hist.default_timeout = timeout_ms;
    if (bus_state.bus) return true;
    int r = sd_bus_open_user(&bus_state.bus);
    if (r < 0) {
        fprintf(stderr, "shady-shell: notifications: no session bus: %s\n", strerror(-r));
        bus_state.bus = NULL;
        return false;
    }
    r = sd_bus_add_object_vtable(bus_state.bus, &bus_state.slot, BUS_PATH, BUS_NAME, vtable, NULL);
    if (r >= 0) r = sd_bus_request_name(bus_state.bus, BUS_NAME, 0);
    if (r < 0) {
        fprintf(stderr, "shady-shell: notifications: %s\n", r == -EEXIST
            ? "another notification server is running" : strerror(-r));
        stop_bus();
        return false;
    }
    fprintf(stderr, "shady-shell: serving notifications\n");
    update_watch();
    return true;
}

bool shell_notify_running(void) {
    return bus_state.bus != NULL;
}

bool shell_notify_dismiss(uint32_t id) {
    struct entry *e = find(id, NULL);
    if (!e || !e->n.active) return false;
    close_entry(e, CLOSED_DISMISSED);
    changed();
    return true;
}

bool shell_notify_invoke(uint32_t id, const char *key) {
    struct entry *e = find(id, NULL);
    if (!e || !e->n.active || !key) return false;
    if (bus_state.bus) {
        sd_bus_emit_signal(bus_state.bus, BUS_PATH, BUS_NAME, "ActionInvoked", "us", id, key);
        update_watch();
    }
    return shell_notify_dismiss(id);
}

void shell_notify_clear(void) {
    if (!hist.count) return;
    for (size_t i = 0; i < hist.count; i++) {
        close_entry(hist.items[i], CLOSED_DISMISSED);
        entry_free(hist.items[i]);
    }
    hist.count = 0;
    changed();
}

void shell_notify_finish(void) {
    for (size_t i = 0; i < hist.count; i++) entry_free(hist.items[i]);
    hist.count = 0;
    if (bus_state.bus) stop_bus();
}

#else /* !HAVE_SD_BUS */

bool shell_notify_serve(struct shell *shell, int timeout_ms) {
    (void)shell; (void)timeout_ms;
    fprintf(stderr, "shady-shell: built without sd-bus; no notification server\n");
    return false;
}

bool shell_notify_running(void) { return false; }
bool shell_notify_dismiss(uint32_t id) { (void)id; return false; }
bool shell_notify_invoke(uint32_t id, const char *key) { (void)id; (void)key; return false; }
void shell_notify_clear(void) {}
void shell_notify_finish(void) {}

#endif
