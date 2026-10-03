local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("SHADY_FPS_TOGGLE_STATUS"))
local done = false

local function mark(line)
  local f = assert(io.open(status, "a"))
  f:write(line, "\n")
  f:close()
end

shady.on("window.mapped", function(window)
  if done or window.app_id ~= "shady-fps-probe" then return end
  done = true
  shady.automation.after(200, function()
    assert(shady.automation.key("Super+f"))
    mark("ENTER=PASS")
    shady.automation.after(250, function()
      assert(shady.automation.key("Super+f"))
      mark("EXIT=PASS")
      shady.automation.after(800, function()
        local found = false
        for _, w in ipairs(shady.windows()) do
          if w.app_id == "shady-fps-probe" then found = true end
        end
        if found then mark("WINDOW_ALIVE=PASS") end
        shady.quit()
      end)
    end)
  end)
end)

shady.spawn(string.format(
  "SHADY_AUTOMATION_PROBE_APP_ID=shady-fps-probe SHADY_AUTOMATION_PROBE_COLOR=0xff305080 %q",
  probe))
