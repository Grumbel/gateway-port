{
  description = "Gateway Linux port and reverse engineering tools";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      eachSystem = nixpkgs.lib.genAttrs systems;
    in {
      packages = eachSystem (system:
        let pkgs = import nixpkgs { inherit system; }; in {
          default = pkgs.stdenv.mkDerivation {
            pname = "gateway-port";
            version = "0.1.0";
            src = pkgs.lib.cleanSourceWith {
              src = self;
              filter = path: type:
                let name = builtins.baseNameOf path;
                in pkgs.lib.cleanSourceFilter path type &&
                  !(type == "directory" &&
                    (pkgs.lib.hasPrefix "build" name || name == ".direnv"));
            };
            nativeBuildInputs = [ pkgs.cmake pkgs.python3 ];
            buildInputs = [ pkgs.sdl3 pkgs.unicorn ];
            doCheck = true;
          };
        });
      checks = eachSystem (system: { build = self.packages.${system}.default; });
      devShells = eachSystem (system:
        let pkgs = import nixpkgs { inherit system; }; in {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.default ];
            packages = [ pkgs.ninja pkgs.pkg-config pkgs.clang-tools pkgs.gdb pkgs.nasm ];
          };
        });
    };
}
