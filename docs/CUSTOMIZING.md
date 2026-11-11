# Customizing

Shady's default desktop is [Tide](../rices/tide/README.md): scrolling columns
and niri's keys in the compositor, and, with
[Gloam](https://github.com/gloaming-dusk/gloam), a glass bar, control center,
launcher, lock screen and login screen in
[shady-shell](https://github.com/gloaming-dusk/shady-shell). Run Shady with
nothing configured and that is what you get. From there you can go as far as
you like, and each step leaves the previous ones working.

| | Start from | You maintain | Tide updates reach you |
|---|---|---|---|
| [Use it](#use-it) | Tide | nothing, or a few settings | yes |
| [Build on it](#build-on-it) | Tide, plus your files | what you changed | yes |
| [Take it apart](#take-it-apart) | your own copy of Tide | all of it | no |
| [Start from nothing](#start-from-nothing) | the bare compositor | all of it | no |

Files live in `~/.config/shady/`. Any of `config.lua`, `init.lua` and
`shell.lua` that is missing comes from the rice, so you only write the ones
you need. Another complete rice is one setting away once it is installed
([the list](../README.md#rices)): `SHADY_RICE=afterglow`, or
`programs.shady.rice` and `programs.shady.rices` on NixOS.

A rice is a directory named after it, in `$SHADY_RICE_PATH`,
`~/.local/share/shady/rices`, Shady's own `share/shady/rices` or any
`$XDG_DATA_DIRS/shady/rices`; each file is looked for on its own, in that
order. So one rice can come from several packages: Shady installs Tide's
`config.lua` and `init.lua`, Gloam its `shell.lua`, and a file of yours in
`~/.local/share/shady/rices/tide/` replaces just that one.

## Use it

Some things need no Lua at all:

- A PNG at `~/.config/shady/wallpaper.png` replaces the night sea.
- `TIDE_LIGHT=1` draws through the plain scene graph, for the least GPU work.
- `TIDE_IDLE_LOCK` and `TIDE_IDLE_SCREEN_OFF` set the idle times in seconds.
- `TIDE_XKB_LAYOUT` and `TIDE_XKB_OPTIONS` set the keyboard.
- `TERMINAL` picks the terminal for `Super+Return`.

The [Gloam](https://github.com/gloaming-dusk/gloam) Settings application changes
the accent colour, wallpaper, corners, gaps, keyboard and touchpad, and saves
them to `~/.config/shady/settings.lua`. Tide reads that file over its own values,
and a config built on Tide can override it.

## Build on it

Start each file from Tide and change what you want afterwards.

`config.lua` runs before the compositor starts. Settings you make after
`shady.base` win:

```lua
-- ~/.config/shady/config.lua
shady.base("tide")
shady.set("layout_gap", 16)
shady.set("window_corner_radius", 6)
shady.set("window_border_focus_color", "#FF9E64")
shady.plugins.load("border-accent")
```

`init.lua` runs once the compositor is up. The last binding of a key wins, so
Tide's keys are yours to take over, and `shady.unbind` removes one:

```lua
-- ~/.config/shady/init.lua
shady.base("tide")
shady.bind("Super+Return", function() shady.spawn("kitty") end, "Open a terminal")
shady.bind("Super+b", function() shady.spawn("firefox") end, "Browser")
shady.unbind("Super+c")
shady.rule({ app_id = "mpv", floating = true })
```

`shell.lua` runs in shady-shell and is hot-reloaded. `shell.base` takes the
options below and returns the pieces Tide is made of (with Gloam's Tide
installed), so what you add looks like it belongs:

```lua
-- ~/.config/shady/shell.lua
local tide
tide = shell.base("tide", {
    palette = { accent = "#FF9E64", accent_2 = "#F7768E" },
    wallpaper = "~/pictures/sea.png",
    bar = {
        right = function(ctx)
            return tide.island { tide.text(shell.date("Week %V")) }
        end,
    },
    tiles = function(ctx, tile)
        return { tile(ctx, "t:vpn", "wifi", "VPN", "Connect", false,
            function() shell.spawn("vpn-toggle") end) }
    end,
    launcher = function(query)
        if query ~= "notes" then return {} end
        return { { title = "Open notes", icon = "grid",
            run = function() shell.spawn("foot nvim ~/notes") end } }
    end,
})
```

| Option | |
|---|---|
| `palette` | colours to change: `text`, `dim`, `accent`, `accent_2`, `deep`, `surface`, `danger`, `base`, `good`, `warn` |
| `wallpaper` | a picture for the desktop and the lock screen |
| `bar` | `left`, `center` and `right`: functions of `ctx` that return an island to add |
| `tiles` | `fn(ctx, tile)` returning a list of tiles for the control center |
| `launcher` | `fn(query)` returning results to list first: `title`, `subtitle`, `icon`, `run` |

`shell.base` returns `P` (the palette), `font`, `text`, `glass`, `card`,
`island`, `tile`, `slider`, `has`, `sys`, `act`, `lock` and `style`. A config
built on Tide runs in the same Lua state, so `shell.popup`, `shell.bar` and
`shell.on` add to it directly.

The same calls work with any rice, though the options are particular to each.

## Take it apart

When options are not enough, make Tide yours:

```sh
shady-rice eject        # or: shady-rice eject afterglow
```

This copies the rice into `~/.config/shady/`, and that copy is what runs from
then on. Everything is open: `shell.lua` is the whole shell, `style.lua` its
look, `init.lua` the keys. It refuses to overwrite files you already have.
Updates to Shady and Gloam no longer change your copy, so compare it with
their `rices/tide/` now and then.

## Start from nothing

`SHADY_RICE=none` is the bare compositor with its C defaults (the 3D floor,
wobble, title bars) and the shell's plain `default.lua`. Every rice written
before Tide became the default keeps working: they say what they want with
`shady -c`, `SHADY_LUA_INIT` and `SHADY_SHELL_CONFIG`, or link their files
into `~/.config/shady/`, and none of that goes through Tide. Native plugins
load with `shady.plugins.load` either way.

The references are the [Lua API](LUA_API.md), the
[Shell Lua API](https://github.com/gloaming-dusk/shady-shell/blob/main/docs/SHELL_LUA_API.md), the [Plugin manager](PLUGIN_MANAGER.md) and
the [C plugin API](C_PLUGIN_API.md).
