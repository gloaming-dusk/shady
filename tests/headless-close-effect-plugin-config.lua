local root = os.getenv("SHADY_ROOT") or "."
local plugin = assert(os.getenv("SHADY_CLOSE_PLUGIN"))

shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("window_wobble", false)
shady.set("window_sides", true)
shady.set("shadows", false)
shady.set("floor", true)
shady.set("close_animation", true)
shady.set("window_effect_strength", 0.0)
shady.set("window_titlebar", true)

shady.modules({
    ["window-motion"] = true,
    ["physics"] = false,
    ["fps"] = false,
    ["close-animation"] = true,
    ["scene-effects"] = true,
    ["lua"] = true,
})

shady.plugin(plugin)
shady.bind("close_window", "Super+q")
