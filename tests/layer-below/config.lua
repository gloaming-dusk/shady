-- Spatial mode with the built-in sky on: the sky is an opaque full-screen
-- draw, so background-layer surfaces must come after it.
shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("fps_mode", false)
shady.set("floor", false)
shady.set("shadows", false)
shady.set("sky", true)
shady.set("sky_path", assert(os.getenv("LAYER_BELOW_SKY")))
shady.modules({ ["physics"] = false, ["fps"] = false, ["lua"] = true })
