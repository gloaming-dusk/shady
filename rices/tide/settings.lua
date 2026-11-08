-- What the Settings application saved, as a table: `return { accent = ... }`
-- in ~/.config/shady/settings.lua. Empty when there is nothing, or nothing
-- readable: a damaged file must not cost the whole desktop.

local home = os.getenv("HOME") or ""
local dir = os.getenv("XDG_CONFIG_HOME")
if not dir or dir == "" then dir = home .. "/.config" end
local ok, settings = pcall(dofile, dir .. "/shady/settings.lua")
if ok and type(settings) == "table" then return settings end
return {}
