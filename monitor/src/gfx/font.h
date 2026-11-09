// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

// Terminus 10×20 glyph lookup (SPEC.md §6). Arduino-free.
namespace font {

constexpr int W = 10;
constexpr int H = 20;

// 20 rows, MSB = leftmost pixel. Missing codepoints fall back to '?'
// (and missing bold glyphs to the regular face).
const uint16_t* rows(uint32_t codepoint, bool bold);

bool has(uint32_t codepoint, bool bold);

}  // namespace font
