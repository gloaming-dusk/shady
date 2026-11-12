-- The computer-use test (headless-computer-use.py) drives this over IPC.
shady.bind("Ctrl+F12", function()
    shady.log("computer-use: shortcut")
end)

local started = false
shady.on("window.mapped", function(window)
    if started or window.app_id ~= "computer-use-probe" then return end
    started = true
    shady.log("computer-use: ready")
end)

shady.spawn(string.format("%q", assert(os.getenv("SHADY_AUTOMATION_PROBE"))))
