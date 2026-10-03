-- Shady bootstrap configuration.
-- This file runs before module dependency resolution.

local root = os.getenv("SHADY_ROOT") or "."

shady.set("spatial_mode", true)
shady.set("window_gravity", false)
shady.set("window_wobble", true)
shady.set("shadows", true)
shady.set("floor", true)
shady.set("window_opacity", 1.0)
shady.set("window_border_width", 3.0)
shady.set("window_border_color", "#0E3344")
shady.set("window_border_focus_color", "#1FAFE0")
shady.set("window_titlebar", true)
shady.set("window_titlebar_height", 28.0)
shady.set("window_titlebar_color", "#0B1F29")
shady.set("window_titlebar_focus_color", "#123B4D")
shady.set("window_titlebar_text_color", "#EAF9FF")

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

-- FPS window shape is an external representation plugin rather than a core
-- renderer policy. Swap this plugin to change folded-window geometry.
shady.plugin(root .. "/build/libshady-plugin-fps-cube.so")

-- External native plugins are loaded before dependency resolution:
-- shady.plugin("/absolute/path/to/my-plugin.so")
