local started = false
local destroyed = false
local module = assert(os.getenv("SHADY_REPRESENTATION_TEST_MODULE"))
shady.on("window.mapped", function(window)
    if started then return end
    started = true
    shady.automation.after(150, function()
        assert(shady.automation.key("Super+f"))
        assert(shady.automation.key("F5"))
        shady.automation.after(250, function()
            assert(shady.reload_plugin(module))
            shady.automation.after(250, function() assert(window:close()) end)
        end)
    end)
end)
shady.on("window.destroyed", function(window)
    destroyed = true
    shady.log("representation-lifetime: PASS " .. module)
    shady.automation.after(100, function() shady.quit() end)
end)
shady.automation.after(4000, function() assert(destroyed) end)
shady.spawn(string.format("%q", assert(os.getenv("SHADY_AUTOMATION_PROBE"))))
