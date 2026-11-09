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
          # depends on it; only the msc51 apps and dev shell request it.
          msc51-archive = pkgs.fetchurl {
            name = "microsoft-c-5.1-1988-floppies.7z";
            urls = [
              "https://dl.winworldpc.com/Abandonware%20Applications/PC/Microsoft%20C%205.1%20Optimizing%20Compiler%20(5.25).7z"
              "https://dl-alt1.winworldpc.com/Abandonware%20Applications/PC/Microsoft%20C%205.1%20Optimizing%20Compiler%20(5.25).7z"
            ];
            hash = "sha512-WDfi77qmgoXVwlh+PdKm6uJpOiG078akK0GtIvgXkcQMLaZO7VRj/Z4IV8I4Hv4M4yCE6m1uqxFzXAV8z8O6gA==";
          };
          msc51 = pkgs.runCommand "gateway-microsoft-c-5.1" {
            archive = self.packages.${system}.msc51-archive;
            nativeBuildInputs = [ pkgs._7zz pkgs.mtools pkgs.python3 ];
          } ''
            mkdir extracted
            7zz x -y "$archive" -oextracted >/dev/null
            python3 ${source}/tools/msc51_unpack.py extracted "$out"
          '';
          # These wrappers use the pinned Nix compiler by default. An
          # explicit --compiler DIR still selects a separate installation.
          msc51-probe = pkgs.writeShellApplication {
            name = "gateway-msc51-probe";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py probe \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-shell = pkgs.writeShellApplication {
            name = "gateway-msc51-shell";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
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
          msc51-prepare-launcher = pkgs.writeShellApplication {
            name = "gateway-msc51-prepare-launcher";
            runtimeInputs = [ pkgs.python3 ];
            text = ''
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py prepare-launcher "$@"
            '';
          };
          msc51-compile-launcher = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-launcher";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-launcher \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-memory = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-memory";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-memory \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-selectors = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-selectors";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-selectors \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-data = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-data";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-data \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-dos-files = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-dos-files";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-dos-files \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-rtl-state = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-rtl-state";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-rtl-state \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-hardware = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-hardware";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-hardware \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-runtime-files = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-runtime-files";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-runtime-files \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-picture-dcl = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-picture-dcl";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-picture-dcl \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-resident-state = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-resident-state";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-resident-state \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-heap = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-heap";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-heap \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-font = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-font";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-font \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-stream = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-stream";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-stream \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-input = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-input";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-input \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-clock = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-clock";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-clock \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-compile-crt = pkgs.writeShellApplication {
            name = "gateway-msc51-compile-crt";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py compile-crt \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          msc51-link-launcher = pkgs.writeShellApplication {
            name = "gateway-msc51-link-launcher";
            runtimeInputs = [ pkgs.python3 pkgs.dosbox-staging ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51.py link-launcher \
                --dosbox ${pkgs.dosbox-staging}/bin/dosbox-staging "$@"
            '';
          };
          # Read existing LINK.LOG without launching DOSBox or fetching MSC.
          msc51-link-report = pkgs.writeShellApplication {
            name = "gateway-msc51-link-report";
            runtimeInputs = [ pkgs.python3 ];
            text = ''
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51_link_report.py "$@"
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
          msc51-compare = pkgs.writeShellApplication {
            name = "gateway-msc51-compare";
            runtimeInputs = [ pkgs.python3 ];
            text = ''
              export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51_compare.py "$@"
            '';
          };
          msc51-unpack = pkgs.writeShellApplication {
            name = "gateway-msc51-unpack";
            runtimeInputs = [ pkgs.python3 pkgs.mtools ];
            text = ''
              exec ${pkgs.python3}/bin/python3 ${source}/tools/msc51_unpack.py "$@"
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
        probe = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/gateway-probe";
          meta.description = "Capture deterministic headless DOS execution and conventional RAM snapshots with Unicorn";
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
        msc51-prepare-launcher = {
          type = "app";
          program = "${self.packages.${system}.msc51-prepare-launcher}/bin/gateway-msc51-prepare-launcher";
          meta.description = "Stage all reconstructed Gateway launcher C modules as 16-bit DOS C89 sources without downloading the compiler";
        };
        msc51-compile-launcher = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-launcher}/bin/gateway-msc51-compile-launcher";
          meta.description = "Attempt to compile all 28 reconstructed launcher modules to DOS objects with Microsoft C 5.1";
        };
        msc51-compile-memory = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-memory}/bin/gateway-msc51-compile-memory";
          meta.description = "Compile and verify the Gateway DOS segmented-memory adapter with Microsoft C 5.1";
        };
        msc51-compile-selectors = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-selectors}/bin/gateway-msc51-compile-selectors";
          meta.description = "Probe two overlapping DOS segment windows and 45 startup selector bindings with Microsoft C 5.1";
        };
        msc51-compile-data = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-data}/bin/gateway-msc51-compile-data";
          meta.description = "Probe copying the exact 5976-byte static EXE data range into the original DOS segment window";
        };
        msc51-compile-dos-files = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-dos-files}/bin/gateway-msc51-compile-dos-files";
          meta.description = "Compile and verify the Gateway DOS file-service adapter with Microsoft C 5.1";
        };
        msc51-compile-rtl-state = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-rtl-state}/bin/gateway-msc51-compile-rtl-state";
          meta.description = "Compile and verify 40 original RTL state accessors and live segment selectors with Microsoft C 5.1";
        };
        msc51-compile-hardware = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-hardware}/bin/gateway-msc51-compile-hardware";
          meta.description = "Compile DOS VGA port I/O and verified resident clipping state with Microsoft C 5.1";
        };
        msc51-compile-runtime-files = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-runtime-files}/bin/gateway-msc51-compile-runtime-files";
          meta.description = "Compile and probe FAR string, DOS path, and intro diagnostic services with Microsoft C 5.1";
        };
        msc51-compile-picture-dcl = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-picture-dcl}/bin/gateway-msc51-compile-picture-dcl";
          meta.description = "Compile and verify streaming PKWARE DCL image decoding with Microsoft C 5.1";
        };
        msc51-compile-resident-state = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-resident-state}/bin/gateway-msc51-compile-resident-state";
          meta.description = "Compile and probe genuine DOS BIOS video-mode transitions and original resident drawing state";
        };
        msc51-compile-heap = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-heap}/bin/gateway-msc51-compile-heap";
          meta.description = "Compile and test DOS heap allocation and release, including VFX buffers larger than 64KiB";
        };
        msc51-compile-font = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-font}/bin/gateway-msc51-compile-font";
          meta.description = "Run recovered RTL font-cache cleanup from a Microsoft C 5.1 launcher object with real DOS heap ownership";
        };
        msc51-compile-stream = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-stream}/bin/gateway-msc51-compile-stream";
          meta.description = "Verify Ctrl-Break quiet stream close and nested no-write flush under Microsoft C 5.1 and DOSBox";
        };
        msc51-compile-input = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-input}/bin/gateway-msc51-compile-input";
          meta.description = "Compile and probe BIOS keyboard queue, original unget state and DOS mouse button query";
        };
        msc51-compile-clock = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-clock}/bin/gateway-msc51-compile-clock";
          meta.description = "Compile and probe DOS elapsed-time and animation delay boundaries";
        };
        msc51-compile-crt = {
          type = "app";
          program = "${self.packages.${system}.msc51-compile-crt}/bin/gateway-msc51-compile-crt";
          meta.description = "Compile and probe original Microsoft C FAR getenv and atol runtime services";
        };
        msc51-link-launcher = {
          type = "app";
          program = "${self.packages.${system}.msc51-link-launcher}/bin/gateway-msc51-link-launcher";
          meta.description = "Attempt Microsoft LINK on the 28 launcher objects and report unresolved original DOS services";
        };
        msc51-link-report = {
          type = "app";
          program = "${self.packages.${system}.msc51-link-report}/bin/gateway-msc51-link-report";
          meta.description = "Classify Microsoft LINK unresolved symbols using original Gateway source names and addresses; no DOSBox required";
        };
        msc51-install = {
          type = "app";
          program = "${self.packages.${system}.msc51-install}/bin/gateway-msc51-install";
          meta.description = "Install user-provided Microsoft C 5.1 floppy images into a local directory using DOSBox";
        };
        msc51-compare = {
          type = "app";
          program = "${self.packages.${system}.msc51-compare}/bin/gateway-msc51-compare";
          meta.description = "Fetch pinned Microsoft C 5.1 and compare its runtime library against the original Gateway CRT";
        };
        msc51-unpack = {
          type = "app";
          program = "${self.packages.${system}.msc51-unpack}/bin/gateway-msc51-unpack";
          meta.description = "Assemble an offline Microsoft C 5.1 installation from a directory of original DOS floppy images";
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
                         self.packages.${system}.msc51-prepare-launcher
                         self.packages.${system}.msc51-compile-launcher
                         self.packages.${system}.msc51-compile-memory
                         self.packages.${system}.msc51-compile-selectors
                         self.packages.${system}.msc51-compile-data
                         self.packages.${system}.msc51-compile-dos-files
                         self.packages.${system}.msc51-compile-rtl-state
                         self.packages.${system}.msc51-compile-hardware
                         self.packages.${system}.msc51-compile-runtime-files
                         self.packages.${system}.msc51-compile-picture-dcl
                         self.packages.${system}.msc51-compile-resident-state
                         self.packages.${system}.msc51-compile-heap
                         self.packages.${system}.msc51-compile-font
                         self.packages.${system}.msc51-compile-stream
                         self.packages.${system}.msc51-compile-input
                         self.packages.${system}.msc51-compile-clock
                         self.packages.${system}.msc51-compile-crt
                         self.packages.${system}.msc51-link-launcher
                         self.packages.${system}.msc51-link-report
                         self.packages.${system}.msc51-install
                         self.packages.${system}.msc51-compare
                         self.packages.${system}.msc51-unpack ];
          };
        });
    };
}
