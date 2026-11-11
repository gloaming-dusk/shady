# Tide (compositor side)

What Shady is with nothing configured: niri's scrolling columns and keys,
window rules, screenshots and idle. Tide's shell (bar, launcher, control
center, notifications, lock screen) is not part of Shady: it comes with
[Gloam](https://github.com/gloaming-dusk/gloam), which installs its
`shell.lua` into this same rice, `share/shady/rices/tide`.

| | |
|---|---|
| `config.lua` | the scrolling layout, borders and corners, input |
| `init.lua` | the keys (`Super+Shift+/` lists them), rules, idle, screenshots |
| `settings.lua` | reads what Gloam's Settings saved in `~/.config/shady/settings.lua` |

With a shell connected, the keys that belong to it (launcher, control
center, lock, volume, brightness, media, the screenshot picker) are sent to
it as the published value `tide.do`. Without one they do what can be done
alone: `fuzzel`, `wofi`, `tofi` or `bemenu` for the launcher, `swaylock`
to lock, `wpctl` or `pactl`, `brightnessctl` and `playerctl` for the
hardware keys, and `shady.screenshot()` for `Print`, whichever is
installed.

Start a config of your own from it with `shady.base("tide")` in
`config.lua` and `init.lua`; see [Customizing](../../docs/CUSTOMIZING.md).
