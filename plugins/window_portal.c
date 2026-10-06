#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static shady_shader_program portal_program;
static shady_window portal_target;
static shady_window portal_source;

static bool managed(shady_window window) {
    return window &&
        api->window_valid(host, window) &&
        api->window_mapped(window) &&
        api->window_visible(window) &&
        !api->window_fullscreen(window);
}

static shady_window next_source(shady_window target, shady_window current) {
    size_t count = api->window_count(host);
    if (count < 2) return NULL;

    size_t start = 0;
    if (current) {
        for (size_t i = 0; i < count; ++i) {
            if (api->window_at(host, i) == current) {
                start = (i + 1) % count;
                break;
            }
        }
    } else {
        for (size_t i = 0; i < count; ++i) {
            if (api->window_at(host, i) == target) {
                start = (i + 1) % count;
                break;
            }
        }
    }

    for (size_t offset = 0; offset < count; ++offset) {
        shady_window candidate = api->window_at(host, (start + offset) % count);
        if (candidate != target && managed(candidate))
            return candidate;
    }
    return NULL;
}

static void detach_portal(void) {
    if (portal_target && api->window_valid(host, portal_target)) {
        api->window_reset_shader_source(host, portal_target);
        api->window_reset_shader(host, portal_target);
    }
    portal_target = NULL;
    portal_source = NULL;
    api->schedule_render(host);
}

static bool attach_portal(shady_window target, shady_window source) {
    if (!managed(target) || !managed(source) || target == source)
        return false;

    if (portal_target && portal_target != target)
        detach_portal();

    if (!api->window_set_shader(host, target, portal_program))
        return false;
    if (!api->window_set_shader_source(host, target, source)) {
        api->window_reset_shader(host, target);
        return false;
    }

    portal_target = target;
    portal_source = source;

    const char *source_title = api->window_title(source);
    char message[512];
    snprintf(message, sizeof(message),
        "window-portal: %s now peers into %s",
        api->window_title(target) ? api->window_title(target) : "(window)",
        source_title ? source_title : "(source)");
    api->log(SHADY_PLUGIN_LOG_INFO, message);
    api->schedule_render(host);
    return true;
}

static bool cycle_portal(void) {
    shady_window target = api->focused_window(host);
    if (!managed(target)) return false;

    shady_window source = NULL;
    if (portal_target == target)
        source = next_source(target, portal_source);
    else
        source = next_source(target, NULL);

    if (!source) {
        api->log(SHADY_PLUGIN_LOG_INFO,
            "window-portal: need at least two visible windows");
        return false;
    }
    return attach_portal(target, source);
}

static bool key(struct shady_server *server, const xkb_keysym_t *syms,
        int nsyms, uint32_t key_state, uint32_t modifiers) {
    (void)server;
    if (key_state != SHADY_KEY_PRESSED)
        return false;

    for (int i = 0; i < nsyms; ++i) {
        if (!(modifiers & SHADY_MODIFIER_LOGO) ||
                (syms[i] != XKB_KEY_p && syms[i] != XKB_KEY_P))
            continue;

        if (modifiers & SHADY_MODIFIER_SHIFT) {
            detach_portal();
            api->log(SHADY_PLUGIN_LOG_INFO,
                "window-portal: disabled");
        } else {
            cycle_portal();
        }
        return true;
    }
    return false;
}

static void on_event(shady_host event_host,
        const struct shady_event *event, void *user_data) {
    (void)event_host;
    (void)user_data;

    if (event->type == SHADY_EVENT_WINDOW_DESTROYED ||
            event->type == SHADY_EVENT_WINDOW_UNMAPPED) {
        shady_window changed = event->object.window;
        if (changed == portal_target) {
            portal_target = NULL;
            portal_source = NULL;
            return;
        }
        if (changed == portal_source && portal_target &&
                api->window_valid(host, portal_target)) {
            shady_window replacement = next_source(portal_target, portal_source);
            if (replacement)
                attach_portal(portal_target, replacement);
            else
                detach_portal();
        }
    }
}

static bool init(struct shady_server *server) {
    (void)server;
    const char *root = getenv("SHADY_ROOT");
    if (!root || !*root) root = ".";

    char vert[1024], frag[1024];
    snprintf(vert, sizeof(vert),
        "%s/plugins/shaders/window_portal.vert", root);
    snprintf(frag, sizeof(frag),
        "%s/plugins/shaders/window_portal.frag", root);

    portal_program = api->shader_program_create(host, vert, frag);
    if (!portal_program) {
        api->log(SHADY_PLUGIN_LOG_ERROR,
            "window-portal: failed to create portal shader");
        return false;
    }
    return true;
}

static void start(struct shady_server *server) {
    (void)server;
    api->log(SHADY_PLUGIN_LOG_INFO,
        "window-portal: Super+P opens/cycles a live portal; Super+Shift+P closes it");
}

static void stop(struct shady_server *server) {
    (void)server;
    detach_portal();
}

static void destroy(struct shady_server *server) {
    (void)server;
    if (portal_program)
        api->shader_program_destroy(host, portal_program);
    portal_program = 0;
    portal_target = NULL;
    portal_source = NULL;
}

static const char *const provides[] = {
    "spatial.window-portal",
    NULL,
};

static const char *const requires[] = {
    "spatial.window-state",
    NULL,
};

static const struct shady_module module = {
    .name = "window-portal",
    .provides = provides,
    .requires = requires,
    .init = init,
    .start = start,
    .stop = stop,
    .destroy = destroy,
    .key = key,
};

const struct shady_module *shady_plugin_entry_v1(
        uint32_t host_abi,
        const struct shady_plugin_api_v1 *host_api,
        shady_host host_handle) {
    const size_t portal_api_size =
        offsetof(struct shady_plugin_api_v1, window_shader_source) +
        sizeof(host_api->window_shader_source);

    if (host_abi != SHADY_PLUGIN_ABI_V1 || !host_api ||
            host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
            host_api->struct_size < portal_api_size ||
            !host_api->shader_program_create ||
            !host_api->shader_program_destroy ||
            !host_api->window_set_shader ||
            !host_api->window_reset_shader ||
            !host_api->window_set_shader_source ||
            !host_api->window_reset_shader_source ||
            !host_api->window_shader_source)
        return NULL;

    api = host_api;
    host = host_handle;

    api->subscribe_event(host, SHADY_EVENT_WINDOW_UNMAPPED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_DESTROYED, on_event, NULL);
    return &module;
}
