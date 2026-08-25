{
  description = "nauka - A small and minimal Wayland compositor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }: let
    supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
    forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
  in {
    packages = forAllSystems (system: let
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      default = pkgs.stdenv.mkDerivation {
        pname = "nauka";
        version = "0.1.0";

        src = ./.;

        nativeBuildInputs = with pkgs; [
          meson
          ninja
          pkg-config
	  wayland-scanner
        ];

        buildInputs = with pkgs; [
          wayland
          wayland-protocols
	  wlroots_0_20
          libinput
          libdrm
          pixman
	  libGL
          libxkbcommon
          seatd
	  scenefx
        ];
      };
    });

    devShells = forAllSystems (system: let
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      default = pkgs.mkShell {
        inputsFrom = [ self.packages.${system}.default ];
      };
    });
  };
}
