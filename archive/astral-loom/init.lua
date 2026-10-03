local terminal = os.getenv("TERMINAL") or "foot"
shady.camera("distance", 1.65)
shady.camera("target_z", -0.35)
shady.camera("yaw", 0.0)
shady.camera("pitch", 0.0)
-- Enter the existing FPS renderer so live indexed surfaces are visible.
-- Super+Shift+F releases mouse capture for ordinary app interaction.
shady.toggle_fps()
shady.bind("Super+Return", function() shady.spawn(terminal) end)
shady.bind("Super+d", function() shady.toggle_launcher() end)
shady.bind("Super+Shift+r", function()
    shady.log("Astral Loom reload: " .. tostring(shady.reload_plugin("astral-loom")))
end)
shady.log("Astral Loom: Super+j restore; Super+Shift+j helix; Super+B pause; Super+F curved panels")
