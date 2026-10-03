#include <stdbool.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;

static bool key(struct shady_server *server, const xkb_keysym_t *syms,
        int nsyms, uint32_t key_state, uint32_t modifiers) {
    (void)server;
    if (key_state != SHADY_KEY_PRESSED ||
            (modifiers & SHADY_MODIFIER_LOGO) == 0)
        return false;

    for (int i = 0; i < nsyms; i++) {
        if (syms[i] != XKB_KEY_b && syms[i] != XKB_KEY_B)
            continue;

        shady_window window = api->focused_window(host);
        if (!window) return true;

        float width = 0.f;
        float color[4] = {0};
        bool overridden = false;
        if (!api->window_border(window, &width, color, &overridden))
            return true;

        if (overridden) {
            if (api->window_reset_border(host, window)) {
                api->window_border(window, &width, color, &overridden);
                api->log(overridden ? SHADY_PLUGIN_LOG_ERROR : SHADY_PLUGIN_LOG_INFO,
                    overridden ? "border-accent: reset verification failed"
                               : "border-accent: reset");
            }
        } else {
            if (api->window_set_border(host, window,
                    7.f, 1.0f, 0.18f, 0.55f, 1.0f)) {
                api->window_border(window, &width, color, &overridden);
                bool ok = overridden && width > 6.9f && width < 7.1f &&
                    color[0] > 0.99f && color[1] > 0.17f && color[1] < 0.19f;
                api->log(ok ? SHADY_PLUGIN_LOG_INFO : SHADY_PLUGIN_LOG_ERROR,
                    ok ? "border-accent: override"
                       : "border-accent: override verification failed");
            }
        }
        return true;
    }
    return false;
}

static void start(struct shady_server *server) {
    (void)server;
    api->log(SHADY_PLUGIN_LOG_INFO,
        "border-accent: Super+B toggles a per-window border override");
}

static const char *const provides[] = {
    "desktop.border-accent",
    NULL,
};

static const struct shady_module module = {
    .name = "border-accent",
    .provides = provides,
    .key = key,
    .start = start,
};

const struct shady_module *shady_plugin_entry_v1(
        uint32_t host_abi,
        const struct shady_plugin_api_v1 *host_api,
        shady_host host_handle) {
    if (host_abi != SHADY_PLUGIN_ABI_V1 ||
            !host_api ||
            host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
            host_api->struct_size < sizeof(*host_api) ||
            !host_api->window_set_border ||
            !host_api->window_border ||
            !host_api->window_reset_border)
        return NULL;

    api = host_api;
    host = host_handle;
    return &module;
}
