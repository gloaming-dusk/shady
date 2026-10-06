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
ls result/iso/   # gloam-live-0.1.0-x86_64-linux.iso
```

The live user `nixos` is logged straight into Shady. After logging out, the
greeter offers Shady and Shady (Safe Mode); the password is empty, so press
Enter. In a VM without a GPU, wlroots falls back to llvmpipe.

## Releasing

`VERSION` holds the Gloam version; it names the ISO
(`gloam-live-<version>-x86_64-linux.iso`) and the boot menu entry. To
release, bump it, commit, and push a matching tag:

```sh
echo 0.2.0 > distro/VERSION && git commit -am "Gloam 0.2.0"
git tag gloam-v0.2.0 && git push origin main gloam-v0.2.0
```

[`gloam-iso.yml`](../.github/workflows/gloam-iso.yml) then builds the ISO,
splits it under GitHub's 2 GiB asset limit (`<iso>.part0`, `.part1`, ... plus
`<iso>.sha256` for the joined image) and publishes the release. The
[website](https://gloaming-dusk.github.io/download/) picks it up on its next
scheduled build. Running the workflow by hand builds the same files as an
artifact without releasing.
