# Neon Transit

A high-contrast Shady rice built around a near-black world, violet horizon, electric-cyan floor grid and a closer workstation-style camera.

Compared with **Night Observatory**, Neon Transit deliberately keeps physics and wobble off by default. It is meant to feel like a sharp futuristic desktop first and a 3D playground second.

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

## Palette

- sky: `#05040A → #160B27 → #020308`
- floor: `#03040A`
- grid: `#00D9FF`
- window tint: `#F8F5FF`
- shell accents remain cyan, which intentionally ties the standalone shell into the world palette.

The floor has a stronger minor/major grid than Night Observatory and fades farther into depth. Application textures stay bright while Shady's spatial effect is present but restrained.
