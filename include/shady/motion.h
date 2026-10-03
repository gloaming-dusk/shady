#ifndef SHADY_PUBLIC_MOTION_H
#define SHADY_PUBLIC_MOTION_H
#include <stdbool.h>
#include <shady/types.h>
#define SHADY_MOTION_API "shady.window-motion"
#define SHADY_MOTION_API_VERSION 1u

/* Render data only. Spring velocities and drag history belong to the plugin. */
struct shady_motion_visual {
    float wobble_x, wobble_y;
    float tilt_x, tilt_y;
    bool animating;
};

/* A driver is immutable, plugin-owned storage that remains valid until detach.
 * The host removes it before dlclose; callbacks run on the compositor thread. */
struct shady_motion_driver {
    uint32_t struct_size;
    void (*impulse)(shady_host, shady_window, float, float, float, float);
    void (*begin_drag)(shady_host, shady_window, double, double);
    void (*drag)(shady_host, shady_window, double, double);
    void (*reset)(shady_host, shady_window);
    void (*damp)(shady_host, shady_window, float);
};

struct shady_motion_api_v1 {
    uint32_t struct_size;
    bool (*spatial_enabled)(shady_host);
    bool (*wobble_enabled)(shady_host);
    bool (*attach)(shady_host, const struct shady_motion_driver *);
    bool (*detach)(shady_host, const struct shady_motion_driver *);
    /* Commands can be issued by any plugin; only the attached driver may
     * publish visual data. Return false when no driver or window is available. */
    bool (*add_impulse)(shady_host, shady_window, float, float, float, float);
    bool (*reset)(shady_host, shady_window);
    bool (*damp)(shady_host, shady_window, float);
    bool (*get_visual)(shady_host, shady_window, struct shady_motion_visual *);
    bool (*set_visual)(shady_host, shady_window, const struct shady_motion_visual *);
};
#endif
