local count, state = 0, nil
for _, p in ipairs(shady.plugins.list()) do
    if p.name == "obj-loader" then count, state = count + 1, p.state end
end
if count == 1 and state == "active" then
    shady.log("plugin-manager-override: PASS")
else
    shady.log("plugin-manager-override: FAIL count=" .. count .. " state=" .. tostring(state))
end
shady.automation.after(100, function() shady.quit() end)
