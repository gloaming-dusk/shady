-- Night Observatory
-- A Shady rice built around depth, calm motion and a "constellation" window layout.
--
-- Run from the repository root:
--   SHADY_LUA_INIT=$PWD/examples/rice/night-observatory/init.lua \
--   WLR_BACKENDS=wayland ./build/shady \
--     -c ./examples/rice/night-observatory/config.lua

local root = os.getenv("SHADY_ROOT") or "."

shady.log("loading rice: Night Observatory")

-- Keep the spatial identity, but make the desktop calm by default.
shady.set("spatial_mode", true)
shady.set("physics_enabled", true)
shady.set("window_gravity", false)
shady.set("window_wobble", true)
shady.set("window_sides", true)
shady.set("shadows", true)
shady.set("floor", true)
shady.set("close_animation", true)
shady.set("fps_mode", true)

-- Use the repository environment as a dim observatory room.
shady.set("sky", true)
shady.set("sky_path", root .. "/sky.ppm")
shady.set("environment_obj", true)
shady.set("environment_obj_path", root .. "/assets/test-room.obj")

-- The rice is intentionally composed from modules rather than baking behavior
-- into the compositor core.
shady.modules({
    ["window-motion"] = true,
    ["physics"] = true,
    ["fps"] = true,
    ["close-animation"] = true,
    ["scene-effects"] = true,
    ["lua"] = true,
})

-- A tiny native layout plugin is the only custom C piece in this rice.
shady.plugin(root .. "/build/libshady-plugin-orbit-layout.so")

-- Core controls. Runtime-only rice controls live in init.lua.
shady.bind("quit", "Super+Shift+Escape")
shady.bind("cycle_windows", "Super+Tab")
shady.bind("close_window", "Super+q")
shady.bind("fps_toggle", "Super+f")
shady.bind("fps_capture", "Super+Shift+f")
shady.bind("gravity_toggle", "Super+g")
shady.bind("debug_ray", "Super+Shift+d")
shady.bind("camera_reset", "Super+0")
shady.bind("camera_zoom_in", "Super+equal")
shady.bind("camera_zoom_out", "Super+minus")
