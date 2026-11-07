-- Test fixture: the configuration of the Neon Transit rice
-- (gloaming-dusk/shady-rice-neon-transit), for the spatial suite.

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
shady.set("environment", false)

shady.modules({
    ["window-motion"] = true,
    ["physics"] = false,
    ["fps"] = true,
    ["close-animation"] = true,
    ["scene-effects"] = true,
    ["lua"] = true,
})

-- FPS interaction no longer owns window geometry. Keep the familiar cube
-- representation as a swappable native plugin.
shady.plugin(os.getenv("SHADY_FPS_REPRESENTATION_PLUGIN") or
    (root .. "/build/libshady-plugin-fps-cube.so"))

-- Native C spatial mode. Overview is the default because it gives Neon Transit
-- an explicit 3D workspace switcher without competing position writers. Magnetic
-- Windows and Window Constellation also write window positions, so either opt-in mode
-- skips the default overview plugin unless a depth mode is explicitly requested.
local magnetic_enabled = os.getenv("SHADY_NEON_MAGNETIC") == "1"
local constellation_enabled = os.getenv("SHADY_NEON_CONSTELLATION") == "1"
local portal_enabled = os.getenv("SHADY_NEON_PORTAL") == "1"
if magnetic_enabled and constellation_enabled then
    shady.log("Neon Transit: constellation takes precedence over magnetic window motion")
    magnetic_enabled = false
end
local spatial_motion_plugin = magnetic_enabled or constellation_enabled
local explicit_depth_mode = os.getenv("SHADY_NEON_DEPTH_MODE")
local depth_mode = explicit_depth_mode or "overview"
if not spatial_motion_plugin or explicit_depth_mode then
    if depth_mode == "focus" then
        shady.plugin(os.getenv("SHADY_FOCUS_DEPTH_PLUGIN") or
            (root .. "/build/libshady-plugin-focus-depth.so"))
    else
        shady.plugin(os.getenv("SHADY_OVERVIEW_PLUGIN") or
            (root .. "/build/libshady-plugin-spatial-overview.so"))
    end
end

shady.bind("quit", "Super+Shift+Escape")
shady.bind("cycle_windows", "Super+Tab")
shady.bind("close_window", "Super+q")
shady.bind("fps_toggle", "Super+f")
shady.bind("fps_capture", "Super+Shift+f")
shady.bind("camera_reset", "Super+0")
shady.bind("camera_zoom_in", "Super+equal")
shady.bind("camera_zoom_out", "Super+minus")
