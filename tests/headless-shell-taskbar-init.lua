local shell = assert(os.getenv("SHADY_SHELL_BIN"))
local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("SHADY_SHELL_TASKBAR_STATUS"))

local mapped = {}
local focused = ""
local destroyed_a = false
local unmapped_a = false
local started = false

local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end

shady.on("window.focused", function(window)
    focused = window.app_id or ""
end)

shady.on("window.unmapped", function(window)
    if window.app_id == "shady-shell-probe-a" then
        unmapped_a = true
    end
end)

shady.on("window.destroyed", function(window)
    if window.app_id == "shady-shell-probe-a" then
        destroyed_a = true
    end
end)

shady.on("window.mapped", function(window)
    local id = window.app_id
    if id ~= "shady-shell-probe-a" and id ~= "shady-shell-probe-b" then return end
    mapped[id] = true

    if id == "shady-shell-probe-a" and not mapped["shady-shell-probe-b"] then
        shady.automation.after(80, function()
            shady.spawn(string.format(
                "SHADY_AUTOMATION_PROBE_APP_ID=shady-shell-probe-b SHADY_AUTOMATION_PROBE_COLOR=0xff304f80 %q",
                probe))
        end)
        return
    end

    if started or not mapped["shady-shell-probe-a"] or not mapped["shady-shell-probe-b"] then
        return
    end
    started = true

    shady.automation.after(500, function()
        assert(focused == "shady-shell-probe-b",
            "expected probe-b to be focused initially, got " .. focused)

        -- A maps first, so it is the first task chip.
        shady.automation.move_pointer(230, 19)
        shady.automation.click("left")

        shady.automation.after(120, function()
            assert(focused == "shady-shell-probe-a",
                "taskbar left click did not focus probe-a; got " .. focused)
            mark("ACTIVATE=PASS")

            shady.automation.move_pointer(230, 19)
            shady.automation.click("right")

            shady.automation.after(800, function()
                assert(unmapped_a or destroyed_a,
                    "taskbar right click did not unmap probe-a")
                mark("CLOSE=PASS")
                shady.log("headless-shell-taskbar: PASS")
                shady.quit()
            end)
        end)
    end)
end)

shady.spawn(string.format("%q", shell))
shady.automation.after(200, function()
    shady.spawn(string.format(
        "SHADY_AUTOMATION_PROBE_APP_ID=shady-shell-probe-a SHADY_AUTOMATION_PROBE_COLOR=0xff803030 %q",
        probe))
end)
