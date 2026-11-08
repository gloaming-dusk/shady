local probe = assert(os.getenv("SHADY_SESSION_LOCK_PROBE"))
local status_path = assert(os.getenv("SHADY_SESSION_LOCK_STATUS"))

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
    assert(shady.spawn(string.format("%q", probe)), "failed to spawn session lock probe")
    wait_for("LAYER_MAPPED=PASS", 60, function()
        shady.automation.move_pointer(100, 100)
        wait_for("UNLOCKED=PASS", 60, function()
            shady.log("headless-session-lock-layer: finished")
            shady.quit()
        end)
    end)
end)
