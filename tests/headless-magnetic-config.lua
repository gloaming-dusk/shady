local root = os.getenv("SHADY_ROOT") or "."

shady.set("spatial_mode", true)
shady.set("window_gravity", false)
shady.set("window_wobble", false)
shady.set("shadows", false)
shady.set("floor", true)
shady.set("window_titlebar", false)

shady.bind("quit", "Escape")
shady.bind("fps_toggle", "F2")

shady.modules({
    ["window-motion"] = true,
    ["physics"] = true,
    ["fps"] = true,
    ["close-animation"] = true,
    ["scene-effects"] = true,
    ["lua"] = true,
})

shady.plugin(os.getenv("SHADY_FPS_REPRESENTATION_PLUGIN") or
    (root .. "/build/libshady-plugin-fps-cube.so"))
shady.plugin(root .. "/build/libshady-plugin-magnetic-windows.so")
