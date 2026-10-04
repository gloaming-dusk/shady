-- Map one probe window and a second workspace, then let tests/headless-ipc.sh
-- drive the compositor through shadyctl. The script ends the session with
-- `shadyctl quit`; the timer is only a safety net.
local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local announced = false
shady.on("window.mapped", function(window)
    if announced or window.app_id ~= "ipc-probe" then return end
    announced = true
    assert(window:focus())
    shady.log("ipc-test: ready")
end)
shady.automation.after(100, function()
    shady.spawn(string.format(
        "SHADY_AUTOMATION_PROBE_APP_ID=ipc-probe SHADY_AUTOMATION_PROBE_TITLE='IPC \"probe\"' %q",
        probe))
end)
shady.automation.after(20000, function()
    shady.log("ipc-test: timed out")
    shady.quit()
end)
