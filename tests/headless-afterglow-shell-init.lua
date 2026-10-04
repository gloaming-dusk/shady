-- The real Afterglow runtime layer plus shady-shell with the rice's
-- shell.lua; tests/headless-afterglow-shell.sh asks for an hour change.
dofile(assert(os.getenv("SHADY_ROOT")) .. "/examples/rice/afterglow/init.lua")
local status = assert(os.getenv("AFTERGLOW_STATUS"))
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
shady.spawn(string.format("%q", assert(os.getenv("SHADY_SHELL_BIN"))))
wait_for("NEXT_HOUR", function() assert(shady.automation.key("Super+t")) end)
wait_for("DONE", function() shady.quit() end)
after(30000, function()
    shady.log("afterglow-shell: timed out")
    shady.quit()
end)
