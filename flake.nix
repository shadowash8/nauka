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
           version = "0.2.0";

           # Build the checkout the flake lives in, not a remote copy.
           src = self;

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
             scenefx_0_5  
           ];

           postInstall = ''
             wrapProgram $out/bin/nauka \
               --prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.xwayland pkgs.xwayland-satellite ]}
           '';

           passthru.providedSessions = [ "nauka" ];
         };
       });
     };
   }
