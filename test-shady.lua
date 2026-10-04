-- Shady development configuration.
shady.config("physics_enabled", true)
shady.config("window_gravity", true)
shady.config("window_wobble", true)
shady.config("window_sides", true)
shady.config("shadows", true)
shady.config("floor", true)
shady.config("close_animation", true)
shady.config("fps_mode", true)
shady.config("sky", true)
shady.config("sky_path", "/home/jihoo/shady/sky.ppm")
shady.config("environment", true)
shady.config("environment_path", "/home/jihoo/shady/assets/test-room.obj")

shady.config("bind.quit", "Escape")
shady.config("bind.cycle_windows", "F1")
shady.config("bind.close_window", "Alt+F11")
shady.config("bind.fps_toggle", "F2")
shady.config("bind.fps_capture", "F3")
shady.config("bind.gravity_toggle", "F4")
shady.config("bind.debug_ray", "F5")
shady.config("bind.camera_left", "Alt+Left")
shady.config("bind.camera_right", "Alt+Right")
shady.config("bind.camera_up", "Alt+Up")
shady.config("bind.camera_down", "Alt+Down")
shady.config("bind.camera_yaw_left", "Alt+q")
shady.config("bind.camera_yaw_right", "Alt+e")
shady.config("bind.camera_zoom_in", "Alt+equal")
shady.config("bind.camera_zoom_out", "Alt+minus")
shady.config("bind.camera_reset", "Alt+0")

shady.log("development config loaded")

-- Runtime scripting examples.
shady.on("window.mapped", function(window)
  shady.log("window mapped: " .. window.app_id .. " / " .. window.title)
end)

shady.on("window.unmapped", function(window)
  shady.log("window unmapped: " .. window.app_id)
end)

shady.bind("F6", function()
  shady.toggle_gravity()
  shady.log("gravity toggled from Lua")
end)

local all_expanded = false
shady.bind("F7", function()
  all_expanded = not all_expanded
  if all_expanded then shady.expand_all() else shady.fold_all() end
end)

shady.bind("F8", function()
  local windows = shady.windows()
  shady.log("alive windows: " .. #windows)
  for i, window in ipairs(windows) do
    shady.log(string.format("[%d] %s / %s  z=%.2f", i, window.app_id, window.title, window.z))
  end
end)

shady.bind("F9", function()
  shady.respawn_all()
  shady.log("all windows respawned")
end)

shady.bind("Ctrl+Alt+q", function()
  shady.quit()
end)
