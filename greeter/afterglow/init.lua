-- Shady greeter runtime: frosted glass for the login card and Afterglow's
-- camera, looking out over the sea.

if shady.has_capability("spatial") then
    shady.layer_effect("login", { blur = 22, saturation = 1.2, tint = "#1a0e1a30" })
    shady.camera("yaw", -0.05)
    shady.camera("pitch", -0.07)
    shady.camera("distance", 1.30)
    shady.camera("target_x", 0.0)
    shady.camera("target_y", -0.02)
    shady.camera("target_z", -0.55)
end
