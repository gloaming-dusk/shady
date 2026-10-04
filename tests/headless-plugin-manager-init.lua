-- Runtime half of the plugin manager test.
local after = shady.automation.after

local function states()
    local out = {}
    for _, p in ipairs(shady.plugins.list()) do out[p.name] = p.state end
    return out
end

local function check(cond, what)
    if not cond then
        shady.log("plugin-manager-test: FAIL " .. what)
        shady.quit()
        error(what)
    end
end

local s = states()
check(s["window-motion"] == "disabled", "window-motion disabled")
check(s["obj-loader"] == "active", "obj-loader active")
check(s["counter-plugin"] == "active", "counter active")
check(s["does-not-exist"] == "failed", "missing plugin reported as failed")
check(shady.plugins.load == nil, "load is bootstrap-only")

after(200, function()
    -- Reload by the requested name, unload by the module name.
    check(shady.plugins.reload("counter"), "reload by spec")
    check(shady.plugins.unload("obj-loader"), "unload")
    after(100, function()
        check(states()["obj-loader"] == "unloaded", "obj-loader unloaded")
        check(shady.reload_plugin("counter-plugin"), "legacy reload alias")
        shady.log("plugin-manager-test: PASS")
        shady.quit()
    end)
end)
