-- The default UI with a CPU pill next to the clock, fed by the native
-- sysinfo plugin (shell/plugins/sysinfo.c): a live graph of the
-- last minute and the current load in percent.
--
--   SHADY_SHELL_CONFIG=shell/examples/sysinfo.lua ./build/shady-shell

local t = shell.theme
local ok, err = shell.plugin("sysinfo", { interval = 1000 })

shell.options = {
    bar_extra = function()
        if not ok then return false end
        return shell.row {
            height = 26, radius = 9, padding = { 0, 10 }, gap = 8,
            background = shell.alpha("#ffffff", 0.03), border = shell.alpha("#ffffff", 0.07),
            shell.widget("sysinfo.graph", { width = 56, height = 16, color = t.accent }),
            shell.text(shell.value("sysinfo.cpu", "–") .. "%", {
                font = "Sans SemiBold 9.5", color = shell.alpha(t.text, 0.92),
                width = 30, text_align = 1,
            }),
        }
    end,
}

if not ok then shell.log("sysinfo unavailable: " .. tostring(err)) end
dofile(shell.data_dir .. "/default.lua")
