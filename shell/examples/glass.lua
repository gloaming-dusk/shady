-- The default UI made translucent, for the compositor's frosted-glass
-- layer effect. Turn the glass on in the compositor's init.lua:
--
--   shady.layer_effect("shady-shell", { blur = 18, saturation = 1.35, tint = "#08101a40" })
--   shady.layer_effect("quick", { blur = 24, saturation = 1.2 })
--   shady.layer_effect("menu", { blur = 24, saturation = 1.2 })
--   shady.layer_effect("launcher", { blur = 32, saturation = 1.2 })
--
-- Layer namespaces are the bar and popup names from the shell config. The
-- effect needs spatial mode; elsewhere the UI is just more transparent.
--
--   SHADY_SHELL_CONFIG=shell/examples/glass.lua ./build/shady-shell

shell.options = {
    bar_opacity = 0.45,
    panel_opacity = 0.6,
}

dofile(shell.data_dir .. "/default.lua")
