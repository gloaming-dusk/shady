local probe = assert(os.getenv("SHADY_SESSION_LOCK_PROBE"))
local status_path = assert(os.getenv("SHADY_SESSION_LOCK_STATUS"))
local shortcut_hits = 0

shady.bind("Ctrl+F12", function()
    shortcut_hits = shortcut_hits + 1
end)

local function contains(marker)
    local file = io.open(status_path, "r")
    if not file then return false end
    local contents = file:read("*a")
    file:close()
    return contents:find(marker, 1, true) ~= nil
end

local function wait_for(marker, attempts, callback)
    if contains(marker) then
        callback()
        return
    end
    assert(attempts > 0, "timed out waiting for " .. marker)
    shady.automation.after(50, function()
        wait_for(marker, attempts - 1, callback)
    end)
end

shady.automation.after(50, function()
    assert(shady.automation.key("Ctrl+F12"), "baseline shortcut was not handled")
    assert(shortcut_hits == 1, "baseline shortcut did not fire")

    assert(shady.spawn(string.format("%q", probe)), "failed to spawn session lock probe")
    wait_for("LOCKED=PASS", 20, function()
        wait_for("SURFACE_CONFIGURED=PASS", 20, function()
            assert(not shady.automation.key("Ctrl+F12"),
                "compositor shortcut was handled while locked")

            wait_for("CLIENT_EXIT=PASS", 20, function()
                shady.automation.after(150, function()
                    assert(not shady.automation.key("Ctrl+F12"),
                        "session unlocked after lock client exited")
                    assert(shortcut_hits == 1,
                        "shortcut callback fired after lock client exited")
                    shady.log("headless-session-lock-client-exit: PASS lock persisted after client exit")
                    shady.quit()
                end)
            end)
        end)
    end)
end)
