/*
 * A scripted input method (input-method-unstable-v2) for
 * tests/headless-shell-ime.sh. When a text field activates it, it composes
 * "한" as preedit, then commits "한글", as a Hangul input method would, and
 * reports each step on stdout.
 */
#define _POSIX_C_SOURCE 200809L

#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>

#include "input-method-unstable-v2-client-protocol.h"

static struct wl_seat *seat;
static struct zwp_input_method_manager_v2 *manager;
static struct zwp_input_method_v2 *input_method;
static uint32_t serial; /* done events received: what commit() refers to */
static bool active, pending_active, unavailable;
static int step; /* 0 waiting, 1 preedit sent, 2 committed */

static void activate(void *data, struct zwp_input_method_v2 *im) {
    (void)data; (void)im;
    pending_active = true;
}

static void deactivate(void *data, struct zwp_input_method_v2 *im) {
    (void)data; (void)im;
    pending_active = false;
}

static void surrounding_text(void *data, struct zwp_input_method_v2 *im, const char *text,
        uint32_t cursor, uint32_t anchor) {
    (void)data; (void)im; (void)text; (void)cursor; (void)anchor;
}

static void text_change_cause(void *data, struct zwp_input_method_v2 *im, uint32_t cause) {
    (void)data; (void)im; (void)cause;
}

static void content_type(void *data, struct zwp_input_method_v2 *im, uint32_t hint,
        uint32_t purpose) {
    (void)data; (void)im; (void)hint; (void)purpose;
}

static void done(void *data, struct zwp_input_method_v2 *im) {
    (void)data; (void)im;
    serial++;
    if (pending_active && !active) printf("ime: activated\n");
    active = pending_active;
    fflush(stdout);
}

static void im_unavailable(void *data, struct zwp_input_method_v2 *im) {
    (void)data; (void)im;
    unavailable = true;
}

static const struct zwp_input_method_v2_listener input_method_listener = {
    .activate = activate,
    .deactivate = deactivate,
    .surrounding_text = surrounding_text,
    .text_change_cause = text_change_cause,
    .content_type = content_type,
    .done = done,
    .unavailable = im_unavailable,
};

static void global(void *data, struct wl_registry *registry, uint32_t name,
        const char *interface, uint32_t version) {
    (void)data; (void)version;
    if (!strcmp(interface, wl_seat_interface.name) && !seat)
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    else if (!strcmp(interface, zwp_input_method_manager_v2_interface.name))
        manager = wl_registry_bind(registry, name, &zwp_input_method_manager_v2_interface, 1);
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = { global, global_remove };

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(void) {
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "ime-probe: no display\n");
        return 1;
    }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!seat || !manager) {
        fprintf(stderr, "ime-probe: no seat or input method manager\n");
        return 1;
    }
    input_method = zwp_input_method_manager_v2_get_input_method(manager, seat);
    zwp_input_method_v2_add_listener(input_method, &input_method_listener, NULL);
    wl_display_roundtrip(display);
    printf("ime: ready\n");
    fflush(stdout);

    double started = now(), stepped = 0;
    while (!unavailable && now() - started < 20) {
        if (active && step == 0) {
            zwp_input_method_v2_set_preedit_string(input_method, "한", 0, (int32_t)strlen("한"));
            zwp_input_method_v2_commit(input_method, serial);
            printf("ime: preedit sent\n");
            step = 1;
            stepped = now();
        } else if (active && step == 1 && now() - stepped > 0.5) {
            zwp_input_method_v2_commit_string(input_method, "한글");
            zwp_input_method_v2_commit(input_method, serial);
            printf("ime: commit sent\n");
            step = 2;
            stepped = now();
        } else if (step == 2 && now() - stepped > 0.5) {
            break;
        }
        fflush(stdout);
        wl_display_flush(display);
        struct pollfd pfd = { .fd = wl_display_get_fd(display), .events = POLLIN };
        if (poll(&pfd, 1, 50) > 0 && wl_display_dispatch(display) < 0) break;
        wl_display_dispatch_pending(display);
    }
    if (unavailable) fprintf(stderr, "ime-probe: another input method is active\n");
    zwp_input_method_v2_destroy(input_method);
    wl_display_disconnect(display);
    return step == 2 ? 0 : 1;
}
