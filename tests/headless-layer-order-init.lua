-- Steps tests/headless-layer-order.sh: an overlay layer surface, then a top
-- layer surface mapped later over the same corner.
local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("LAYER_ORDER_STATUS"))
local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end
shady.spawn("SHADY_AUTOMATION_PROBE_LAYER=overlay SHADY_AUTOMATION_PROBE_APP_ID=order-overlay "
    .. "SHADY_AUTOMATION_PROBE_COLOR=0xffff0000 " .. probe)
shady.automation.after(800, function()
    shady.spawn("SHADY_AUTOMATION_PROBE_LAYER=top SHADY_AUTOMATION_PROBE_APP_ID=order-top "
        .. "SHADY_AUTOMATION_PROBE_COLOR=0xff0000ff " .. probe)
end)
shady.automation.after(1800, function() mark("MAPPED") end)
shady.automation.after(20000, function() mark("TIMEOUT"); shady.quit() end)
