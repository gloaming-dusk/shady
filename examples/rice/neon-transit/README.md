# Neon Transit

A high-contrast Shady rice built around a near-black world, violet horizon, electric-cyan floor grid and a closer workstation-style camera.

Compared with **Night Observatory**, Neon Transit deliberately keeps physics and wobble off by default. It is meant to feel like a sharp futuristic desktop first and a 3D playground second. By default it loads the native C `spatial-overview` plugin and the native `water-windows` effect. `Super+O` spreads visible windows into a 3D grid, arrow keys move the highlighted selection, Enter focuses the selected window, and Escape cancels back to the saved layout. `Super+W` toggles the liquid surface at runtime. Set `SHADY_NEON_DEPTH_MODE=focus` to use the alternate native `focus-depth` style, `SHADY_NEON_WATER=0` to disable the animated water effect, or tune its intensity with `SHADY_WATER_STRENGTH` from `0.25` to `1.5` (default `1.0`).

## Run

From the repository root:

```sh
nix develop -c ninja -C build
./examples/rice/neon-transit/run.sh
```

For DRM/libinput on a VT:

```sh
./examples/rice/neon-transit/run-native.sh
```

Both launchers start `shady-shell` plus one terminal when no extra Shady arguments are supplied.

## Controls

| Binding | Action |
|---|---|
| Super + Return | Open terminal |
| Super + D | Toggle launcher |
| Super + Tab | Cycle windows |
| Super + O | Toggle native 3D overview |
| Arrow keys | Move overview selection |
| Enter | Focus selected overview window |
| Escape | Cancel overview and restore layout |
| Super + 1 / 2 / 3 | Switch main / code / comms |
| Super + Shift + 1 / 2 / 3 | Move focused window and follow |
| Super + M | Toggle maximize |
| Super + Shift + M | Toggle fullscreen |
| Super + Space | Expand all windows |
| Super + Shift + Space | Fold all windows |
| Super + F | Toggle FPS mode |
| Super + Shift + F | Toggle FPS capture |
| Super + R | Respawn windows |
| Super + I | Log session status |
| Super + 0 | Reset camera |
| Super + +/- | Zoom |
| Super + Q | Close focused window |
| Super + W | Toggle water-window effect |
| Super + Shift + Escape | Quit Shady |

## Palette

- sky: `#05040A → #160B27 → #020308`
- floor: `#03040A`
- grid: `#00D9FF`
- window tint: `#F8F5FF`
- window opacity: `0.90`
- shell accents remain cyan, which intentionally ties the standalone shell into the world palette.

The floor has a stronger minor/major grid than Night Observatory and fades farther into depth. Neon Transit also uses `shady.set("window_opacity", 0.90)` so the world subtly shows through application surfaces; any config can set `window_opacity` from `0.0` (fully transparent) to `1.0` (opaque). Application textures stay bright while Shady's spatial effect is present but restrained. The default overview behavior lives in `examples/plugins/spatial_overview.c`; the optional focus style lives in `examples/plugins/focus_depth.c`, and the liquid-surface effect lives in `examples/plugins/water_windows.c`. The water plugin now drives both shape and surface appearance: mesh waves and multi-scale UV refraction are combined with Fresnel cyan edge light, moving broad/specular glints, water tint, caustic bands, crest/trough shading, and thin-water transmission in the fragment shader. Refraction also follows the deformed surface normal so the application texture bends in the same direction as the 3D water sheet. Surface lighting is size-adaptive: small windows keep the liquid deformation/refraction but automatically soften Fresnel/specular intensity, while large windows retain the full wet-surface reflection. All three use the native C plugin API rather than Lua animation code. The water effect intentionally requests continuous frames while active and returns to demand-driven idle immediately when disabled.
