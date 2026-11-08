-- Tide's look: the palette, the type and the glass that the shell (shell.lua)
-- and the login screen (greeter/shell.lua) are drawn with.
--
-- Colours come from, in order: the overrides passed in (the `palette` option
-- of shell.base("tide", ...)), the SHADY_SHELL_* environment, Tide's own.

local DEFAULTS = {
    text = "#E4E6F5", dim = "#8E92AE", accent = "#B4A0FF", accent_2 = "#7CB7FF",
    deep = "#2E2850", surface = "#16161F", danger = "#FF7A93",
    base = "#11111B", good = "#8BD5A8", warn = "#F2C27B",
}
local ENVIRONMENT = {
    text = "SHADY_SHELL_TEXT", dim = "SHADY_SHELL_TEXT_DIM", accent = "SHADY_SHELL_ACCENT",
    accent_2 = "SHADY_SHELL_ACCENT_2", deep = "SHADY_SHELL_ACCENT_DEEP",
    surface = "SHADY_SHELL_SURFACE", danger = "SHADY_SHELL_DANGER",
}

return function(overrides)
    overrides = overrides or {}
    local a, mix = shell.alpha, shell.mix
    local P = {}
    for name, default in pairs(DEFAULTS) do
        local env = ENVIRONMENT[name] and os.getenv(ENVIRONMENT[name])
        P[name] = overrides[name] or (env ~= "" and env) or default
    end

    local S = { P = P, SANS = "Manrope", ISLAND = 32 }

    function S.font(weight, size) return S.SANS .. " " .. weight .. " " .. size end

    function S.text(s, props)
        props = props or {}
        props.color = props.color or P.text
        props.font = props.font or S.font("Medium", 9.5)
        return shell.text(s, props)
    end

    function S.glass(alpha)
        return shell.gradient("vertical", a(mix(P.surface, "#FFFFFF", 0.035), alpha), a(P.surface, alpha))
    end

    -- A glass pill floating on the top edge; the bar is made of these.
    function S.island(props)
        props.height = S.ISLAND
        props.radius = props.radius or 16
        props.padding = props.padding or { 0, 12 }
        props.gap = props.gap or 10
        props.align = "center"
        props.background = S.glass(0.88)
        props.border = a(P.text, 0.07)
        props.animate = "snappy"
        return shell.row(props)
    end

    -- The uniforms of shaders/tide.frag, the night sea.
    S.SEA_UNIFORMS = { accent = P.accent, accent_2 = P.accent_2, base = "#0F0F1A" }

    return S
end
