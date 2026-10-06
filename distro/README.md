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
| `iso.nix` | the live/installer ISO: the minimal NixOS installer plus Shady, the greeter, and Gloam's name in os-release and the boot menu |

## Building the ISO

```sh
nix build .#nixosConfigurations.gloam-live.config.system.build.isoImage
ls result/iso/   # gloam-live-<version>-x86_64-linux.iso
```

The live user `nixos` is logged straight into Shady. After logging out, the
greeter offers Shady and Shady (Safe Mode); the password is empty, so press
Enter. In a VM without a GPU, wlroots falls back to llvmpipe.
