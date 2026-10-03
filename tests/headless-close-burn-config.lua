shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("window_wobble", false)
shady.set("window_titlebar", true)
shady.set("window_titlebar_height", 28)
shady.set("close_animation", true)

local root = os.getenv("SHADY_ROOT") or "."
shady.plugin(root .. "/build/libshady-plugin-close-burn.so")

shady.modules({
    ["window-motion"] = true,
    ["physics"] = false,
    ["fps"] = false,
    ["close-animation"] = true,
    ["scene-effects"] = true,
    ["lua"] = true,
    ["close-burn"] = true,
})
