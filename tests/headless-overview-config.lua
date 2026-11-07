shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("window_wobble", false)
shady.set("window_sides", false)
shady.set("shadows", false)
shady.set("floor", false)
shady.set("close_animation", false)
shady.set("fps_mode", false)
shady.set("layout", "scrolling")
shady.set("layout_gap", 10)
shady.set("layout_default_width", 0.5)
shady.modules({
    ["window-motion"] = false,
    ["physics"] = false,
    ["fps"] = false,
    ["close-animation"] = false,
    ["scene-effects"] = true,
    ["lua"] = true,
})
shady.bind("quit", "none")
