-- Publishes from Lua on request of tests/headless-published-values.sh.
local status = assert(os.getenv("VALUES_STATUS"))
local after = shady.automation.after
local function wait_for(token, fn)
    local function poll()
        local f = io.open(status, "r")
        local text = f and f:read("a") or ""
        if f then f:close() end
        if text:find(token, 1, true) then fn() else after(50, poll) end
    end
    poll()
end
assert(shady.publish("test.hour", "golden"))
assert(not shady.publish("no spaces allowed", "x"))
assert(shady.value("test.hour") == "golden")
shady.spawn(string.format("%q", assert(os.getenv("SHADY_SHELL_BIN"))))
wait_for("CHANGE", function() assert(shady.publish("test.hour", "night")) end)
wait_for("REMOVE", function() assert(shady.publish("test.hour", nil)) end)
after(25000, function()
    shady.log("published-values: timed out")
    shady.quit()
end)
