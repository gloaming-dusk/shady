-- Neon Transit
-- A crisp, high-contrast Shady rice inspired by night trains, terminals and neon signage.

shady.log("loading rice: Neon Transit")

shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("window_wobble", false)
shady.set("window_sides", true)
shady.set("shadows", true)
shady.set("floor", true)
shady.set("close_animation", true)
shady.set("fps_mode", true)

-- Near-black sky with a violet horizon and electric cyan grid.
shady.set("background_top", "#05040A")
shady.set("background_horizon", "#160B27")
shady.set("background_bottom", "#020308")
shady.set("window_tint", "#F8F5FF")
shady.set("window_effect_strength", 0.12)
shady.set("window_brightness", 1.28)

shady.set("floor_base_color", "#03040A")
shady.set("floor_grid_color", "#00D9FF")
shady.set("floor_grid_strength", 0.22)
shady.set("floor_major_strength", 0.42)
shady.set("floor_fade_start", 1.2)
shady.set("floor_fade_end", 5.5)

shady.set("sky", false)
shady.set("environment_obj", false)

shady.modules({
    ["window-motion"] = true,
    ["physics"] = false,
    ["fps"] = true,
    ["close-animation"] = true,
    ["scene-effects"] = true,
    ["lua"] = true,
})

shady.bind("quit", "Super+Shift+Escape")
shady.bind("cycle_windows", "Super+Tab")
shady.bind("close_window", "Super+q")
shady.bind("fps_toggle", "Super+f")
shady.bind("fps_capture", "Super+Shift+f")
shady.bind("camera_reset", "Super+0")
shady.bind("camera_zoom_in", "Super+equal")
shady.bind("camera_zoom_out", "Super+minus")
