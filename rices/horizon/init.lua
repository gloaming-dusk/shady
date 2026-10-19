-- Horizon runtime layer: Afterglow's sky, sea and bindings, plus glass and
-- a 3D lean for the control center.

local root = assert(os.getenv("SHADY_ROOT"))
dofile(root .. "/rices/afterglow/init.lua")

if shady.has_capability("spatial") then
    -- A slab of frosted glass standing in the sky, turned towards the
    -- middle of the screen.
    shady.layer_effect("horizon-control", { blur = 26, saturation = 1.3, tint = "#2a12301c" })
    shady.layer_transform("horizon-control", { yaw = 14, depth = 30, pivot = "center" })
    shady.layer_effect("horizon-menu", { blur = 22, saturation = 1.25 })
    shady.layer_effect("horizon-toasts", { blur = 20, saturation = 1.2 })
end
