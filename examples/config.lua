-- Shady bootstrap configuration.
-- This file runs before module dependency resolution.

shady.set("spatial_mode", true)
shady.set("window_gravity", false)
shady.set("window_wobble", true)
shady.set("shadows", true)
shady.set("floor", true)
shady.set("window_opacity", 1.0)
shady.set("window_border_width", 3.0)
shady.set("window_border_color", "#0E3344")
shady.set("window_border_focus_color", "#1FAFE0")

shady.bind("quit", "Escape")
shady.bind("fps_toggle", "F2")
shady.bind("gravity_toggle", "F4")

-- Runtime module selection is independent from compile-time Meson options.
shady.modules({
    ["window-motion"] = true,
    ["physics"] = true,
    ["fps"] = true,
    ["close-animation"] = true,
    ["scene-effects"] = true,
    ["lua"] = true,
})

-- External native plugins are loaded before dependency resolution:
-- shady.plugin("/absolute/path/to/my-plugin.so")
