# Gloam live/installer ISO: NixOS that boots straight into the Shady session.
#   nix build .#nixosConfigurations.gloam-live.config.system.build.isoImage
self:
{ config, lib, pkgs, modulesPath, ... }:

{
  imports = [
    "${modulesPath}/installer/cd-dvd/installation-cd-minimal.nix"
    self.nixosModules.default
  ];

  # Gloam is a NixOS derivative: its name goes into os-release and the boot menu.
  system.nixos.distroName = "Gloam";
  system.nixos.distroId = "gloam";
  image.baseName = lib.mkForce "gloam-live-${config.system.nixos.label}-${pkgs.stdenv.hostPlatform.system}";
  isoImage.volumeID = lib.mkForce "gloam-live-${config.system.nixos.release}-${pkgs.stdenv.hostPlatform.uname.processor}";
  isoImage.appendToMenuLabel = " Live";
  isoImage.squashfsCompression = "zstd -Xcompression-level 6";

  programs.shady.enable = true;
  programs.shady.greeter.enable = true;

  # Log the live user straight in; after logging out, the Shady greeter
  # offers Shady and Shady (Safe Mode). The nixos user has no password:
  # press Enter.
  services.greetd.settings.initial_session = {
    user = "nixos";
    command = "shady-session";
  };
  security.pam.services.greetd.allowNullPassword = true;

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
