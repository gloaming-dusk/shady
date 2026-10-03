#include <math.h>
#include <stdio.h>

#include "shady.h"

static int failures = 0;

static void expect_true(bool value, const char *name) {
    if (!value) {
        fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    }
}

static void expect_float(float actual, float expected, const char *name) {
    if (fabsf(actual - expected) > 0.0001f) {
        fprintf(stderr, "FAIL: %s got %.4f expected %.4f\n",
            name, actual, expected);
        failures++;
    }
}

int main(void) {
    struct shady_config config;
    shady_config_defaults(&config);

    expect_true(config.window_titlebar, "titlebar enabled by default");
    expect_float(config.window_titlebar_height, 28.0f, "default titlebar height");

    expect_true(shady_config_set(&config, "window_titlebar", "false"),
        "disable titlebar");
    expect_true(!config.window_titlebar, "titlebar disabled");
    expect_true(shady_config_set(&config, "window_titlebar", "true"),
        "enable titlebar");
    expect_true(config.window_titlebar, "titlebar re-enabled");

    expect_true(shady_config_set(&config, "window_titlebar_height", "34"),
        "set titlebar height");
    expect_float(config.window_titlebar_height, 34.0f, "stored titlebar height");
    expect_true(!shady_config_set(&config, "window_titlebar_height", "15"),
        "reject short titlebar");
    expect_true(!shady_config_set(&config, "window_titlebar_height", "65"),
        "reject tall titlebar");

    expect_true(shady_config_set(&config, "window_titlebar_color", "#123456"),
        "set titlebar color");
    expect_float(config.window_titlebar_color[0], 0x12 / 255.0f, "titlebar red");
    expect_float(config.window_titlebar_color[1], 0x34 / 255.0f, "titlebar green");
    expect_float(config.window_titlebar_color[2], 0x56 / 255.0f, "titlebar blue");

    expect_true(shady_config_set(&config, "window_titlebar_focus_color", "#16708A"),
        "set focused titlebar color");
    expect_true(shady_config_set(&config, "window_titlebar_text_color", "#EAF9FF"),
        "set titlebar text color");

    if (failures) return 1;
    puts("config-titlebar: PASS");
    return 0;
}
