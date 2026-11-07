#!/bin/sh
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
set -eu

if ! command -v wcl >/dev/null 2>&1; then
    echo 'OpenWatcom wcl (16-bit compiler/linker) is required' >&2
    exit 127
fi

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output_dir=${1:-"$repo_dir/build/intro-dos"}
mkdir -p "$output_dir"
cd "$output_dir"

# -0: 8086 instructions; -ms: small model; -l=dos: DOS MZ executable.
# Compile an isolated recovered C runtime routine with a minimal DOS harness.
# The complete reconstructed intro cannot be linked this way yet.
wcl -y -0 -ms -l=dos -fe=INTROCHK.EXE \
    "$repo_dir/decomp/launcher/dos/intro_probe.c" \
    "$repo_dir/decomp/launcher/gate_c_runtime_random.c"
echo "Created $output_dir/INTROCHK.EXE (diagnostic, not playable Gateway)"
