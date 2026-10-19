-- Objects
-- Chunky, tactile cards on a flat paper sunset: thick ink outlines, hard
-- offset shadows and a few loud colours. The wallpaper, the desk clock, the
-- dock, the control center and the card-deck launcher all come from
-- shady-shell (shell.lua); the compositor only frames the windows to match.

local root = os.getenv("SHADY_ROOT") or "."

shady.log("loading rice: Objects")

shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("window_gravity", false)
shady.set("window_wobble", true)
shady.set("window_sides", false)
-- The wallpaper is a background layer surface drawn by the shell; no sky,
-- floor or projected shadows in front of it.
shady.set("shadows", false)
shady.set("floor", false)
shady.set("sky", false)
shady.set("environment", false)
shady.set("close_animation", true)
shady.set("fps_mode", false)

-- Shown only until the shell's wallpaper arrives.
shady.set("background_top", "#FFD9BF")
shady.set("background_horizon", "#FFB99B")
shady.set("background_bottom", "#F59A8C")

-- Windows look like the shell's cards: cream paper, ink outline, a lilac
-- title strip that turns tangerine when focused.
shady.set("window_tint", "#FFFFFF")
shady.set("window_effect_strength", 0.0)
shady.set("window_brightness", 1.0)
shady.set("window_opacity", 1.0)
shady.set("window_corner_radius", 18.0)
shady.set("window_border_width", 3.0)
shady.set("window_border_color", "#241530")
shady.set("window_border_focus_color", "#241530")
shady.set("window_titlebar", true)
shady.set("window_titlebar_height", 32.0)
shady.set("window_titlebar_color", "#B9A3FF")
shady.set("window_titlebar_focus_color", "#FF7A3D")
shady.set("window_titlebar_text_color", "#241530")

shady.modules({
    ["window-motion"] = true,
    ["physics"] = false,
    ["fps"] = false,
    ["close-animation"] = true,
    ["scene-effects"] = true,
    ["lua"] = true,
})

-- Super+O: 3D overview of every window.
shady.plugin(os.getenv("SHADY_OVERVIEW_PLUGIN") or (root .. "/build/libshady-plugin-spatial-overview.so"))

shady.bind("quit", "Super+Shift+Escape")
shady.bind("cycle_windows", "Super+Tab")
shady.bind("close_window", "Super+q")
