-- Shady bootstrap configuration.
-- This file runs before module dependency resolution.

shady.set("spatial_mode", true)
shady.set("window_gravity", false)
shady.set("window_wobble", true)
shady.set("shadows", true)
shady.set("floor", true)

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
