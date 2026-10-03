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

    expect_float(config.window_border_width, 3.0f, "default border width");
    expect_float(config.window_border_focus_color[0], 0.10f, "default focus red");
    expect_float(config.window_border_focus_color[1], 0.65f, "default focus green");
    expect_float(config.window_border_focus_color[2], 1.00f, "default focus blue");

    expect_true(shady_config_set(&config, "window_border_width", "6.5"),
        "set border width");
    expect_float(config.window_border_width, 6.5f, "stored border width");
    expect_true(shady_config_set(&config, "window_border_width", "0"),
        "allow disabled border");
    expect_true(!shady_config_set(&config, "window_border_width", "-1"),
        "reject negative border");
    expect_true(!shady_config_set(&config, "window_border_width", "33"),
        "reject oversized border");

    expect_true(shady_config_set(&config, "window_border_color", "#123456"),
        "set border color");
    expect_float(config.window_border_color[0], 0x12 / 255.0f, "border red");
    expect_float(config.window_border_color[1], 0x34 / 255.0f, "border green");
    expect_float(config.window_border_color[2], 0x56 / 255.0f, "border blue");

    expect_true(shady_config_set(&config, "window_border_focus_color", "#00D7FF"),
        "set focus border color");
    expect_float(config.window_border_focus_color[1], 0xD7 / 255.0f, "focus green custom");
    expect_float(config.window_border_focus_color[2], 1.0f, "focus blue custom");

    if (failures) return 1;
    puts("config-border: PASS");
    return 0;
}
