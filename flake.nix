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
        version = "main";

        src = pkgs.fetchFromGitHub {
          owner = "shadowash8";
          repo = "nauka";
          rev = "main";
          hash = "sha256-pD439Emit1XgluVvWgZJh/vkckUk5JStf+sTALRHXBM=";
        };

        nativeBuildInputs = with pkgs; [
          meson
          ninja
          pkg-config
          wayland-scanner
          makeBinaryWrapper
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
    
        postInstall = ''
          wrapProgram $out/bin/nauka \
            --prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.xwayland pkgs.xwayland-satellite ]}
        '';
      };
    });
  };
}
