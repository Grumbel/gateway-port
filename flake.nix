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
          # Explicitly opt-in, SHA-512-pinned historic compiler distribution.
          # This is proprietary Microsoft software: do not commit it to Git or
          # redistribute the resulting closure. No default package/check
          # depends on it; only the explicit msc51 dev shell requests it.
          msc51 = pkgs.runCommand "gateway-microsoft-c-5.1" {
            archive = pkgs.fetchurl {
              name = "microsoft-c-5.1-1988-floppies.7z";
              urls = [
                "https://dl.winworldpc.com/Abandonware%20Applications/PC/Microsoft%20C%205.1%20Optimizing%20Compiler%20(5.25).7z"
                "https://dl-alt1.winworldpc.com/Abandonware%20Applications/PC/Microsoft%20C%205.1%20Optimizing%20Compiler%20(5.25).7z"
              ];
              hash = "sha512-WDfi77qmgoXVwlh+PdKm6uJpOiG078akK0GtIvgXkcQMLaZO7VRj/Z4IV8I4Hv4M4yCE6m1uqxFzXAV8z8O6gA==";
            };
            nativeBuildInputs = [ pkgs._7zz pkgs.mtools pkgs.python3 ];
          } ''
            mkdir extracted
            7zz x -y "$archive" -oextracted >/dev/null
            python3 ${source}/tools/msc51_unpack.py extracted "$out"
          '';
        });
      apps = eachSystem (system:
        let
          pkgs = import nixpkgs { inherit system; };
          sourceCSetup = ''
              if [ "$#" -ne 1 ] && [ "$#" -ne 2 ]; then
                echo "usage: $0 DOS-DIR [guest-22s.bin]" >&2
                echo "   or: $0 guest-22s.bin DOS-DIR" >&2
                exit 2
              fi
              dos_dir="$1"
              if [ "$#" -eq 2 ] && [ -f "$1" ] && [ -d "$2" ]; then
                dos_dir="$2"
                snapshot="$1"
              fi
              if [ "$#" -eq 1 ]; then
                if [ ! -f "$dos_dir/GATE.EXE" ]; then
                  echo "missing $dos_dir/GATE.EXE" >&2
                  exit 1
                fi
                # The source-C renderer builds its observed DOS/RTLink
                # inputs directly from the user's unmodified original assets.
                # No Unicorn startup snapshot is needed on the default path.
                snapshot="--from-original"
              elif [ -z "''${snapshot:-}" ]; then
                snapshot="$2"
              fi
          '';
          sourceCLive = pkgs.writeShellApplication {
            name = "gateway-intro-source-c-live";
            runtimeInputs = [ pkgs.coreutils ];
            text = sourceCSetup + ''
              ${self.packages.${system}.default}/bin/gateway-intro-source-c-live \
                "$snapshot" "$dos_dir"
            '';
          };
          sourceCPlay = pkgs.writeShellApplication {
            name = "gateway-intro-source-c-play";
            runtimeInputs = [ pkgs.coreutils ];
            text = sourceCSetup + ''
              if [ ! -f "$dos_dir/GATEMAIN.EXE" ]; then
                echo "missing $dos_dir/GATEMAIN.EXE" >&2
                exit 1
              fi
              ${self.packages.${system}.default}/bin/gateway-intro-source-c-live \
                "$snapshot" "$dos_dir" --exit-on-complete
              # Captured at GATE.EXE's verified 4b03 overlay-load handoff.
              # GATEMAIN.EXE can run independently with this DOS command tail.
              ${self.packages.${system}.default}/bin/gateway \
                "$dos_dir/GATEMAIN.EXE" --game-args "0 258 9 2 816 1 7 544"
            '';
          };
          dosLauncher = pkgs.writeShellApplication {
            name = "gateway-dos";
            runtimeInputs = [ pkgs.coreutils pkgs.dosbox-staging ];
            text = ''
              exec ${pkgs.bash}/bin/bash ${self}/tools/gateway_dos.sh "$@"
            '';
          };
          gatewayApp = {
            type = "app";
            program = "${self.packages.${system}.default}/bin/gateway";
            meta.description = "Run the original Gateway DOS game through the SDL3/Unicorn port";
          };
        in {
        default = gatewayApp;
        gateway = gatewayApp;
        gateway-dos = {
          type = "app";
          program = "${dosLauncher}/bin/gateway-dos";
          meta.description = "Run the recompiled source-C DOS intro in DOSBox Staging (requires locally built GATEWAY.EXE)";
        };
        gateway-decomp = {
          type = "app";
          program = "${sourceCLive}/bin/gateway-intro-source-c-live";
          meta.description = "Play the reconstructed source-C intro in SDL3 with AdLib audio and input";
        };
        gateway-hybrid = {
          type = "app";
          program = "${sourceCPlay}/bin/gateway-intro-source-c-play";
          meta.description = "Play the source-C intro in SDL3, then enter the Unicorn-powered Gateway game";
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
          flake-layout = pkgs.runCommand "gateway-port-flake-layout" {
            nativeBuildInputs = [ pkgs.python3 pkgs.bash ];
            src = self;
          } ''
            cd "$src"
            python3 tests/test_flake_layout.py
            touch "$out"
          '';
          msc51-tooling = pkgs.runCommand "gateway-port-msc51-tooling" {
            nativeBuildInputs = [ pkgs.python3 pkgs.mtools ];
            src = self;
          } ''
            cd "$src"
            python3 tests/test_msc51.py
            python3 tests/test_msc51_link_report.py
            python3 tests/test_msc51_compare.py
            python3 tests/test_msc51_unpack.py
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
          # Research binaries live behind local development commands, not
          # public flake apps. Build them from the checkout being edited.
          devUtility = name: pkgs.writeShellScriptBin name ''
            set -euo pipefail
            gateway-build
            exec "$GATEWAY_BUILD_DIR/${name}" "$@"
          '';
          devUtilities = map devUtility [
            "gateway-probe"
            "gateway-intro"
            "gateway-intro-dump"
            "gateway-intro-source-c-captions"
            "gateway-intro-source-c-dump"
            "gateway-intro-source-c-picture-dump"
            "gateway-intro-source-c-replay"
            "gateway-intro-source-c-live"
          ];
          sourceCFull = pkgs.writeShellScriptBin "gateway-intro-source-c-full" ''
            ${sourceCheck}
            exec ${pkgs.bash}/bin/bash "$GATEWAY_SOURCE/tools/gateway_intro_source_c_full.sh" "$@"
          '';
          # The actual DOS research tools are Python subcommands, not
          # independent applications. Keep them in the dev shell rather
          # than exporting dozens of near-identical flake outputs.
          msc51Command = pkgs.writeShellScriptBin "gateway-msc51" ''
            set -euo pipefail
            if [ "$#" -eq 0 ] || [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
              echo "usage: gateway-msc51 SUBCOMMAND [options]"
              echo "  compare       Compare the original Gateway RTL against Microsoft C"
              echo "  unpack        Assemble a compiler from offline floppy images"
              echo "  link-report   Analyze an existing Microsoft LINK.LOG"
              echo "  The remaining compiler/probe subcommands and options follow:"
              exec ${pkgs.python3}/bin/python3 ${self}/tools/msc51.py --help
            fi
            subcommand="$1"
            shift
            case "$subcommand" in
              compare)
                exec ${pkgs.python3}/bin/python3 ${self}/tools/msc51_compare.py "$@"
                ;;
              unpack)
                exec ${pkgs.python3}/bin/python3 ${self}/tools/msc51_unpack.py "$@"
                ;;
              link-report)
                exec ${pkgs.python3}/bin/python3 ${self}/tools/msc51_link_report.py "$@"
                ;;
              *)
                exec ${pkgs.python3}/bin/python3 ${self}/tools/msc51.py "$subcommand" "$@"
                ;;
            esac
          '';
        in {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.default ];
            packages = [ pkgs.ninja pkgs.pkg-config pkgs.clang-tools pkgs.gdb pkgs.nasm
                         configure build run runGdb sourceCFull msc51Command ] ++ devUtilities;
            shellHook = ''
              export GATEWAY_SOURCE="$PWD"
              export GATEWAY_BUILD_DIR="''${GATEWAY_BUILD_DIR:-/tmp/gateway-port-build}"
              case $- in
                *i*)
                  printf '\n%s\n' \
                    'Gateway development shell' \
                    '=========================' \
                    "Source: $GATEWAY_SOURCE" \
                    "Build:  $GATEWAY_BUILD_DIR (Debug by default)" \
                    '' \
                    'Build and run (SDL3 + Unicorn)' \
                    '  gateway-configure                 Configure CMake/Ninja' \
                    '  gateway-build                     Build the project' \
                    '  ctest --test-dir "$GATEWAY_BUILD_DIR" --output-on-failure' \
                    '  gateway-run ../gateway-dos/GATE.EXE' \
                    '  gateway-run-gdb ../gateway-dos/GATE.EXE' \
                    '' \
                    'Source-C intro development' \
                    '  gateway-intro-source-c-live --from-original ../gateway-dos' \
                    '  gateway-intro-source-c-full ../gateway-dos      Silent replay' \
                    '  gateway-intro-source-c-dump SNAPSHOT DOS-DIR OUT-DIR' \
                    '  gateway-intro-source-c-picture-dump SNAPSHOT DOS-DIR OUT.ppm' \
                    '  gateway-intro-source-c-captions SNAPSHOT DOS-DIR' \
                    '  gateway-intro-source-c-replay FILE.gvr' \
                    '' \
                    'Emulation diagnostics and captures' \
                    '  gateway-probe ../gateway-dos/GATE.EXE --headless --milliseconds 15000' \
                    '  gateway-intro ../gateway-dos' \
                    '  gateway-intro-dump ../gateway-dos --ms 22000 OUT.ppm' \
                    '' \
                    'Specialized tools' \
                    '  gateway-msc51 --help             DOS compiler, probes and link audit' \
                    '  nix develop .#msc51              Compiler + DOSBox Staging shell' \
                    '  nix develop .#ghidra             Ghidra + PyGhidra shell' \
                    '' \
                    'Debugging: gateway-run-gdb; Ctrl-C then bt / continue.' \
                    'Unicorn guest registers are not native GDB registers.' \
                    'See README.md for commands and required original assets.' \
                    ''
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
            shellHook = ''
              case $- in
                *i*)
                  printf '\n%s\n' \
                    'Ghidra reverse engineering' \
                    '--------------------------' \
                    '  gateway-decompile-intro GATE.EXE GATE.RTL --snapshot RAM.bin --output DIR' \
                    '  gateway-decompile-intro --help' \
                    '  See docs/intro-ghidra.md for the snapshot workflow.' \
                    ''
                  ;;
              esac
            '';
          };
          msc51 = pkgs.mkShell {
            inputsFrom = [ self.devShells.${system}.default ];
            packages = [ pkgs.dosbox-staging pkgs.python3 pkgs.mtools
                         pkgs._7zz ];
            shellHook = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              case $- in
                *i*)
                  printf '\n%s\n' \
                    'Microsoft C 5.1 / DOSBox tools' \
                    '-----------------------------' \
                    '  gateway-msc51 --help             List all compiler and probe commands' \
                    '  gateway-msc51 compile-launcher    Compile the source-C launcher objects' \
                    '  gateway-msc51 compile-data --original-game-dir ../gateway-dos' \
                    '  gateway-msc51 link-launcher       Link GATEWAY.EXE after prerequisite probes' \
                    '  gateway-msc51 test-dos-entry --original-game-dir ../gateway-dos' \
                    '  nix run .#gateway-dos -- ../gateway-dos /INTRO' \
                    ''
                  ;;
              esac
            '';
          };
        });
    };
}
