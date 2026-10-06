#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static shady_shader_program program;
static shady_render_hook_id hook;
static bool drew_once;

static void draw_overlay(shady_host h, const struct shady_render_context *ctx,
        void *user_data) {
    (void)user_data;
    if (!program || !ctx) return;
    api->shader_uniform_float(h, program, "u_time", ctx->time_seconds);
    api->shader_uniform_vec2(h, program, "u_resolution",
        (float)ctx->width, (float)ctx->height);
    api->shader_draw_fullscreen(h, program);
    if (!drew_once) {
        api->log(SHADY_PLUGIN_LOG_INFO, "shader-overlay: first frame rendered");
        drew_once = true;
    }
}

static bool plugin_init(struct shady_server *server) {
    (void)server;
    const char *root = getenv("SHADY_ROOT");
    char vert[1024], frag[1024];
    if (!root || !*root) root = ".";
    snprintf(vert, sizeof(vert), "%s/plugins/shaders/overlay.vert", root);
    snprintf(frag, sizeof(frag), "%s/plugins/shaders/overlay.frag", root);
    program = api->shader_program_create(host, vert, frag);
    if (!program) {
        api->log(SHADY_PLUGIN_LOG_ERROR, "shader-overlay: failed to create shader program");
        return false;
    }
    hook = api->render_hook_add(host, SHADY_RENDER_STAGE_OVERLAY, draw_overlay, NULL);
    if (!hook) {
        api->shader_program_destroy(host, program);
        program = 0;
        return false;
    }
    api->log(SHADY_PLUGIN_LOG_INFO, "shader-overlay: enabled");
    api->schedule_render(host);
    return true;
}

static void plugin_destroy(struct shady_server *server) {
    (void)server;
    if (hook) api->render_hook_remove(host, hook);
    if (program) api->shader_program_destroy(host, program);
    hook = 0;
    program = 0;
}

static const char *provides[] = {"shader-overlay", NULL};
static const struct shady_module module = {
    .name = "shader-overlay",
    .provides = provides,
    .init = plugin_init,
    .destroy = plugin_destroy,
};

const struct shady_module *shady_plugin_entry_v1(uint32_t host_abi,
        const struct shady_plugin_api_v1 *host_api, shady_host host_handle) {
    if (host_abi != SHADY_PLUGIN_ABI_V1 || !host_api ||
            host_api->struct_size < offsetof(struct shady_plugin_api_v1, render_hook_remove) +
                sizeof(host_api->render_hook_remove))
        return NULL;
    api = host_api;
    host = host_handle;
    return &module;
}
