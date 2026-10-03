local plugin = assert(os.getenv("SHADY_BORDER_PLUGIN"))

shady.set("spatial_mode", false)
shady.set("window_border_width", 3.0)
shady.set("window_border_color", "#173747")
shady.set("window_border_focus_color", "#20D7FF")
shady.module("lua", true)
shady.plugin(plugin)
