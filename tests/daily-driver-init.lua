-- Make lifecycle progress visible to the smoke-test harness.
local function window_label(window)
    if window == nil then return "<nil>" end
    return (window.app_id or "") .. " / " .. (window.title or "")
end

shady.on("window.mapped", function(window)
    shady.log("daily-driver-test: mapped " .. window_label(window))
end)

shady.on("window.unmapped", function(window)
    shady.log("daily-driver-test: unmapped " .. window_label(window))
end)

shady.on("window.destroyed", function(window)
    shady.log("daily-driver-test: destroyed " .. window_label(window))
end)

shady.on("output.added", function(output)
    shady.log("daily-driver-test: output-added " .. (output.name or ""))
end)

shady.on("output.removed", function(output)
    shady.log("daily-driver-test: output-removed " .. (output.name or ""))
end)
