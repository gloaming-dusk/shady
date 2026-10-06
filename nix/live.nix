# Live/installer ISO that boots straight into the Shady session.
#   nix build .#nixosConfigurations.live.config.system.build.isoImage
self:
{ config, lib, pkgs, modulesPath, ... }:

let
  sessions = "${config.services.displayManager.sessionData.desktops}/share/wayland-sessions";
in
{
  imports = [
    "${modulesPath}/installer/cd-dvd/installation-cd-minimal.nix"
    self.nixosModules.default
  ];

  image.baseName = lib.mkForce "shady-live-${config.system.nixos.label}-${pkgs.stdenv.hostPlatform.system}";
  isoImage.appendToMenuLabel = " Shady Live";
  isoImage.squashfsCompression = "zstd -Xcompression-level 6";

  programs.shady.enable = true;

  # Log the live user straight in; after logging out, tuigreet offers the
  # Shady and Shady (Safe Mode) sessions.
  services.greetd = {
    enable = true;
    settings = {
      initial_session = {
        user = "nixos";
        command = "shady-session";
      };
      default_session.command =
        "${pkgs.tuigreet}/bin/tuigreet --time --remember-session --sessions ${sessions} --cmd shady-session";
    };
  };

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
