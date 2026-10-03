shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("window_wobble", false)
shady.set("window_titlebar", true)
shady.set("window_titlebar_height", "28")
shady.set("close_animation", false)
shady.set("window_effect_strength", 0.0)

shady.modules({
    ["window-motion"] = true,
    ["physics"] = false,
    ["fps"] = false,
    ["close-animation"] = false,
    ["scene-effects"] = true,
    ["lua"] = true,
})
