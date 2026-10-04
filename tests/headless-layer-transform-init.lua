-- Steps tests/headless-layer-transform.sh through tilt, depth and removal
-- of a 3D layer transform, clicking where the bar is drawn after each.
local status = assert(os.getenv("LAYER_TRANSFORM_STATUS"))
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

shady.spawn(string.format("%q", assert(os.getenv("SHADY_SHELL_BIN"))))
after(2000, function()
    -- Hinged on its top edge, tilted 70 degrees back: about 28 px tall.
    assert(shady.layer_transform("tf-bar", { tilt = 70 }))
    after(300, function() mark("TILT") end)
    wait_for("NEXT1", function()
        click(100, 8, "CLICK_TILT")
        wait_for("NEXT2", function()
            click(100, 36, "MISS_TILT")
            wait_for("NEXT3", function()
                -- 200 px back: the bar shrinks toward the output centre.
                assert(shady.layer_transform("tf-bar", { depth = 200 }))
                after(300, function() mark("DEPTH") end)
                wait_for("NEXT4", function()
                    click(200, 80, "CLICK_DEPTH")
                    wait_for("NEXT5", function()
                        assert(shady.layer_transform("tf-bar", nil))
                        after(300, function() mark("REMOVED") end)
                        wait_for("NEXT6", function()
                            click(100, 36, "CLICK_PLAIN")
                            wait_for("DONE", function() shady.quit() end)
                        end)
                    end)
                end)
            end)
        end)
    end)
end)
after(30000, function()
    mark("TIMEOUT")
    shady.quit()
end)
