// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once

#include "debug_scene.hpp"
#include <array>
#include <cstdint>
#include <iosfwd>

namespace gateway {
// Serialize the frozen picker table sampled *at entry*, next to the native
// return. These comment records are auxiliary to --trace-picker-follow's
// instruction TSV; no guest state is modified and no object names are copied.
void write_picker_scene_trace(std::ostream &output, unsigned call,
                              const DebugSceneView &scene, std::uint16_t native_ax,
                              std::array<std::uint16_t, 3> native_outputs);
} // namespace gateway
