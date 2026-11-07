<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception -->

# Room art remaster queue

The pipeline prepares image-generation work; it does not call a model or spend
credits. Build `gateway-extract`, then run:

```sh
python3 tools/remaster_pipeline.py prepare --data ../gateway-dos --output build/remaster
python3 tools/remaster_pipeline.py check --output build/remaster
```

Preparation requires an empty output directory. All PIC archives are passed to
the extractor together, sorted by filename, to retain inherited palettes. Check
`extraction.log` for warnings. `originals/` contains all decodable PIC images;
`manifest.json` queues only 379×224 images in GATE_102 through GATE_114, the
documented room-art archives. This is a dimension-based candidate selection,
not a semantic classification: review candidates and omit duplicates, unsuitable
states or incomplete compositions. It excludes GUI fragments and intro art, but
may also miss scene images with other dimensions. The extractor may produce XOR
composites or leave unplaced deltas; see its log and the extraction notes in
README. These files do not capture every possible game-state composition.

For each selected manifest item, Luna can supply `source` and `prompt` to an
image-generation tool, then save its result as the specified `output` path under
the same directory. Create parent directories as needed. Skip output files that
already exist to resume. Record model, settings and review decisions separately;
the manifest's `status` is initially `pending` and is not automatically updated.
The checker verifies source hashes, detects missing outputs and reads PNG
headers. It flags dimensions that differ from the suggested dimensions; it does
not validate image fidelity, complete PNG data, alpha, or semantic correctness.
An existing output is only ready for human review, not approved.

The prompt preserves room-image proportions and uses 3× dimensions as a
minimum working target, rather than applying 640×400 screenshot correction to
individual room assets. Verify display geometry against the original game before
final production. Compare each result to the original for object positions,
character identity, borders and color; do not use generated elements as evidence
of original game content. Room layers, hotspots and animation can require separate
reconstruction. Keep generated and original assets under ignored `build/` and out
of Git.
