-- tests/headless-layer-effect.sh: a patterned bar on the bottom layer and
-- a nearly transparent "fx-bar" over it that gets the compositor effects.
-- Pattern rows (y 0-19): 20 px stripes, white from x = 0, black from 20;
-- (y 20-39): blue.
local stripes = {}
for i = 0, 63 do
    stripes[#stripes + 1] = shell.box { width = 20, height = 20,
        background = i % 2 == 0 and "#ffffff" or "#000000" }
end
shell.bar {
    name = "pattern", size = 40, exclusive = false, layer = "bottom",
    view = function()
        return shell.column {
            align = "stretch",
            shell.row { height = 20, clip = true, stripes },
            shell.box { height = 20, background = "#0000ff" },
        }
    end,
}
shell.bar {
    name = "fx-bar", size = 40, exclusive = false,
    view = function()
        return shell.row { background = "#00000010", shell.spacer() }
    end,
}
