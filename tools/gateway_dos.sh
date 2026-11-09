#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

# Build the source-C MS-DOS executable using the Nix-supplied Microsoft C
# toolchain on first run, then launch it with original files mounted read-only.
# The proprietary original game is never copied into the Nix store.
set -euo pipefail

dos_dir="${1:-../gateway-dos}"
if [ "$#" -gt 0 ]; then shift; fi
mode="${1:-/INTRO}"
if [ "$#" -gt 0 ]; then shift; fi
if [ "$#" -gt 0 ]; then
  echo 'usage: gateway-dos [DOS-DIR [/INTRO|/CHECK|/PICTURE|/SECOND|/TITLE|/TRANS|/SCENE]]' >&2
  exit 2
fi
case "$mode" in
  /INTRO|/CHECK|/PICTURE|/SECOND|/TITLE|/TRANS|/SCENE) ;;
  *) echo "unsupported GATEWAY.EXE mode: $mode" >&2; exit 2 ;;
esac

if [ ! -f "$dos_dir/GATE.EXE" ] || [ ! -f "$dos_dir/GATE.RTL" ]; then
  echo "Gateway originals not found in $dos_dir (need GATE.EXE and GATE.RTL)" >&2
  exit 1
fi
dos_dir="$(realpath "$dos_dir")"
source_dir="${GATEWAY_MSC51_SOURCE_DIR:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}"
compiler_dir="${GATEWAY_MSC51_DIR:-}"
if [ -z "$compiler_dir" ] || [ ! -d "$compiler_dir/BIN" ]; then
  echo 'gateway-dos needs its Nix-supplied Microsoft C 5.1 toolchain (GATEWAY_MSC51_DIR).' >&2
  exit 1
fi
launcher_dir="${GATEWAY_MSC51_BUILD_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/gateway-port/msc51}"
mkdir -p "$launcher_dir"
launcher_dir="$(realpath "$launcher_dir")"
source_dir="$(realpath "$source_dir")"
compiler_dir="$(realpath "$compiler_dir")"
dosbox_bin="${GATEWAY_MSC51_DOSBOX:-dosbox-staging}"

# DOSBox's -c interpreter parses its own quotes, independent of Bash.
for path in "$dos_dir" "$launcher_dir" "$compiler_dir"; do
  case "$path" in
    *'"'*|*$'\n'*|*$'\r'*) echo "unsupported DOSBox mount path: $path" >&2; exit 2 ;;
  esac
done

# Nix gives each source revision and compiler its own immutable store path.
# A successful build is reusable until either input changes. Check the stamp
# only after the complete compiler, linker and executable smoke-test chain;
# interrupted or failed rebuilds must never be considered ready.
stamp="$source_dir|$compiler_dir"
stamp_file="$launcher_dir/GATEWAY.SRC"
# Serialize builds sharing one cache directory. Keep the lock only through
# compilation; gameplay can run concurrently in independent DOSBox sessions.
exec 9> "$launcher_dir/GATEWAY.LCK"
flock 9
if [ "${GATEWAY_DOS_REBUILD:-0}" = 1 ] || [ ! -f "$launcher_dir/GATEWAY.EXE" ] || \
   [ ! -f "$stamp_file" ] || [ "$(<"$stamp_file")" != "$stamp" ]; then
  printf 'gateway-dos: compiling source-C MS-DOS launcher (first run may take a while)\n' >&2
  # Do not allow a stamp from an earlier build to survive an unsuccessful run.
  rm -f "$stamp_file"
  printf 'gateway-dos: compile-all (one DOSBox session)\n' >&2
  python3 "$source_dir/tools/msc51.py" compile-all \
    --compiler "$compiler_dir" --output "$launcher_dir" \
    --dosbox "$dosbox_bin" --headless --original-game-dir "$dos_dir"
  printf '%s\n' "$stamp" > "$stamp_file"
fi
flock -u 9
exec 9>&-

# Match the explicit CPU settings of the verified Microsoft C 5.1 build
# session. The original DOS program has no host CPU-cycle calibration: the
# DOSBox Staging default can otherwise make the reproduced VGA routines
# appear dramatically slower without any change to the recovered delays.
exec "$dosbox_bin" \
  --set core=dynamic \
  --set cpu_cycles=max \
  -c "mount e \"$dos_dir\" -ro" \
  -c "mount d \"$launcher_dir\"" \
  -c 'e:' \
  -c "D:\\GATEWAY.EXE $mode" \
  --exit
