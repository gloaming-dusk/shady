local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
shady.on("window.mapped", function(window)
  if window.app_id ~= "quit-probe" then return end
  shady.automation.after(150, function()
    assert(shady.automation.key("Super+Shift+Escape"))
  end)
end)
shady.spawn(string.format("SHADY_AUTOMATION_PROBE_APP_ID=quit-probe %q", probe))
