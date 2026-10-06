# Gloam

Gloam is a NixOS-based distribution built around the Shady compositor: it
boots into Shady with the Afterglow rice, logs in through the Shady greeter
and carries NetworkManager, PipeWire, Noto fonts (CJK included) and Firefox.

This directory holds what is specific to Gloam. Shady itself is packaged in
[`../nix`](../nix): `package.nix`, the NixOS module (`programs.shady`,
`programs.shady.greeter`) and its VM tests, which any NixOS system can use
without Gloam.

| | |
|---|---|
| `iso.nix` | the live/installer ISO: the minimal NixOS installer plus Shady, the greeter, flakes, and Gloam's name in os-release and the boot menu |
| `VERSION` | the Gloam version |

## Building the ISO

```sh
nix build .#nixosConfigurations.gloam-live.config.system.build.isoImage
ls result/iso/   # gloam-live-0.2.1-x86_64-linux.iso
```

The ISO boots to the Shady greeter. The live user `nixos` has no password, so
press Enter (the card says so through `programs.shady.greeter.hint`); Tab picks
Shady or Shady (Safe Mode). In a VM without a GPU, wlroots falls back to
llvmpipe.

## Testing in QEMU

Run from the repository root after building the ISO. On a Linux host with
KVM access and a QEMU build with GTK/OpenGL support:

```sh
qemu-system-x86_64 \
  -enable-kvm -cpu host -smp 4 -m 4096 \
  -device virtio-vga-gl -display gtk,gl=on \
  -device qemu-xhci -device usb-tablet \
  -boot d -cdrom result/iso/gloam-live-0.2.1-x86_64-linux.iso
```

The USB tablet provides absolute pointer coordinates. The GL device enables
accelerated rendering; for hosts without OpenGL support, use
`-device virtio-vga -display gtk` instead (software rendering is slower).
Shady draws software cursors when hardware cursor planes are unavailable,
and includes an Adwaita cursor theme for both the greeter and user session.
The live ISO enables `programs.shady.softwareCursor` so cursor orientation and
hotspots do not depend on virtual GPU hardware cursor planes. Other NixOS
systems can enable the same option; standalone sessions can set
`SHADY_SOFTWARE_CURSORS=1` before starting Shady.
Right clicks reach applications and shell context menus; use Alt+right-drag
to orbit the camera.

Firefox runs through native Wayland and opens on the current workspace.
If an application fails to launch, open
a terminal with Super+Enter and run it there to see its error. Session and
launcher output is also available with `journalctl -b -t shady`.
To diagnose boot delays, run `systemd-analyze` and
`systemd-analyze critical-chain greetd.service` inside the guest.

After changing Shady or the NixOS configuration, rebuild the ISO: an existing
ISO will continue to contain the old binaries. The session VM regression test
includes opening a Firefox window:

```sh
nix build .#checks.x86_64-linux.session
```

For the GLES2 pointer regression test, build the automation probe and run the
test in a development shell with a usable DRM render node:

```sh
nix develop
ninja -C build shady shady-shell headless-automation-probe
python3 tests/headless-spatial-pointer.py --build-dir build
```

This checks cursor orientation and hotspot pixels, client click coordinates,
right-click delivery, invisible spatial misses, and overlay click priority.

Gloam follows the latest NixOS stable release (the flake's `nixpkgs` input,
now `nixos-26.05`). `nix/wlroots.nix` builds wlroots 0.20.2 for Shady while
that release still ships an older 0.20.x.

## Releasing

`VERSION` holds the Gloam version; it names the ISO
(`gloam-live-<version>-x86_64-linux.iso`) and the boot menu entry. To
release, bump it, commit, and push a matching tag:

```sh
echo 0.2.1 > distro/VERSION && git commit -am "Gloam 0.2.1"
git tag gloam-v0.2.1 && git push origin main gloam-v0.2.1
```

[`gloam-iso.yml`](../.github/workflows/gloam-iso.yml) then builds the ISO,
splits it under GitHub's 2 GiB asset limit (`<iso>.part0`, `.part1`, ... plus
`<iso>.sha256` for the joined image) and publishes the release. The
[website](https://gloaming-dusk.github.io/download/) picks it up on its next
scheduled build. Running the workflow by hand builds the same files as an
artifact without releasing.
