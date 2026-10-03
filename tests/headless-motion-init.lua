local started = false
shady.on("window.mapped", function(window)
    if started then return end
    started = true
    shady.automation.after(300, function()
        assert(shady.reload_plugin("window-motion"))
        shady.automation.after(100, function() shady.quit() end)
    end)
end)
shady.spawn(string.format("%q", assert(os.getenv("SHADY_AUTOMATION_PROBE"))))
