{
  description = "Shady, an experimental 3D Wayland compositor";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      packages = forAllSystems (pkgs: rec {
        shady = pkgs.callPackage ./nix/package.nix { };
        default = shady;
      });

      overlays.default = final: prev: {
        shady = final.callPackage ./nix/package.nix { };
      };

      nixosModules = rec {
        shady = import ./nix/module.nix self;
        default = shady;
      };

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          strictDeps = true;
          # Follow the selected wlroots package’s dependency set.
          inputsFrom = [ pkgs.wlroots ];
          nativeBuildInputs = with pkgs; [ pkg-config meson ninja wayland-scanner wlr-protocols ];
          buildInputs = with pkgs; [
            wlroots wayland wayland-protocols libxkbcommon pixman libdrm
            mesa libglvnd libffi libxau libxdmcp lua5_4
            cairo cairo.dev pango pango.dev
          ];
          packages = with pkgs; [ stdenv.cc gdb foot seatd wlr-randr grim wtype nodejs ];
          shellHook = ''
            export PKG_CONFIG_PATH=${pkgs.cairo.dev}/lib/pkgconfig:${pkgs.pango.dev}/lib/pkgconfig:${pkgs.wlr-protocols}/share/pkgconfig:$PKG_CONFIG_PATH
          '';
        };
      });
    };
}
