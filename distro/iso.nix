# Gloam live/installer ISO: NixOS that boots straight into the Shady session.
#   nix build .#nixosConfigurations.gloam-live.config.system.build.isoImage
self:
{ config, lib, pkgs, modulesPath, ... }:

let
  # The Gloam release, tagged gloam-v<version> (see .github/workflows/gloam-iso.yml).
  version = lib.fileContents ./VERSION;
  arch = pkgs.stdenv.hostPlatform.uname.processor;
in
{
  imports = [
    "${modulesPath}/installer/cd-dvd/installation-cd-minimal.nix"
    self.nixosModules.default
  ];

  # Gloam is a NixOS derivative: its name goes into os-release and the boot menu.
  system.nixos.distroName = "Gloam";
  system.nixos.distroId = "gloam";
  # The boot menu reads "Gloam <version> Live".
  system.nixos.label = version;
  image.baseName = lib.mkForce "gloam-live-${version}-${pkgs.stdenv.hostPlatform.system}";
  isoImage.volumeID = lib.mkForce "gloam-live-${version}-${arch}";
  isoImage.appendToMenuLabel = " Live";
  isoImage.squashfsCompression = "zstd -Xcompression-level 6";

  programs.shady.enable = true;
  programs.shady.greeter.enable = true;

  # The install guide uses nixos-install --flake.
  nix.settings.experimental-features = [ "nix-command" "flakes" ];

  # Boot to the Shady greeter. The live nixos user has no password, so greetd
  # must accept an empty one, and the card says so.
  security.pam.services.greetd.allowNullPassword = true;
  programs.shady.greeter.hint = "Live session: the nixos user has no password, press Enter.";

  # Let wlroots fall back to llvmpipe so the ISO also runs in VMs.
  environment.sessionVariables.WLR_RENDERER_ALLOW_SOFTWARE = "1";

  networking.networkmanager.enable = true;
  networking.wireless.enable = lib.mkForce false;
  users.users.nixos.extraGroups = [ "networkmanager" "video" ];

  services.pipewire = {
    enable = true;
    pulse.enable = true;
  };

  fonts.packages = with pkgs; [ noto-fonts noto-fonts-cjk-sans noto-fonts-color-emoji ];

  environment.systemPackages = with pkgs; [ firefox networkmanagerapplet pavucontrol git ];
}
