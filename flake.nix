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
          # Historical compiler: run the *user-provided* Microsoft C 5.1
          # installation under DOSBox, never fetch/store proprietary binaries.
          # The probe stages C89-compatible copies of the recovered C routines
          # in build/ rather than attempting to package the original compiler.
          msc51-probe = pkgs.writeShellApplication {
            name = "gateway-msc51-probe";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py probe \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-shell = pkgs.writeShellApplication {
            name = "gateway-msc51-shell";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py shell \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-prepare = pkgs.writeShellApplication {
            name = "gateway-msc51-prepare";
            runtimeInputs = [ pkgs.python3 ];
            text = ''
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py prepare "$@"
            '';
          };
          msc51-install = pkgs.writeShellApplication {
            name = "gateway-msc51-install";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py install \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
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
              replay_dir="$(mktemp -d)"
              trap 'rm -f "$replay_dir/replay.gvr" "$replay_dir/final.ppm" "$replay_dir/guest-22s.bin" "$replay_dir/guest-22s.ppm" "$replay_dir/guest-22s.regs" "$replay_dir/probe.log"; rmdir "$replay_dir"' EXIT
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
          sourceCFull = pkgs.writeShellApplication {
            name = "gateway-intro-source-c-full";
            runtimeInputs = [ pkgs.coreutils ];
            text = sourceCSetup + ''
              ${self.packages.${system}.intro-decompile}/bin/gateway-intro-source-c-composite \
                "$snapshot" "$dos_dir" "$replay_dir/final.ppm" --full --record "$replay_dir/replay.gvr"
              ${self.packages.${system}.default}/bin/gateway-intro-source-c-replay \
                "$replay_dir/replay.gvr"
            '';
          };
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
        in {
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
        intro-source-c = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/gateway-intro-source-c-captions";
          meta.description = "Play all eighteen original source-C caption pages (requires a DOS RAM snapshot)";
        };
        intro-source-c-dump = {
          type = "app";
          program = "${self.packages.${system}.intro-decompile}/bin/gateway-intro-source-c-dump";
          meta.description = "Export all eighteen original source-C caption frames without SDL3";
        };
        intro-source-c-picture-dump = {
          type = "app";
          program = "${self.packages.${system}.intro-decompile}/bin/gateway-intro-source-c-picture-dump";
          meta.description = "Render one real Gateway PIC through the reconstructed C loader and VGA path";
        };
        intro-source-c-full = {
          type = "app";
          program = "${sourceCFull}/bin/gateway-intro-source-c-full";
          meta.description = "Play the original source-C Gateway intro with PIC images, palette fades, animations and all captions (silent)";
        };
        intro-source-c-live = {
          type = "app";
          program = "${sourceCLive}/bin/gateway-intro-source-c-live";
          meta.description = "Play the source-C Gateway intro live from original DOS assets through SDL3 with AdLib music and keyboard input";
        };
        intro-source-c-play = {
          type = "app";
          program = "${sourceCPlay}/bin/gateway-intro-source-c-play";
          meta.description = "Play the source-C intro with AdLib audio, then enter the original Unicorn-powered Gateway game";
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
        msc51-probe = {
          type = "app";
          program = "${self.packages.${system}.msc51-probe}/bin/gateway-msc51-probe";
          meta.description = "Compile and run the recovered intro random-number probe with a local Microsoft C 5.1 installation in DOSBox";
        };
        msc51-shell = {
          type = "app";
          program = "${self.packages.${system}.msc51-shell}/bin/gateway-msc51-shell";
          meta.description = "Open DOSBox with a user-installed Microsoft C 5.1 compiler and writable Gateway work directory";
        };
        msc51-prepare = {
          type = "app";
          program = "${self.packages.${system}.msc51-prepare}/bin/gateway-msc51-prepare";
          meta.description = "Stage the recovered intro C sources for Microsoft C 5.1 without running DOSBox";
        };
        msc51-install = {
          type = "app";
          program = "${self.packages.${system}.msc51-install}/bin/gateway-msc51-install";
          meta.description = "Install user-provided Microsoft C 5.1 floppy images into a local directory using DOSBox";
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
          msc51-tooling = pkgs.runCommand "gateway-port-msc51-tooling" {
            nativeBuildInputs = [ pkgs.python3 ];
            src = self;
          } ''
            cd "$src"
            python3 tests/test_msc51.py
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
          msc51 = pkgs.mkShell {
            inputsFrom = [ self.devShells.${system}.default ];
            packages = [ pkgs.dosbox-staging pkgs.python3
                         self.packages.${system}.msc51-probe
                         self.packages.${system}.msc51-shell
                         self.packages.${system}.msc51-prepare
                         self.packages.${system}.msc51-install ];
          };
        });
    };
}
