/* Drive the bad-apple plugin against a fake host: a missing frames.bin is
 * reported and nothing starts; Super+A runs intro -> playing (the audio
 * player starts) -> outro -> idle on a three-frame file; Super+A during
 * playback stops it and the player. Render hooks need GL and are not run.
 * Uses only the public plugin API. */
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <dlfcn.h>
#include <shady/plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

static int host_object;
#define HOST ((shady_host)&host_object)
static char logs[16384];
static int hooks;

static void log_msg(enum shady_plugin_log_level l, const char *m) {
    (void)l;
    fprintf(stderr, "[plugin] %s\n", m);
    strncat(logs, m, sizeof(logs) - strlen(logs) - 2);
    strcat(logs, "\n");
}
static shady_shader_program program_create(shady_host h, const char *v, const char *f) {
    (void)h; assert(strstr(v, "overlay.vert") && strstr(f, "bad_apple.frag")); return 7;
}
static bool program_destroy(shady_host h, shady_shader_program p) { (void)h; return p == 7; }
static shady_render_hook_id hook_add(shady_host h, uint32_t stage, shady_render_callback cb, void *u) {
    (void)h; (void)cb; (void)u; assert(stage == SHADY_RENDER_STAGE_OVERLAY); return (shady_render_hook_id)++hooks;
}
static bool hook_remove(shady_host h, shady_render_hook_id id) { (void)h; (void)id; hooks--; return true; }
static bool draw_scene(shady_host h, shady_shader_program p) { (void)h; (void)p; return true; }
static void schedule(shady_host h) { (void)h; }

static struct shady_plugin_api_v1 api = {
    .abi_version = SHADY_PLUGIN_ABI_V1, .struct_size = sizeof(api), .log = log_msg,
    .shader_program_create = program_create, .shader_program_destroy = program_destroy,
    .render_hook_add = hook_add, .render_hook_remove = hook_remove,
    .shader_draw_fullscreen_scene = draw_scene, .schedule_render = schedule,
};

static const struct shady_module *module;
static char dir[256], marker[300];

static bool super_a(void) {
    xkb_keysym_t sym = XKB_KEY_a;
    return module->key((struct shady_server *)HOST, &sym, 1, SHADY_KEY_PRESSED, SHADY_MODIFIER_LOGO);
}

static void run(float seconds) {
    for (float t = 0; t < seconds; t += 1.f / 60.f)
        module->tick((struct shady_server *)HOST, 1.f / 60.f, 1920.f, 1080.f);
}

static void sleep_ms(long ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* Three 10x2 frames at 30 fps: two columns of bytes per row. */
static void write_frames(void) {
    char path[300];
    snprintf(path, sizeof(path), "%s/frames.bin", dir);
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs("shady-bad-apple 1 10 2 30 1\n", f);
    for (int frame = 0; frame < 3; frame++)
        for (int row = 0; row < 2; row++) {
            unsigned char bytes[2] = { frame & 1 ? 0xff : 0x00, 0xc0 };
            fwrite(bytes, 1, 2, f);
        }
    fclose(f);
    snprintf(path, sizeof(path), "%s/audio.wav", dir);
    f = fopen(path, "wb");
    assert(f);
    fputs("RIFF", f);
    fclose(f);
}

static bool player_started(void) {
    for (int i = 0; i < 100; i++) {
        if (access(marker, F_OK) == 0) return true;
        sleep_ms(10);
    }
    return false;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    snprintf(dir, sizeof(dir), "/tmp/shady-bad-apple-test.XXXXXX");
    assert(mkdtemp(dir));
    snprintf(marker, sizeof(marker), "%s/played", dir);
    setenv("SHADY_BAD_APPLE_DIR", dir, 1);
    /* The player records that it ran, then waits to be stopped. */
    char player[400];
    snprintf(player, sizeof(player), "sh -c 'touch %s; exec sleep 30'", marker);
    setenv("SHADY_BAD_APPLE_PLAYER", player, 1);

    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    shady_plugin_entry_v1_fn entry = (shady_plugin_entry_v1_fn)dlsym(handle, SHADY_PLUGIN_ENTRY_V1);
    /* Hosts without scene capture are rejected. */
    uint32_t full = api.struct_size;
    api.struct_size = offsetof(struct shady_plugin_api_v1, shader_draw_fullscreen_scene);
    assert(!entry(SHADY_PLUGIN_ABI_V1, &api, HOST));
    api.struct_size = full;
    module = entry(SHADY_PLUGIN_ABI_V1, &api, HOST);
    assert(module && !strcmp(module->name, "bad-apple"));
    assert(module->init((struct shady_server *)HOST) && hooks == 1);
    module->start((struct shady_server *)HOST);

    /* Other keys and modifiers are not ours. */
    xkb_keysym_t other = XKB_KEY_b;
    assert(!module->key((struct shady_server *)HOST, &other, 1, SHADY_KEY_PRESSED, SHADY_MODIFIER_LOGO));
    xkb_keysym_t a = XKB_KEY_a;
    assert(!module->key((struct shady_server *)HOST, &a, 1, SHADY_KEY_PRESSED,
        SHADY_MODIFIER_LOGO | SHADY_MODIFIER_SHIFT));

    /* No frames yet: reported, and nothing plays. */
    assert(super_a());
    assert(strstr(logs, "no frames at"));
    run(2.f);
    assert(!strstr(logs, "playing"));

    /* A whole show: the intro, three frames with the player, the outro. */
    write_frames();
    assert(super_a());
    assert(strstr(logs, "3 frames of 10x2 at 30.000 fps"));
    assert(strstr(logs, "playing (Super+A stops)"));
    assert(access(marker, F_OK) != 0);
    run(1.1f);
    assert(player_started());
    sleep_ms(200);
    run(0.1f);
    assert(strstr(logs, "finished"));
    run(2.f);
    /* Idle again, so Super+A starts a new show. */
    logs[0] = 0;
    unlink(marker);
    assert(super_a());
    assert(strstr(logs, "playing"));

    /* Stopped mid-show: the outro runs and the player is not started. */
    assert(super_a());
    assert(strstr(logs, "stopped"));
    run(2.f);
    sleep_ms(100);
    assert(access(marker, F_OK) != 0);

    module->stop((struct shady_server *)HOST);
    module->destroy((struct shady_server *)HOST);
    assert(hooks == 0);

    char path[300];
    snprintf(path, sizeof(path), "%s/frames.bin", dir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/audio.wav", dir);
    unlink(path);
    unlink(marker);
    rmdir(dir);
    printf("bad-apple plugin test passed\n");
    return 0;
}
