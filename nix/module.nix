# NixOS module: `programs.shady.enable = true;` adds a Shady login session.
self:
{ config, lib, pkgs, ... }:

let
  cfg = config.programs.shady;
in
{
  options.programs.shady = {
    enable = lib.mkEnableOption "the Shady 3D Wayland compositor session";

    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
      defaultText = lib.literalExpression "shady.packages.\${system}.default";
      description = "The shady package to use.";
    };

    rice = lib.mkOption {
      type = lib.types.str;
      default = "afterglow";
      example = "neon-transit";
      description = ''
        Rice from `share/shady/examples/rice` used for whatever a user has not
        configured in `~/.config/shady/{config,init,shell}.lua`.
      '';
    };

    extraPackages = lib.mkOption {
      type = lib.types.listOf lib.types.package;
      default = with pkgs; [ foot grim slurp wl-clipboard wlr-randr ];
      defaultText = lib.literalExpression "with pkgs; [ foot grim slurp wl-clipboard wlr-randr ]";
      description = "Extra packages installed alongside the session.";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ] ++ cfg.extraPackages;
    environment.sessionVariables.SHADY_RICE = cfg.rice;

    services.displayManager.sessionPackages = [ cfg.package ];

    hardware.graphics.enable = lib.mkDefault true;
    security.polkit.enable = lib.mkDefault true;
    programs.dconf.enable = lib.mkDefault true;
    fonts.enableDefaultPackages = lib.mkDefault true;

    xdg.portal = {
      enable = lib.mkDefault true;
      wlr.enable = lib.mkDefault true;
      extraPortals = [ pkgs.xdg-desktop-portal-gtk ];
      config.shady.default = lib.mkDefault [ "wlr" "gtk" ];
    };
  };
}
