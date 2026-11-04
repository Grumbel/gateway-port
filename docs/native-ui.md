<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
-->

# Native UI direction

The long-term goal is a modern, configurable interface for Gateway, using
SDL3's 2D renderer and the player's original game assets. No direct OpenGL
rendering is required. Preserve the original DOS interface as a selectable
reference during migration.

## First working slice

`gateway --ui-layout widescreen` draws the aspect-corrected **complete DOS
framebuffer**, unchanged, in the largest rectangle that fits the window. It
uses sufficiently wide side margins for placeholder action names and names
from the existing debugger's scene tracker. At 1280x720 and 4:3 content, the
scene is 960x720, with 160-pixel side panels. In narrower windows, the panels
are hidden; there is no cropping. The mouse and debug overlay use the same
rectangle as video presentation. `--ui-layout original` remains the default.

This is deliberately a presentation-only preview. Clicking the action labels
does not yet dispatch a command, and the inferred PIC/RGN scene may be out of
date or wrong. The 8x8 SDL built-in debugging text is **not the intended font**.
The core layout is implemented independently of SDL and covered by unit tests.

## Requirements to implement incrementally

1. **Layout:** Keep scene/art, story text, inventory, actions and visible-object
   list as independent movable and resizable panels. On widescreen displays,
   actions and objects should be unobtrusive at opposite corners; on narrower
   displays, the user can access these lists without covering the art.
2. **Typography:** Use configurable proportional fonts, text scaling and
   history. SDL3 itself provides drawing but not production TrueType font
   rasterization: use SDL3_ttf or a similarly small font rasterizer atop its
   renderer. Keep story text external to the source repository.
3. **Input:** Both the original text parser and mouse interactions must remain
   available. Tab-complete command words and object names, starting from the
   decoded vocabulary and known visible objects; do not offer parser actions
   whose validity has not been established.
4. **Saves:** Provide a native save/load menu with a thumbnail of the room at
   the moment of saving. Keep screenshot and UI metadata as sidecars next to
   each **unmodified** original save: the DOS save format must remain intact.
   If the room scene cannot be captured reliably, show a clear fallback.
5. **Map:** A room map may be a separate native panel. The existing room-exit
   table and map reference are research inputs, not proof that all links are
   usable; distinguish discovered/visited rooms and conditional exits.
6. **Artwork:** Eventually replace source-resolution scene presentation with
   independently upscaled versions of the original illustrations. Preserve
   original scene framing, clickable region alignment, animation layering and
   corrected aspect ratio. Upscaled resources belong outside the repository.

## Backend boundary

Build a presentation model (current scene, visible objects, inventory, text
history, available actions, exits, saves) separately from guest execution.
For now, individual fields may be sourced from the original emulator or from
explicit research snapshots. Mark inferred/unknown values and keep the native
presentation working even when they are unavailable. Later replace the backend
field by field with verified game logic.

The first blocker to a fully independent picture/text interface is reliable
current-room / current-picture selection and interception of the original
text-output stream. Neither is required for the initial shell.
