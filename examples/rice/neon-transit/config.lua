-- Neon Transit
-- A crisp, high-contrast Shady rice inspired by night trains, terminals and neon signage.

local root = os.getenv("SHADY_ROOT") or "."

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
shady.set("window_opacity", 0.90)
shady.set("window_border_width", 3.0)
shady.set("window_border_color", "#0B3442")
shady.set("window_border_focus_color", "#28E6FF")
shady.set("window_titlebar", true)
shady.set("window_titlebar_height", 28.0)
shady.set("window_titlebar_color", "#071A22")
shady.set("window_titlebar_focus_color", "#0E4052")
shady.set("window_titlebar_text_color", "#E9FBFF")

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

-- Native C spatial mode. Overview is the default because it gives Neon Transit
-- an explicit 3D workspace switcher without competing Z writers. Set
-- SHADY_NEON_DEPTH_MODE=focus to use the older focus-depth behavior instead.
local depth_mode = os.getenv("SHADY_NEON_DEPTH_MODE") or "overview"
if depth_mode == "focus" then
    shady.plugin(os.getenv("SHADY_FOCUS_DEPTH_PLUGIN") or
        (root .. "/build/libshady-plugin-focus-depth.so"))
else
    shady.plugin(os.getenv("SHADY_OVERVIEW_PLUGIN") or
        (root .. "/build/libshady-plugin-spatial-overview.so"))
end

-- Native liquid-surface effect is part of the Neon Transit look by default.
-- Set SHADY_NEON_WATER=0 to disable it when battery/idle usage matters more.
if os.getenv("SHADY_NEON_WATER") ~= "0" then
    shady.plugin(os.getenv("SHADY_WATER_PLUGIN") or
        (root .. "/build/libshady-plugin-water-windows.so"))
end

-- Close style stays on the built-in crumple by default. Opt into the example
-- native close-effect plugin with SHADY_NEON_CLOSE_STYLE=slide.
if os.getenv("SHADY_NEON_CLOSE_STYLE") == "slide" then
    shady.plugin(os.getenv("SHADY_CLOSE_PLUGIN") or
        (root .. "/build/libshady-plugin-close-slide-fade.so"))
end

shady.bind("quit", "Super+Shift+Escape")
shady.bind("cycle_windows", "Super+Tab")
shady.bind("close_window", "Super+q")
shady.bind("fps_toggle", "Super+f")
shady.bind("fps_capture", "Super+Shift+f")
shady.bind("camera_reset", "Super+0")
shady.bind("camera_zoom_in", "Super+equal")
shady.bind("camera_zoom_out", "Super+minus")
