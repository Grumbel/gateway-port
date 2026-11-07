#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Extract PIC images and prepare a reviewable, resumable image-generation queue."""
import argparse
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

PROMPT = """Restore this original 1990s VGA adventure-game image as a richly detailed hand-painted illustration.
Keep the exact composition and canvas boundaries. Preserve the position, size, silhouette and orientation of every visible object, character and architectural feature; preserve perspective, framing, poses, expressions, clothing, colors and lighting. Do not crop, extend, move, add or remove significant elements. Preserve recognizable identities and proportions. Treat the reference as a geometry map, not merely inspiration.
Use traditional acrylic and gouache, expressive ink outlines, saturated colors and dramatic contrast. Add restrained dry brush, pigment granulation, paper tooth, scumbling and crosshatching. Recover the appearance of hand-painted artwork before VGA palette reduction. Avoid pixelation, dithering, halftones, plastic smoothing, photorealism, 3D, anime and modern cartoon styling. Reconstruct ambiguous detail conservatively.
{geometry}
Return only the restored image. Human review is required for geometry and fidelity.
"""


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dimensions(path):
    with path.open('rb') as f:
        header = f.read(24)
    if len(header) != 24 or header[:8] != b'\x89PNG\r\n\x1a\n' or header[12:16] != b'IHDR':
        raise ValueError(f'{path}: invalid PNG header')
    return struct.unpack('>II', header[16:24])


def prepare(args):
    root = args.output.resolve()
    raw = root / 'originals'
    if root.exists() and any(root.iterdir()):
        raise ValueError('output directory is not empty; use a new directory to avoid overwriting work')
    inputs = sorted(args.data.glob('*.[Pp][Ii][Cc]'), key=lambda p: p.name.upper())
    if not inputs:
        raise ValueError('no PIC files found')
    if len({p.stem.casefold() for p in inputs}) != len(inputs):
        raise ValueError('PIC filenames collide after removing extensions')
    root.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([str(args.extractor.resolve()), '-o', str(raw), *map(str, inputs)],
                            capture_output=True, text=True)
    (root / 'extraction.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise ValueError(f'extraction failed; see {root / "extraction.log"}')
    images = sorted(raw.glob('*/*.png'))
    if not images:
        raise ValueError('extractor produced no PNG images')
    records = []
    for path in images:
        width, height = dimensions(path)
        archive = path.parent.name.upper()
        entry = int(path.stem)
        room = (archive in {f'GATE_{n:03}' for n in range(102, 115)} and
                (width, height) == (379, 224))
        # Backgrounds identified in docs/intro-reconstruction.md, not sprites.
        intro = ((archive == 'GATE_301' and entry in range(6)) or
                 (archive == 'GATE_302' and entry in (0, 33)) or
                 (archive == 'GATE_303' and entry in (0, 42)) or
                 (archive == 'GATE_310' and entry == 4)) and (width, height) == (320, 200)
        if not (room or intro):
            continue
        ident = path.relative_to(raw).with_suffix('').as_posix()
        if intro:
            target_w, target_h = 1440, 1080
            geometry = ('This is a complete 320×200 VGA intro background with '
                        'non-square pixels. Correct it to a 4:3 square-pixel canvas '
                        'at 1440×1080 or higher; keep all content in its original '
                        'relative position and do not crop or extend the frame.')
        else:
            target_w, target_h = width * 3, height * 3
            geometry = (f'The reference is a {width}×{height} room image, not a full '
                        f'screen. Preserve its exact aspect ratio and boundaries; target '
                        f'{target_w}×{target_h} or a larger image with the same ratio. '
                        'Do not add interface elements or stretch it to 4:3.')
        records.append({'id': ident, 'source': path.relative_to(root).as_posix(),
                        'source_sha256': digest(path), 'kind': 'intro' if intro else 'room',
                        'width': width, 'height': height,
                        'suggested_width': target_w, 'suggested_height': target_h,
                        'output': f'restored/{ident}.png', 'status': 'pending',
                        'prompt': PROMPT.format(geometry=geometry)})
    if not records:
        raise ValueError('no scene-image candidates found')
    manifest = {'version': 1, 'input_archives': [{'name': p.name, 'sha256': digest(p)} for p in inputs],
                'extraction_order': 'case-insensitive filename order', 'items': records}
    (root / 'manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n')
    print(f'Prepared {len(records)} images in {root}; review extraction.log and manifest.json')


def check(args):
    root = args.output.resolve()
    manifest = json.loads((root / 'manifest.json').read_text())
    counts = {'pending': 0, 'ready_for_review': 0, 'invalid': 0}
    for item in manifest['items']:
        src = root / item['source']
        out = root / item['output']
        if not src.is_file() or digest(src) != item['source_sha256']:
            print(f'{item["id"]}: source missing or changed')
            counts['invalid'] += 1
        elif not out.exists():
            counts['pending'] += 1
        else:
            try:
                w, h = dimensions(out)
                if (w, h) != (item['suggested_width'], item['suggested_height']):
                    print(f'{item["id"]}: output {w}x{h}; suggested {item["suggested_width"]}x{item["suggested_height"]} (review)')
                counts['ready_for_review'] += 1
            except (ValueError, OSError) as e:
                print(e)
                counts['invalid'] += 1
    print(json.dumps(counts))
    return bool(counts['invalid'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('prepare')
    p.add_argument('--data', type=Path, required=True)
    p.add_argument('--extractor', type=Path, default=Path('build/gateway-extract'))
    p.add_argument('--output', type=Path, default=Path('build/remaster'))
    c = sub.add_parser('check')
    c.add_argument('--output', type=Path, default=Path('build/remaster'))
    args = parser.parse_args()
    try:
        return prepare(args) if args.command == 'prepare' else check(args)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as e:
        print(f'remaster: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
