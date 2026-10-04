shady.set("spatial_mode", false)
shady.plugins.path(assert(os.getenv("SHADY_TEST_PLUGIN_DIR")))
shady.plugins.load("publish-probe")
