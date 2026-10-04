-- Afterglow
-- A low sun over a mirror-still sea. Windows float like warm glass panels
-- above the water; the sky, the sea and every accent move through four hours
-- of light together (Super+T).

local root = os.getenv("SHADY_ROOT") or "."

shady.log("loading rice: Afterglow")

shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("window_wobble", true)
shady.set("window_sides", true)
shady.set("shadows", true)
-- No grid floor: the Afterglow sky plugin draws an open sea down to the
-- horizon, and window shadows land directly on the water.
shady.set("floor", false)
shady.set("close_animation", true)
shady.set("fps_mode", true)
shady.set("sky", false)
shady.set("environment", false)

-- Fallback gradient (the plugin keeps it on palette as the hour changes).
shady.set("background_top", "#0E0F33")
shady.set("background_horizon", "#FF8061")
shady.set("background_bottom", "#07081A")

-- Warm glass: slightly translucent, softly rounded, thin luminous rim.
shady.set("window_tint", "#FFF4EA")
shady.set("window_effect_strength", 0.04)
shady.set("window_brightness", 1.08)
shady.set("window_opacity", 0.95)
shady.set("window_corner_radius", 14.0)
shady.set("window_border_width", 2.0)
shady.set("window_border_color", "#3B2A40")
shady.set("window_border_focus_color", "#FFB070")
shady.set("window_titlebar", true)
shady.set("window_titlebar_height", 30.0)
shady.set("window_titlebar_color", "#17111F")
shady.set("window_titlebar_focus_color", "#2A1B30")
shady.set("window_titlebar_text_color", "#FFEEDD")

-- Used only if the floor is re-enabled (e.g. SHADY_AFTERGLOW_FLOOR=1).
shady.set("floor_base_color", "#08060E")
shady.set("floor_grid_color", "#FF7A5C")
shady.set("floor_grid_strength", 0.08)
shady.set("floor_major_strength", 0.18)
shady.set("floor_fade_start", 0.9)
shady.set("floor_fade_end", 4.0)
shady.set("floor_horizon_fog", 1.0)
if os.getenv("SHADY_AFTERGLOW_FLOOR") == "1" then
    shady.set("floor", true)
end

shady.modules({
    ["window-motion"] = true,
    ["physics"] = false,
    ["fps"] = true,
    ["close-animation"] = true,
    ["scene-effects"] = true,
    ["lua"] = true,
})

local function plugin(env, name)
    shady.plugin(os.getenv(env) or (root .. "/build/libshady-plugin-" .. name .. ".so"))
end

-- Sky, sea, time of day, accent sync and the "sink into the sea" close.
plugin("SHADY_AFTERGLOW_PLUGIN", "afterglow")
-- FPS representation stays swappable; the paper fold suits the calm mood.
plugin("SHADY_FPS_REPRESENTATION_PLUGIN", "fps-folded-paper")
-- Super+O: 3D overview of every window above the water.
plugin("SHADY_OVERVIEW_PLUGIN", "spatial-overview")

shady.bind("quit", "Super+Shift+Escape")
shady.bind("cycle_windows", "Super+Tab")
shady.bind("close_window", "Super+q")
shady.bind("fps_toggle", "Super+f")
shady.bind("fps_capture", "Super+Shift+f")
shady.bind("camera_reset", "Super+0")
shady.bind("camera_zoom_in", "Super+equal")
shady.bind("camera_zoom_out", "Super+minus")
