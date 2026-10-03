local plugin = assert(os.getenv("SHADY_WATER_PLUGIN"))

shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("window_wobble", false)
shady.set("window_sides", false)
shady.set("shadows", false)
shady.set("floor", false)
shady.set("close_animation", false)
shady.set("fps_mode", true)
shady.set("window_effect_strength", 0.0)
shady.set("window_brightness", 1.0)
shady.set("background_top", "#080A0F")
shady.set("background_horizon", "#080A0F")
shady.set("background_bottom", "#080A0F")

local root = os.getenv("SHADY_ROOT") or "."
shady.plugin(root .. "/build/libshady-plugin-fps-cube.so")

shady.modules({
    ["window-motion"] = true,
    ["physics"] = false,
    ["fps"] = true,
    ["close-animation"] = false,
    ["scene-effects"] = true,
    ["lua"] = true,
})

shady.plugin(plugin)
