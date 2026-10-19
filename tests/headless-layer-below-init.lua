-- Steps tests/headless-layer-below.sh: a window over the wallpaper, then
-- clicks on the bottom-layer widget, plain and turned in 3D.
local status = assert(os.getenv("LAYER_BELOW_STATUS"))
local after = shady.automation.after
local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end
local function wait_for(token, fn)
    local function poll()
        local f = io.open(status, "r")
        local text = f and f:read("a") or ""
        if f then f:close() end
        if text:find(token, 1, true) then fn() else after(50, poll) end
    end
    poll()
end
local function click(x, y, token)
    shady.automation.move_pointer(x, y)
    shady.automation.click("left")
    after(300, function() mark(token) end)
end

shady.on("window.mapped", function() after(600, function() mark("MAPPED") end) end)
shady.spawn(string.format("%q", assert(os.getenv("SHADY_SHELL_BIN"))))
after(1500, function()
    shady.spawn(string.format("SHADY_AUTOMATION_PROBE_COLOR=0xff3050c0 %q",
        assert(os.getenv("SHADY_AUTOMATION_PROBE"))))
end)
wait_for("NEXT1", function()
    click(120, 560, "CLICK_PLAIN")
    wait_for("NEXT2", function()
        -- Turned 60 degrees about its centre: half as wide where drawn.
        assert(shady.layer_transform("below-widget", { yaw = 60, pivot = "center" }))
        after(300, function()
            click(120, 560, "CLICK_TURNED")
            wait_for("NEXT3", function()
                click(46, 560, "MISS_TURNED")
                wait_for("DONE", function() shady.quit() end)
            end)
        end)
    end)
end)
after(30000, function()
    mark("TIMEOUT")
    shady.quit()
end)
