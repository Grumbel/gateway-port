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
      apps = eachSystem (system: {
        default = { type = "app"; program = "${self.packages.${system}.default}/bin/gateway"; };
      });
      checks = eachSystem (system:
        let pkgs = import nixpkgs { inherit system; }; in {
          build = self.packages.${system}.default;
          reuse = pkgs.runCommand "gateway-port-reuse" {
            nativeBuildInputs = [ pkgs.reuse ];
            src = pkgs.lib.cleanSourceWith {
              src = self;
              filter = path: type:
                let name = builtins.baseNameOf path;
                in pkgs.lib.cleanSourceFilter path type &&
                  !(type == "directory" &&
                    (pkgs.lib.hasPrefix "build" name || name == ".direnv"));
            };
          } ''
            cd "$src"
            reuse lint
            touch "$out"
          '';
        });
      devShells = eachSystem (system:
        let
          pkgs = import nixpkgs { inherit system; };
          sourceCheck = ''
            set -euo pipefail
            : "''${GATEWAY_SOURCE:?Enter the development shell with nix develop}"
            : "''${GATEWAY_BUILD_DIR:?Enter the development shell with nix develop}"
          '';
          configure = pkgs.writeShellScriptBin "gateway-configure" ''
            ${sourceCheck}
            cmake -S "$GATEWAY_SOURCE" -B "$GATEWAY_BUILD_DIR" -G Ninja \
              -DCMAKE_BUILD_TYPE="''${CMAKE_BUILD_TYPE:-Debug}" \
              -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
            if [ ! -e "$GATEWAY_SOURCE/build" ] && [ ! -L "$GATEWAY_SOURCE/build" ]; then
              ln -s "$GATEWAY_BUILD_DIR" "$GATEWAY_SOURCE/build"
            fi
          '';
          build = pkgs.writeShellScriptBin "gateway-build" ''
            ${sourceCheck}
            if [ ! -f "$GATEWAY_BUILD_DIR/CMakeCache.txt" ] || \
               ! ${pkgs.gnugrep}/bin/grep -Fxq "CMAKE_HOME_DIRECTORY:INTERNAL=$GATEWAY_SOURCE" \
                   "$GATEWAY_BUILD_DIR/CMakeCache.txt"; then
              gateway-configure
            fi
            cmake --build "$GATEWAY_BUILD_DIR"
          '';
          run = pkgs.writeShellScriptBin "gateway-run" ''
            ${sourceCheck}
            gateway-build
            exec "$GATEWAY_BUILD_DIR/gateway" "$@"
          '';
          runGdb = pkgs.writeShellScriptBin "gateway-run-gdb" ''
            ${sourceCheck}
            gateway-build
            exec gdb -q \
              -ex 'set pagination off' \
              -ex 'set confirm off' \
              -ex 'set debuginfod enabled off' \
              -ex run \
              -ex 'python
try:
  if int(gdb.parse_and_eval("$_exitcode")) == 0:
    gdb.execute("quit")
except Exception:
  pass
' \
              --args "$GATEWAY_BUILD_DIR/gateway" "$@"
          '';
        in {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.default ];
            packages = [ pkgs.ninja pkgs.pkg-config pkgs.clang-tools pkgs.gdb pkgs.nasm
                         configure build run runGdb ];
            shellHook = ''
              export GATEWAY_SOURCE="$PWD"
              export GATEWAY_BUILD_DIR="''${GATEWAY_BUILD_DIR:-/tmp/gateway-port-build}"
              case $- in
                *i*)
                  printf '%s\n' \
                    'Gateway development shell' \
                    '=========================' \
                    "Source: $GATEWAY_SOURCE" \
                    "Build:  $GATEWAY_BUILD_DIR (Debug by default)" \
                    '  gateway-configure                   Configure CMake/Ninja' \
                    '  gateway-build                       Build the project' \
                    '  gateway-run ../101/S101.EXE         Build and run Spellcasting 101' \
                    '  gateway-run ../201/S201.EXE         Build and run Spellcasting 201' \
                    '  gateway-run-gdb ../101/S101.EXE     Build and debug under GDB' \
                    '  nix develop .#ghidra                Optional Ghidra reverse engineering shell' \
                    'Troubleshooting:' \
                    '  gateway-run ../201/S201.EXE --trace-video' \
                    '  gateway-run ../101/S101.EXE --headless --milliseconds 15000 \' \
                    '    --dump-prefix /tmp/s101-stall --trace-blocks /tmp/s101-blocks.txt' \
                    'GDB: press Ctrl-C to interrupt a frozen game, then use bt / continue.' \
                    'Guest DOS registers are inside Unicorn, not native GDB registers.' \
                    'More details: README.md (Debugging with nix develop).'
                  ;;
              esac
            '';
          };
          # Ghidra 12 runs .py scripts only through PyGhidra (Jython was
          # removed), so the shell provides a Python with pyghidra and the
          # installation directory that tools/run_ghidra.py launches. Use the
          # source-built ghidra package: ghidra-bin's prebuilt decompiler
          # process crashes on NixOS.
          ghidra = pkgs.mkShell {
            inputsFrom = [ self.devShells.${system}.default ];
            packages = [ pkgs.ghidra pkgs.openjdk21
                         (pkgs.python3.withPackages (ps: [ ps.pyghidra ])) ];
            GHIDRA_INSTALL_DIR = "${pkgs.ghidra}/lib/ghidra";
          };
        });
    };
}
