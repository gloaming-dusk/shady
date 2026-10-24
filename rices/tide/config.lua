-- Tide
-- An everyday desktop. Windows tile in a strip of columns that scrolls
-- sideways, niri's model: a new window opens beside the focused one and the
-- strip glides to keep the focus on screen. Above it floats a quiet glass
-- bar with a minimap of the strip; the rest of the shell (launcher, control
-- center, notifications, OSD, lock) is in shell.lua.
--
-- TIDE_LIGHT=1 draws through wlroots' scene graph instead of the GL
-- renderer: no rounded corners or glass, but the least GPU work there is.

local light = os.getenv("TIDE_LIGHT") == "1"

shady.log("loading rice: Tide" .. (light and " (light)" or ""))

shady.set("spatial_mode", not light)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("window_wobble", false)
shady.set("window_sides", false)
shady.set("window_focused_exact", true)
shady.set("shadows", true)
shady.set("floor", false)
shady.set("sky", false)
shady.set("environment", false)
shady.set("fps_mode", false)
shady.set("close_animation", true)

-- The layout.
shady.set("layout", "scrolling")
shady.set("layout_gap", 10)
shady.set("layout_default_width", 0.5)
shady.set("layout_center_single", true)
shady.set("layout_animation", 22)
shady.set("gesture_fingers", 3)

-- Windows draw no title bars of their own (prefer-no-csd) and get none from
-- Shady: a thin border and rounded corners are enough.
shady.set("window_titlebar", false)
shady.set("prefer_no_csd", true)
shady.set("window_corner_radius", 12.0)
shady.set("window_border_width", 2.0)
shady.set("window_border_color", "#24242F")
shady.set("window_border_focus_color", "#A58BFA")
shady.set("window_tint", "#FFFFFF")
shady.set("window_effect_strength", 0.0)
shady.set("window_brightness", 1.0)
shady.set("window_opacity", 1.0)

-- Behind the wallpaper, in case the shell is not running.
shady.set("background_top", "#11111B")
shady.set("background_horizon", "#1B1A2B")
shady.set("background_bottom", "#0D0D14")
shady.set("background_color", "#11111B")

-- Input.
shady.set("touchpad_tap", true)
shady.set("touchpad_natural_scroll", true)
shady.set("numlock", true)
shady.set("repeat_rate", 30)
shady.set("repeat_delay", 300)
if os.getenv("TIDE_XKB_LAYOUT") then shady.set("xkb_layout", os.getenv("TIDE_XKB_LAYOUT")) end
if os.getenv("TIDE_XKB_OPTIONS") then shady.set("xkb_options", os.getenv("TIDE_XKB_OPTIONS")) end

shady.modules({
    ["window-motion"] = false,
    ["physics"] = false,
    ["fps"] = false,
    ["close-animation"] = true,
    ["scene-effects"] = not light,
    ["lua"] = true,
})

-- Nothing on a bare key or on Alt: those belong to the applications. Shady's
-- first-person and camera keys are off; Tide's own keys are in init.lua.
shady.bind("quit", "none")
shady.bind("close_window", "none")
shady.bind("cycle_windows", "Alt+Tab")
shady.bind("show_shortcuts", "Super+Shift+slash")
for _, action in ipairs({ "fps_toggle", "fps_capture", "gravity_toggle", "debug_ray",
        "camera_left", "camera_right", "camera_up", "camera_down",
        "camera_yaw_left", "camera_yaw_right", "camera_zoom_in", "camera_zoom_out",
        "camera_reset" }) do
    shady.bind(action, "none")
end
