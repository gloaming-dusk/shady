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

    expect_float(config.window_opacity, 1.0f, "default window_opacity");

    expect_true(shady_config_set(&config, "window_opacity", "0.72"),
        "set valid window_opacity");
    expect_float(config.window_opacity, 0.72f, "stored window_opacity");

    expect_true(shady_config_set(&config, "window_opacity", "0.0"),
        "allow fully transparent");
    expect_float(config.window_opacity, 0.0f, "zero window_opacity");

    expect_true(shady_config_set(&config, "window_opacity", "1.0"),
        "allow fully opaque");
    expect_float(config.window_opacity, 1.0f, "one window_opacity");

    expect_true(!shady_config_set(&config, "window_opacity", "-0.01"),
        "reject negative opacity");
    expect_true(!shady_config_set(&config, "window_opacity", "1.01"),
        "reject opacity over one");

    if (failures) return 1;
    puts("config-opacity: PASS");
    return 0;
}
