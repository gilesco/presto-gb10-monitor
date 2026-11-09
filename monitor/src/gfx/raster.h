// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#include "grid.h"
#include "theme.h"

// Draws one grid cell as 10×24 RGB565 pixels. Arduino-free.
namespace raster {

constexpr int CELL_W = 10;
constexpr int CELL_H = 24;  // 20 px Terminus glyph + 2 px above + 2 px below
constexpr int PAD_TOP = (CELL_H - 20) / 2;

// dst points at the cell's top-left pixel; stride is in pixels (480 for the
// panel framebuffer).
void cell(const Cell& c, const theme::Theme& t, uint16_t* dst, int stride);

}  // namespace raster
