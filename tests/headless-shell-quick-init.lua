local shell = assert(os.getenv("SHADY_SHELL_BIN"))
local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("SHADY_SHELL_QUICK_STATUS"))

local mapped = {}
local focused = ""
local started = false

local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end

assert(shady.workspace("x"))
assert(shady.workspace("main"))

shady.on("window.focused", function(window)
    focused = window.app_id or ""
end)

shady.on("window.mapped", function(window)
    local id = window.app_id
    if id ~= "shady-quick-a" and id ~= "shady-quick-b" then return end
    mapped[id] = true

    if id == "shady-quick-a" and not mapped["shady-quick-b"] then
        shady.automation.after(80, function()
            shady.spawn(string.format(
                "SHADY_AUTOMATION_PROBE_APP_ID=shady-quick-b SHADY_AUTOMATION_PROBE_COLOR=0xff304f80 %q",
                probe))
        end)
        return
    end

    if started or not mapped["shady-quick-a"] or not mapped["shady-quick-b"] then
        return
    end
    started = true

    shady.automation.after(450, function()
        assert(focused == "shady-quick-b",
            "expected probe-b focused initially, got " .. focused)

        -- Open clock/quick-settings pill and click Next window (row 1).
        shady.automation.move_pointer(1238, 19)
        shady.automation.click("left")
        shady.automation.after(120, function()
            shady.automation.move_pointer(1120, 133)
            shady.automation.click("left")

            shady.automation.after(160, function()
                assert(focused == "shady-quick-a",
                    "quick settings Next window did not cycle focus; got " .. focused)
                mark("NEXT=PASS")

                -- Open again and choose workspace x (row 3: Launcher, Next, main, x).
                shady.automation.move_pointer(1238, 19)
                shady.automation.click("left")
                shady.automation.after(120, function()
                    shady.automation.move_pointer(1120, 209)
                    shady.automation.click("left")

                    shady.automation.after(160, function()
                        assert(shady.current_workspace() == "x",
                            "quick settings workspace switch failed")
                        mark("WORKSPACE=PASS")

                        -- The bar remains on-screen on workspace x. Re-open and Quit Shady.
                        shady.automation.move_pointer(1238, 19)
                        shady.automation.click("left")
                        shady.automation.after(120, function()
                            mark("QUIT_REQUESTED=PASS")
                            -- Row 4 is Quit with two workspaces.
                            shady.automation.move_pointer(1120, 247)
                            shady.automation.click("left")
                        end)
                    end)
                end)
            end)
        end)
    end)
end)

shady.spawn(string.format("%q", shell))
shady.automation.after(200, function()
    shady.spawn(string.format(
        "SHADY_AUTOMATION_PROBE_APP_ID=shady-quick-a SHADY_AUTOMATION_PROBE_COLOR=0xff803030 %q",
        probe))
end)
