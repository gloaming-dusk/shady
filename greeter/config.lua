-- The Tide greeter: the compositor greetd runs before anyone logs in, with
-- Tide's night sea behind the login card (shell.lua). Nothing on it can start
-- a program: no launcher, no terminal, no window keys.

shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("window_wobble", false)
shady.set("floor", false)
shady.set("sky", false)
shady.set("environment", false)
shady.set("shadows", false)
shady.set("background_color", "#0F0F1A")

shady.modules({
    ["window-motion"] = false,
    ["physics"] = false,
    ["fps"] = false,
    ["close-animation"] = false,
    ["scene-effects"] = false,
    ["lua"] = true,
})

-- Keys belong to the login card. (The default Escape would quit.)
for _, action in ipairs({
    "quit", "cycle_windows", "close_window", "fps_toggle", "fps_capture",
    "gravity_toggle", "debug_ray", "camera_left", "camera_right", "camera_up",
    "camera_down", "camera_yaw_left", "camera_yaw_right", "camera_zoom_in",
    "camera_zoom_out", "camera_reset", "show_shortcuts",
}) do
    shady.bind(action, "none")
end
