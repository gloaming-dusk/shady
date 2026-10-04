-- Two probe windows for tests/headless-standard-protocols.sh: std-a renames
-- itself after a while, std-b maps second and so starts focused.
local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("STD_STATUS"))
assert(shady.workspace("x"))
assert(shady.workspace("main"))
local mapped = 0
shady.on("window.mapped", function(window)
    if window.app_id ~= "std-a" and window.app_id ~= "std-b" then return end
    mapped = mapped + 1
    if mapped == 2 then
        local f = assert(io.open(status, "a"))
        f:write("READY\n")
        f:close()
    end
end)
shady.spawn(string.format("SHADY_AUTOMATION_PROBE_APP_ID=std-a SHADY_AUTOMATION_PROBE_TITLE='Std A' " ..
    "SHADY_AUTOMATION_PROBE_RETITLE='Std A renamed' SHADY_AUTOMATION_PROBE_RETITLE_MS=2500 %q", probe))
shady.automation.after(300, function()
    shady.spawn(string.format("SHADY_AUTOMATION_PROBE_APP_ID=std-b SHADY_AUTOMATION_PROBE_TITLE='Std B' %q", probe))
end)
shady.automation.after(25000, function()
    shady.log("standard-protocols: timed out")
    shady.quit()
end)
