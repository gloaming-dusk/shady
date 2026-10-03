local root = os.getenv("SHADY_ROOT") or "."

shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("window_wobble", false)
shady.set("shadows", false)
shady.set("floor", true)
shady.set("window_titlebar", true)

shady.bind("quit", "Escape")

shady.modules({
    ["window-motion"] = true,
    ["physics"] = false,
    ["fps"] = true,
    ["close-animation"] = true,
    ["scene-effects"] = true,
    ["lua"] = true,
})

shady.plugin(root .. "/build/libshady-plugin-fps-cube.so")
shady.plugin(root .. "/build/libshady-plugin-window-portal.so")
