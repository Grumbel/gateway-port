#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

# Launch the separately built source-C MS-DOS executable with proprietary
# game files mounted read-only. Nothing from the original game is packaged.
set -euo pipefail

dos_dir="${1:-../gateway-dos}"
if [ "$#" -gt 0 ]; then shift; fi
mode="${1:-/INTRO}"
if [ "$#" -gt 0 ]; then shift; fi
if [ "$#" -gt 0 ]; then
  echo 'usage: gateway-dos [DOS-DIR [/INTRO|/CHECK|/PICTURE]]' >&2
  exit 2
fi
case "$mode" in
  /INTRO|/CHECK|/PICTURE) ;;
  *) echo "unsupported GATEWAY.EXE mode: $mode" >&2; exit 2 ;;
esac

if [ ! -f "$dos_dir/GATE.EXE" ] || [ ! -f "$dos_dir/GATE.RTL" ]; then
  echo "Gateway originals not found in $dos_dir (need GATE.EXE and GATE.RTL)" >&2
  exit 1
fi
dos_dir="$(realpath "$dos_dir")"
launcher_dir="${GATEWAY_MSC51_BUILD_DIR:-$PWD/build/msc51-launcher}"
if [ ! -f "$launcher_dir/GATEWAY.EXE" ]; then
  echo "Recompiled GATEWAY.EXE not found in $launcher_dir" >&2
  echo 'Use nix develop .#msc51 to compile prerequisites and run gateway-msc51 link-launcher.' >&2
  exit 1
fi
launcher_dir="$(realpath "$launcher_dir")"

# DOSBox's -c interpreter parses its own quotes, independent of Bash.
for path in "$dos_dir" "$launcher_dir"; do
  case "$path" in
    *'"'*|*$'\n'*|*$'\r'*) echo "unsupported DOSBox mount path: $path" >&2; exit 2 ;;
  esac
done

exec dosbox-staging \
  -c "mount e \"$dos_dir\" -ro" \
  -c "mount d \"$launcher_dir\"" \
  -c 'e:' \
  -c "D:\\GATEWAY.EXE $mode" \
  --exit
