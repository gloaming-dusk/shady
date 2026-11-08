-- Shady greeter: the compositor greetd runs before anyone logs in.
-- Afterglow's sky and sea behind the login card (greeter/shell.lua), and
-- nothing a visitor could use to run programs: no launcher, terminal or
-- window bindings.

local root = os.getenv("SHADY_ROOT") or "."

shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("floor", false)
shady.set("sky", false)
shady.set("environment", false)
shady.set("shadows", false)

-- Fallback gradient if the sky plugin cannot load.
shady.set("background_top", "#0E0F33")
shady.set("background_horizon", "#FF8061")
shady.set("background_bottom", "#07081A")

shady.modules({
    ["window-motion"] = false,
    ["physics"] = false,
    ["fps"] = false,
    ["close-animation"] = true, -- the afterglow plugin needs it
    ["scene-effects"] = true,
    ["lua"] = true,
})

shady.plugin(os.getenv("SHADY_AFTERGLOW_PLUGIN") or
    (root .. "/build/libshady-plugin-afterglow.so"))

-- Unbind every compositor shortcut: keys belong to the login card. (The
-- default Escape would otherwise quit the greeter.)
for _, action in ipairs({
    "quit", "cycle_windows", "close_window", "fps_toggle", "fps_capture",
    "gravity_toggle", "debug_ray", "camera_left", "camera_right", "camera_up",
    "camera_down", "camera_yaw_left", "camera_yaw_right", "camera_zoom_in",
    "camera_zoom_out", "camera_reset",
}) do
    shady.bind(action, "none")
end
