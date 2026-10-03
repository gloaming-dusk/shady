shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("window_wobble", false)
shady.set("fps_mode", false)
shady.set("window_sides", false)
shady.set("shadows", false)
shady.set("floor", false)
shady.set("sky", false)
shady.set("environment_obj", false)
shady.set("close_animation", false)

shady.modules({
    ["window-motion"] = false,
    ["physics"] = false,
    ["fps"] = false,
    ["close-animation"] = false,
    ["scene-effects"] = false,
    ["lua"] = true,
})
