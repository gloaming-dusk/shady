-- Afterglow's shell: the default UI in the light of the sky.
--
-- The afterglow plugin publishes its palette (afterglow.accent,
-- afterglow.surface, ...) and re-publishes it through every hour change
-- (Super+T). shell.theme is replaced by a table that reads those values each
-- time a view asks for a colour, so the bar, Quick Settings, the task menu
-- and the launcher cross-fade with the sky. Until the first values arrive,
-- the colours from theme.sh (SHADY_SHELL_*) are used.
--
-- The bar and panels are translucent so the compositor's frosted glass
-- (shady.layer_effect in init.lua) shows the sunset through them.

local base = shell.theme
local palette = { "accent", "accent_2", "accent_deep", "surface", "text", "text_dim" }
local live = {}
for _, name in ipairs(palette) do live[name] = true end

shell.theme = setmetatable({}, {
    __index = function(_, name)
        if live[name] then
            return shell.compositor_value("afterglow." .. name, base[name])
        end
        return base[name]
    end,
})

shell.options = {
    bar_opacity = 0.55,
    panel_opacity = 0.7,
}

dofile(shell.data_dir .. "/default.lua")
