# Horizon

shady-shell living in [Afterglow](../afterglow/README.md)'s sky. There is no
bar:

- the time is written large across the sky, with a line that follows the
  hour ("the sun is low", "the blue hour", ...),
- the workspaces are lights sitting on the horizon (the current one bright,
  ones with windows lit, empty ones faint) and the open windows are
  reflected in the sea just below it: click to focus, middle-click to close,
- a single line of text in the top-right sky shows Wi-Fi, volume and
  battery; clicking it brings up the control center,
- the control center is a slab of frosted glass standing in the air, turned
  towards the middle of the screen in 3D. It picks the hour of the sky
  (golden, afterglow, blue hour, night), sets volume and brightness, toggles
  Wi-Fi, Bluetooth and sound, takes a screenshot and shows what is playing,
- the launcher (`Super+d`) scatters every application across the sky as a
  star. Type a name and the matching stars light up and join into a
  constellation; `Tab` moves to the next one, `↵` launches it.

Every colour follows the sky's published palette, so the whole shell
cross-fades with `Super+T`.

```bash
nix develop -c ./tools/build.sh
nix develop -c ./rices/horizon/run.sh
```

It uses Afterglow's compositor config, sky plugin and key bindings
(`rices/afterglow/config.lua`, `init.lua`); `init.lua` here adds the glass
and the 3D turn of the control center. The control center's tools are the
same optional ones as in the Objects rice: `wpctl`, `brightnessctl`,
`nmcli`, `bluetoothctl`, `playerctl` and `grim`.

The desk (clock, horizon, reflections) is a background-level layer, so
windows cover it; the status line stays above them.

Typefaces: [Instrument Serif](https://github.com/Instrument/instrument-serif)
and [Manrope](https://github.com/googlefonts/manrope), both under the SIL Open
Font License 1.1 (`fonts/OFL-*.txt`).
