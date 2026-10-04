-- Steps through layer effects on the "fx-bar" namespace; the test script
-- samples pixels after each mark.
local status = assert(os.getenv("LAYER_EFFECT_STATUS"))
local dir = assert(os.getenv("LAYER_EFFECT_DIR"))
local after = shady.automation.after
local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end
local function wait_for(token, fn)
    -- The script appends `token` to the status file when it is done.
    local function poll()
        local f = io.open(status, "r")
        local text = f and f:read("a") or ""
        if f then f:close() end
        if text:find(token, 1, true) then fn() else after(50, poll) end
    end
    poll()
end

shady.spawn(string.format("%q", assert(os.getenv("SHADY_SHELL_BIN"))))
after(2000, function()
    assert(shady.layer_effect("fx-bar", { shader = dir .. "/identity.frag" }))
    after(300, function() mark("IDENTITY") end)
    wait_for("NEXT1", function()
        assert(shady.layer_effect("fx-bar", { shader = dir .. "/halves.frag" }))
        after(300, function() mark("HALVES") end)
        wait_for("NEXT2", function()
            assert(shady.layer_effect("fx-bar", { blur = 40 }))
            after(300, function() mark("BLUR") end)
            wait_for("NEXT3", function()
                assert(shady.layer_effect("fx-bar", { shader = dir .. "/time.frag" }))
                after(300, function() mark("TIME") end)
                wait_for("NEXT4", function()
                    assert(shady.layer_effect("fx-bar", { shader = os.getenv("SHADY_ROOT") ..
                        "/examples/layer-effects/refract.frag", uniforms = { strength = 0.6 } }))
                    mark("REFRACT")
                    local ok = shady.layer_effect("fx-bar", { shader = dir .. "/broken.frag" })
                    mark(ok and "BROKEN_ACCEPTED" or "BROKEN_REJECTED")
                    assert(shady.layer_effect("fx-bar", nil))
                    after(300, function() mark("REMOVED") end)
                    wait_for("DONE", function() shady.quit() end)
                end)
            end)
        end)
    end)
end)
after(30000, function()
    mark("TIMEOUT")
    shady.quit()
end)
