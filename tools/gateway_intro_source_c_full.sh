#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

# Development-only source-C intro: generate a full silent recording from the
# original private assets, then replay it through the local SDL3 build.
set -euo pipefail
: "${GATEWAY_BUILD_DIR:?Enter the Gateway development shell first}"

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo 'usage: gateway-intro-source-c-full DOS-DIR [guest-22s.bin]' >&2
  echo '   or: gateway-intro-source-c-full guest-22s.bin DOS-DIR' >&2
  exit 2
fi
dos_dir="$1"
snapshot="--from-original"
if [ "$#" -eq 2 ]; then
  if [ -f "$1" ] && [ -d "$2" ]; then
    snapshot="$1"
    dos_dir="$2"
  else
    snapshot="$2"
  fi
fi
if [ ! -f "$dos_dir/GATE.EXE" ]; then
  echo "missing $dos_dir/GATE.EXE" >&2
  exit 1
fi

gateway-build
replay_dir="$(mktemp -d)"
trap 'rm -f "$replay_dir/replay.gvr" "$replay_dir/final.ppm"; rmdir "$replay_dir"' EXIT
"$GATEWAY_BUILD_DIR/gateway-intro-source-c-composite" \
  "$snapshot" "$dos_dir" "$replay_dir/final.ppm" --full \
  --record "$replay_dir/replay.gvr"
"$GATEWAY_BUILD_DIR/gateway-intro-source-c-replay" "$replay_dir/replay.gvr"
