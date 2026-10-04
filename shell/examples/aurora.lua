-- The default UI with two shader effects: aurora light drifting through
-- the bar and a glow running around the focused task.
--
--   SHADY_SHELL_CONFIG=shell/examples/aurora.lua ./build/shady-shell
--
-- Or copy this file to ~/.config/shady/shell.lua; the shaders are found in
-- the shell's data directory either way. Edit a shader and save it to see
-- the change live.

local t = shell.theme

shell.options = {
    bar_shader = shell.shader("shaders/aurora.frag"),
    bar_uniforms = { strength = 0.55, accent = t.accent, accent_2 = t.accent_2 },
    focus_shader = shell.shader("shaders/glow.frag"),
    focus_uniforms = { color = t.accent, strength = 0.9 },
}

dofile(shell.data_dir .. "/default.lua")
