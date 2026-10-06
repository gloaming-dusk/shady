-- A config may load its own build of a shipped plugin by path; the shipped
-- default must then step aside instead of failing startup.
shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("fps_mode", false)
shady.set("sky", false)
shady.modules({ ["physics"] = false, ["fps"] = false, ["lua"] = true })
shady.plugins.load(assert(os.getenv("SHADY_TEST_PLUGIN_DIR")) .. "/libshady-plugin-window-motion.so")
