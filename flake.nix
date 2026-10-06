{
  description = "Shady, an experimental 3D Wayland compositor";

  # Gloam follows the latest NixOS stable release.
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { self, nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
      wlrootsFor = pkgs: pkgs.callPackage ./nix/wlroots.nix { };
      shadyFor = pkgs: pkgs.callPackage ./nix/package.nix { wlroots = wlrootsFor pkgs; };
    in {
      packages = forAllSystems (pkgs: rec {
        shady = shadyFor pkgs;
        default = shady;
      });

      overlays.default = final: prev: {
        shady = shadyFor final;
      };

      nixosModules = rec {
        shady = import ./nix/module.nix self;
        default = shady;
      };

      nixosConfigurations.gloam-live = nixpkgs.lib.nixosSystem {
        system = "x86_64-linux";
        modules = [ (import ./distro/iso.nix self) ];
      };

      checks = nixpkgs.lib.genAttrs [ "x86_64-linux" ] (system: {
        session = nixpkgs.legacyPackages.${system}.testers.runNixOSTest (import ./nix/tests/session.nix self);
        greeter = nixpkgs.legacyPackages.${system}.testers.runNixOSTest (import ./nix/tests/greeter.nix self);
      });

      devShells = forAllSystems (pkgs: let wlroots = wlrootsFor pkgs; in {
        default = pkgs.mkShell {
          strictDeps = true;
          # Follow the selected wlroots package’s dependency set.
          inputsFrom = [ wlroots ];
          nativeBuildInputs = with pkgs; [ pkg-config meson ninja wayland-scanner wlr-protocols ];
          buildInputs = [ wlroots ] ++ (with pkgs; [
            wayland wayland-protocols libxkbcommon pixman libdrm
            mesa libglvnd libffi libxau libxdmcp lua5_4
            cairo cairo.dev pango pango.dev
          ]);
          packages = with pkgs; [ stdenv.cc gdb foot seatd wlr-randr grim wtype nodejs ];
          shellHook = ''
            export PKG_CONFIG_PATH=${pkgs.cairo.dev}/lib/pkgconfig:${pkgs.pango.dev}/lib/pkgconfig:${pkgs.wlr-protocols}/share/pkgconfig:$PKG_CONFIG_PATH
          '';
        };
      });
    };
}
