local root = os.getenv("SHADY_ROOT") or "."
local plugin = assert(os.getenv("SHADY_OVERVIEW_PLUGIN"))

shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("window_wobble", false)
shady.set("window_sides", true)
shady.set("shadows", false)
shady.set("floor", true)
shady.set("close_animation", false)
shady.set("fps_mode", true)

shady.modules({
    ["window-motion"] = true,
    ["physics"] = false,
    ["fps"] = true,
    ["close-animation"] = false,
    ["scene-effects"] = true,
    ["lua"] = true,
})

shady.plugin(plugin)
shady.bind("quit", "Super+Shift+Escape")
