#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
#
# Regenerate the machine-generated sources that contain original code and
# therefore stay untracked under build/: the RTLink manifest, the verified
# NASM disassembly (build/disasm) and the lifted C (build/lift). Deleting
# build/ loses nothing that this script cannot rebuild from the original
# files. Ghidra output is separate (docs/ghidra.md).
#
# usage: tools/regen_build_sources.sh [DOS-DIR] [UNIT...]
#   DOS-DIR defaults to ../gateway-dos; UNIT defaults to ovl-038.
# Run inside `nix develop` (needs cmake, a C compiler, python3 and nasm).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
dos=${1:-$root/../gateway-dos}
shift || true
units=("$@")
[ ${#units[@]} -gt 0 ] || units=(ovl-038)
build=${GATEWAY_BUILD_DIR:-$root/build}

for file in GATEMAIN.EXE GATEMAIN.OVL GATE.RTL; do
    [ -f "$dos/$file" ] || { echo "missing $dos/$file" >&2; exit 1; }
done

cmake -S "$root" -B "$build" >/dev/null
cmake --build "$build" --target gateway-overlay -j"$(nproc)" >/dev/null

manifest=$build/handler-coverage/rtlink-manifest.json
mkdir -p "$(dirname "$manifest")" "$build/lift"
"$build/gateway-overlay" "$dos/GATEMAIN.EXE" "$dos/GATEMAIN.OVL" --fixups \
    > "$manifest.tmp"
mv "$manifest.tmp" "$manifest"
echo "manifest: $manifest"

python3 "$root/tools/disassemble_gateway.py" "$dos/GATEMAIN.EXE" \
    "$dos/GATEMAIN.OVL" --rtl "$dos/GATE.RTL" --manifest "$manifest" \
    --output "$build/disasm" --verify
echo "disassembly: $build/disasm (NASM round trip verified)"

for unit in "${units[@]}"; do
    python3 "$root/tools/lift_x86.py" "$dos/GATEMAIN.EXE" "$dos/GATEMAIN.OVL" \
        --rtl "$dos/GATE.RTL" --manifest "$manifest" \
        --unit "$unit" --output "$build/lift/$unit.c"
    cc -std=c99 -O2 -Wall -Wextra -Wno-unused-label -fPIC -shared \
        -I"$root/src" -o "$build/lift/$unit.so" "$build/lift/$unit.c"
    echo "lifted: $build/lift/$unit.c -> $unit.so"
done
