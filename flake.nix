{
  description = "Gateway Linux port and reverse engineering tools";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      eachSystem = nixpkgs.lib.genAttrs systems;
    in {
      packages = eachSystem (system:
        let
          pkgs = import nixpkgs { inherit system; };
          source = pkgs.lib.cleanSourceWith {
            src = self;
            filter = path: type:
              let name = builtins.baseNameOf path;
              in pkgs.lib.cleanSourceFilter path type &&
                !(type == "directory" &&
                  (pkgs.lib.hasPrefix "build" name || name == ".direnv"));
          };
        in {
          default = pkgs.stdenv.mkDerivation {
            pname = "gateway-port";
            version = "0.1.0";
            src = source;
            nativeBuildInputs = [ pkgs.cmake pkgs.python3 ];
            buildInputs = [ pkgs.sdl3 pkgs.unicorn ];
            doCheck = true;
          };
          # Independent C-only archive of original-code reconstruction.
          # It is intentionally NOT a playable intro until all linked RTL
          # services and the host adapter have been recovered and verified.
          intro-decompile = pkgs.stdenv.mkDerivation {
            pname = "gateway-intro-decompile";
            version = "0.1.0";
            src = source;
            nativeBuildInputs = [ pkgs.cmake pkgs.python3 ];
            cmakeFlags = [ "-DGATEWAY_INTRO_DECOMPILE_ONLY=ON" ];
            doCheck = true;
            checkPhase = ''
              ctest --output-on-failure
            '';
          };
          # Run Ghidra's batch decompiler on a *relocated* intro snapshot.
          # The original binaries/snapshot are runtime inputs, never copied
          # into the Nix store or the repository's source derivation.
          intro-ghidra =
            let python = pkgs.python3.withPackages (ps: [ ps.pyghidra ]);
            in pkgs.writeShellApplication {
              name = "gateway-decompile-intro";
              runtimeInputs = [ python pkgs.openjdk21 pkgs.ghidra ];
              text = ''
                export GHIDRA_INSTALL_DIR="${pkgs.ghidra}/lib/ghidra"
                exec ${python}/bin/python3 ${source}/tools/run_intro_ghidra.py "$@"
              '';
            };
        });
      apps = eachSystem (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/gateway";
          meta.description = "Run the original Gateway DOS game through the SDL3/Unicorn port";
        };
        intro = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/gateway-intro";
          meta.description = "Preview the Gateway intro with the native C++ renderer";
        };
        intro-dump = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/gateway-intro-dump";
          meta.description = "Render a Gateway intro frame to a PPM image at a specified time";
        };
        intro-ghidra = {
          type = "app";
          program = "${self.packages.${system}.intro-ghidra}/bin/gateway-decompile-intro";
          meta.description = "Decompile a relocated Gateway intro snapshot with Ghidra";
        };
      });
      checks = eachSystem (system:
        let pkgs = import nixpkgs { inherit system; }; in {
          build = self.packages.${system}.default;
          intro-decompile = self.packages.${system}.intro-decompile;
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
            reuse --no-multiprocessing lint
            touch "$out"
          '';
          tracker = pkgs.runCommand "gateway-port-tracker" {
            nativeBuildInputs = [ pkgs.python3 ];
            src = self;
          } ''
            python3 "$src/tools/decomp_status.py" --tracker "$src/decomp/functions.tsv" check
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
                         (pkgs.python3.withPackages (ps: [ ps.pyghidra ]))
                         self.packages.${system}.intro-ghidra ];
            GHIDRA_INSTALL_DIR = "${pkgs.ghidra}/lib/ghidra";
          };
        });
    };
}
